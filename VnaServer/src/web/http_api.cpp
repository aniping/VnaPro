/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/http_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <future>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "vna_core/frame_layout.h"
#include "vna_measurement/measurement_processor.h"
#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"
#include "web/frame_selection.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace VnaWeb {
HttpResponse HttpError(unsigned status, const std::string &code, const std::string &message)
{
    HttpResponse response;
    response.status = status;
    response.json = Json::MakeObject();
    Json error = Json::MakeObject();
    error["code"] = Json(code);
    error["message"] = Json(message);
    response.json["error"] = std::move(error);
    return response;
}

namespace {
constexpr unsigned LEASE_SECONDS = 30U;
constexpr unsigned RETENTION_SECONDS = 60U;
constexpr std::size_t SESSION_LIMIT = 32U;
constexpr std::size_t RETRY_LIMIT = 32U;
constexpr std::size_t OPERATION_LIMIT = 128U;
constexpr std::size_t NAME_LENGTH_LIMIT = 128U;
constexpr unsigned DISPLAY_POINT_LIMIT = 2001U;
constexpr unsigned SWEEP_POINT_LIMIT = 10001U;
constexpr unsigned TOKEN_WORD_COUNT = 4U;
constexpr int TOKEN_WORD_HEX_WIDTH = 8;
constexpr unsigned DECIMAL_BASE = 10U;
constexpr unsigned CONTROL_CHARACTER_LIMIT = 32U;

std::string Text(const Json &value, const std::string &key, bool optional = false)
{
    const Json *item = value.Find(key);
    if (item == nullptr && optional) {
        return std::string();
    }
    if (item == nullptr || item->type != Json::Type::STRING) {
        throw std::runtime_error(key + " must be a string");
    }
    return item->string;
}

double Numeric(const Json &value, const std::string &key)
{
    const Json *item = value.Find(key);
    if (item == nullptr || item->type != Json::Type::NUMBER || !std::isfinite(item->number)) {
        throw std::runtime_error(key + " must be finite numeric data");
    }
    return item->number;
}

unsigned PositiveInteger(const Json &value, const std::string &key, unsigned maximum)
{
    const double number = Numeric(value, key);
    if (number < 1 || number > maximum || std::floor(number) != number) {
        throw std::runtime_error(key + " out of range");
    }
    return static_cast<unsigned>(number);
}

std::uint64_t Revision(const Json &value)
{
    const std::string string = Text(value, "expectedRevision");
    if (string.empty()) {
        throw std::runtime_error("expectedRevision is empty");
    }
    std::uint64_t result = 0;
    for (char digit : string) {
        if (digit < '0' || digit > '9' ||
            result > ((std::numeric_limits<std::uint64_t>::max)() - (digit - '0')) / DECIMAL_BASE) {
            throw std::runtime_error("expectedRevision must be a decimal uint64 string");
        }
        result = result * DECIMAL_BASE + static_cast<unsigned>(digit - '0');
    }
    return result;
}

void Allowed(const Json &value, const std::vector<std::string> &keys)
{
    if (value.type != Json::Type::OBJECT) {
        throw std::runtime_error("JSON body must be an object");
    }
    for (const auto &item : value.object) {
        if (std::find(keys.begin(), keys.end(), item.first) == keys.end()) {
            throw std::runtime_error("Unknown member: " + item.first);
        }
    }
}

bool Ready(const VnaService::Operation &value)
{
    return value.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string CoreStateName(VnaCore::CoreState value)
{
    switch (value) {
        case VnaCore::CoreState::UNINITIALIZED:
            return "Uninitialized";
        case VnaCore::CoreState::READY:
            return "Ready";
        case VnaCore::CoreState::RUNNING:
            return "Running";
        case VnaCore::CoreState::FAULTED:
            return "Faulted";
        case VnaCore::CoreState::CLOSED:
            return "Closed";
        default:
            return "Uninitialized";
    }
}

Json MeasurementJson(const VnaService::Measurement &measurement)
{
    Json value = Json::MakeObject();
    value["name"] = Json(measurement.name);
    value["receiverKind"] = Json(std::string(1, measurement.receiverKind));
    value["receiverPort"] = Json(measurement.receiverPort);
    value["sourcePort"] = Json(measurement.sourcePort);
    value["parameter"] = Json(measurement.Parameter());
    const bool ratio = measurement.kind == VnaService::MeasurementKind::S_PARAMETER;
    value["kind"] = Json(ratio ? "sParameter" : "receiver");
    value["unit"] = Json(ratio ? "1" : "a.u.");
    value["calibration"] = Json(ratio ? "uncalibrated" : "raw");
    return value;
}

bool SafeName(const std::string &value)
{
    if (value.empty() || value.size() > NAME_LENGTH_LIMIT || value == "." || value == "..") {
        return false;
    }
    for (unsigned char character : value) {
        if (character < CONTROL_CHARACTER_LIMIT || character == '/' || character == '\\') {
            return false;
        }
    }
    return true;
}
}  // 匿名命名空间

HttpApi::HttpApi(VnaService::InstrumentService &service, Now clock) : service_(service), now_(std::move(clock))
{
    if (!now_) {
        throw std::invalid_argument("HTTP clock callback is required");
    }
}

std::string HttpApi::Token()
{
    std::ostringstream value;
    value.exceptions(std::ios::badbit | std::ios::failbit);
    value << "web-" << std::hex << std::setfill('0');
    for (unsigned index = 0; index < TOKEN_WORD_COUNT; ++index) {
        value << std::setw(TOKEN_WORD_HEX_WIDTH) << static_cast<std::uint32_t>(random_());
    }
    return value.str();
}

std::string HttpApi::HeaderToken(const HttpRequest &request) const
{
    const auto found = request.headers.find("x-vna-session");
    return found == request.headers.end() ? std::string() : found->second;
}

void HttpApi::Poll()
{
    service_.Poll();
    const auto time = now_();
    for (auto &item : records_) {
        HttpOperationRecord &record = item.second;
        if (!record.done && Ready(record.operation)) {
            auto result = record.operation.get();
            record.result = std::move(result);
            record.completed = time;
            record.done = true;
        }
    }
    for (auto record = records_.begin(); record != records_.end();) {
        if (record->second.done && time - record->second.completed >= std::chrono::seconds(RETENTION_SECONDS)) {
            record = records_.erase(record);
        } else {
            ++record;
        }
    }
    for (auto &item : sessions_) {
        HttpSession &session = item.second;
        if (service_.ControlOwner() == item.first && (shuttingDown_ || time >= session.expires || session.releasing)) {
            session.releasing = true;
            service_.BeginReleaseControl(item.first);
            if (!service_.HasPending()) {
                service_.ReleaseControl(item.first);
            }
        }
    }
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (service_.ControlOwner() != session->first &&
            time - session->second.touched >= std::chrono::seconds(RETENTION_SECONDS)) {
            session = sessions_.erase(session);
        } else {
            ++session;
        }
    }
}

HttpResponse HttpApi::Status(const HttpRequest &request) const
{
    const auto snapshot = service_.Snapshot();
    const auto &settings = snapshot.settings;
    HttpResponse response;
    Json &value = response.json = Json::MakeObject();
    value["identity"] = Json(service_.Identity());
    value["backend"] = Json("NativeSimulator");
    const auto &profile = service_.Profile();
    Json capabilities = Json::MakeObject();
    capabilities["portCount"] = Json(profile.portCount);
    capabilities["minFrequencyHz"] = Json(profile.minFrequencyHz);
    capabilities["maxFrequencyHz"] = Json(profile.maxFrequencyHz);
    capabilities["minSweepPoints"] = Json(VnaService::MIN_SWEEP_POINTS);
    capabilities["maxSweepPoints"] = Json(VnaService::MAX_SWEEP_POINTS);
    value["capabilities"] = std::move(capabilities);
    value["state"] = Json(CoreStateName(snapshot.core.state));
    value["error"] = Json(snapshot.core.error);
    value["pending"] = Json(snapshot.pending);
    value["revision"] = Json(std::to_string(settings.revision));
    value["runId"] = Json(std::to_string(snapshot.core.runId));
    value["activeConfigValid"] =
        Json(snapshot.core.state != VnaCore::CoreState::FAULTED && snapshot.core.state != VnaCore::CoreState::CLOSED &&
             snapshot.core.configRevision != 0U && snapshot.core.configRevision == settings.revision);
    value["config"] = ConfigurationJson(settings);
    Json measurements = Json::MakeArray();
    for (const auto &measurement : settings.measurements) {
        measurements.array.push_back(MeasurementJson(measurement));
    }
    value["measurements"] = std::move(measurements);
    value["control"] = ControlJson(snapshot, request);
    return response;
}

HttpResponse HttpApi::Frame(const std::string &query) const
{
    const auto snapshot = service_.Snapshot();
    const auto selected = SelectFrameMeasurements(query, snapshot.settings.measurements);
    HttpResponse response;
    Json &value = response.json = Json::MakeObject();
    if (!snapshot.core.frame) {
        value["available"] = Json(false);
        value["current"] = Json(false);
        value["reason"] = Json("No completed frame");
        return response;
    }
    const auto &frame = *snapshot.core.frame;
    const std::size_t count = frame.plan.frequencyHz.size();
    if (count == 0U) {
        return HttpError(409, "no_physical_axis", "Frame does not have a verified physical frequency axis");
    }
    const unsigned display = static_cast<unsigned>((std::min)(count, static_cast<std::size_t>(DISPLAY_POINT_LIMIT)));
    std::vector<std::size_t> indexes;
    for (unsigned index = 0; index < display; ++index) {
        indexes.push_back(display == 1U ? 0U : index * (count - 1U) / (display - 1U));
    }
    value["available"] = Json(true);
    value["current"] =
        Json(!snapshot.finitePending && snapshot.core.state != VnaCore::CoreState::FAULTED &&
             snapshot.core.state != VnaCore::CoreState::CLOSED && frame.configRevision == snapshot.settings.revision &&
             frame.configRevision == snapshot.core.configRevision && frame.runId == snapshot.core.runId);
    value["channelId"] = Json(frame.channelId);
    value["runId"] = Json(std::to_string(frame.runId));
    value["revision"] = Json(std::to_string(frame.configRevision));
    value["frameSeq"] = Json(std::to_string(frame.frameSeq));
    value["originalPointCount"] = Json(static_cast<unsigned>(count));
    value["displayPointCount"] = Json(display);
    value["decimated"] = Json(count > display);
    Json axis = Json::MakeArray();
    for (auto index : indexes) {
        axis.array.emplace_back(frame.plan.frequencyHz[index]);
    }
    value["frequencyHz"] = std::move(axis);
    Json traces = Json::MakeArray();
    for (const auto &measurement : selected) {
        traces.array.push_back(TraceJson(frame, measurement, indexes));
    }
    value["traces"] = std::move(traces);
    return response;
}

HttpResponse HttpApi::OperationStatus(const std::string &id, const std::string &requester) const
{
    const auto found = records_.find(id);
    if (found == records_.end()) {
        return HttpError(404, "operation_not_found", "Operation is unknown or expired");
    }
    if (requester.empty() || requester != found->second.owner) {
        return HttpError(403, "not_operation_owner", "Operation result belongs to another session");
    }
    const HttpOperationRecord &record = found->second;
    HttpResponse response;
    response.json = Json::MakeObject();
    response.json["operationId"] = Json(id);
    response.json["state"] = Json(!record.done ? "pending" : record.result.ok ? "completed" : "failed");
    Json value;
    if (record.done) {
        value = Json::MakeObject();
        value["ok"] = Json(record.result.ok);
        value["errorCode"] = Json(static_cast<double>(record.result.errorCode));
        value["message"] = Json(record.result.message);
    }
    response.json["result"] = std::move(value);
    return response;
}

HttpResponse HttpApi::Accepted(const std::string &id, const std::string &owner) const
{
    HttpResponse response = OperationStatus(id, owner);
    response.status = 202;
    response.json.object.erase("result");
    return response;
}

HttpResponse HttpApi::Claim(const Json &body)
{
    Allowed(body, {"sessionId"});
    const std::string requested = Text(body, "sessionId", true);
    if (service_.ControlReleasing()) {
        return HttpError(409, "control_releasing", "Previous controller is still completing accepted operations");
    }
    if (!service_.ControlOwner().empty() && service_.ControlOwner() != requested) {
        return HttpError(409, "control_busy", "Another controller holds the instrument");
    }
    std::string id = requested;
    if (id.empty()) {
        if (sessions_.size() >= SESSION_LIMIT) {
            return HttpError(429, "session_limit", "Web session budget is full");
        }
        do {
            id = Token();
        } while (sessions_.count(id) != 0U);
        sessions_[id] = HttpSession();
    } else if (sessions_.count(id) == 0U) {
        return HttpError(403, "invalid_session", "Session is unknown or expired");
    }
    const auto time = now_();
    HttpResponse response;
    response.json = Json::MakeObject();
    response.json["sessionId"] = Json(id);
    response.json["hasControl"] = Json(true);
    response.json["leaseSeconds"] = Json(LEASE_SECONDS);
    if (service_.ControlOwner().empty() && !service_.TryAcquireControl(id)) {
        return HttpError(409, "control_busy", "Instrument control is unavailable");
    }
    HttpSession &session = sessions_[id];
    session.releasing = false;
    session.expires = time + std::chrono::seconds(LEASE_SECONDS);
    session.touched = time;
    return response;
}

HttpResponse HttpApi::Release(const std::string &id)
{
    const auto found = sessions_.find(id);
    if (found == sessions_.end()) {
        return HttpError(403, "invalid_session", "Missing or expired session");
    }
    if (service_.ControlOwner() != id) {
        return HttpError(403, "not_controller", "Session does not hold instrument control");
    }
    found->second.releasing = true;
    found->second.touched = now_();
    service_.BeginReleaseControl(id);
    if (!service_.HasPending()) {
        service_.ReleaseControl(id);
    }
    HttpResponse response;
    response.json = Json::MakeObject();
    response.json["releasing"] = Json(true);
    return response;
}

HttpResponse HttpApi::Write(const HttpRequest &request, const Json &body)
{
    const std::string id = HeaderToken(request);
    const auto found = sessions_.find(id);
    if (found == sessions_.end()) {
        return HttpError(403, "invalid_session", "X-Vna-Session is missing or expired");
    }
    HttpSession &session = found->second;
    if (service_.ControlOwner() != id || session.releasing || service_.ControlReleasing() ||
        now_() >= session.expires) {
        return HttpError(409, "not_controller", "Session does not have active write control");
    }
    const std::string requestId = Text(body, "requestId");
    if (!SafeName(requestId)) {
        throw std::runtime_error("requestId must contain 1..128 safe characters");
    }
    const std::string fingerprint = request.method + '\n' + request.path + '\n' + StringifyJson(body);
    HttpResponse previous;
    if (RetryResponse(session, {requestId, fingerprint, id}, previous)) {
        return previous;
    }
    if (records_.size() >= OPERATION_LIMIT) {
        return HttpError(429, "operation_limit", "Operation record budget is full");
    }
    return RecordWrite(request, body, session, {requestId, fingerprint, id});
}

HttpResponse HttpApi::Route(const HttpRequest &request)
{
    Poll();  // 即使处于两次事件循环之间，也处理租约过期。
    if (request.method == "GET") {
        if (request.path == "/api/v1/status") {
            return Status(request);
        }
        if (request.path == "/api/v1/frame") {
            return Frame(request.query);
        }
        const std::string prefix = "/api/v1/operations/";
        if (request.path.find(prefix) == 0U) {
            return OperationStatus(request.path.substr(prefix.size()), HeaderToken(request));
        }
        return HttpError(404, "route_not_found", "Unknown API route");
    }
    if (shuttingDown_) {
        return HttpError(503, "shutting_down", "Server is shutting down");
    }
    if (request.method != "POST" && request.method != "DELETE" && request.method != "PUT") {
        return HttpError(405, "method_not_allowed", "Unsupported API method");
    }
    if (request.method == "PUT" && request.path.find("/api/v1/measurements/") != 0U) {
        return HttpError(405, "method_not_allowed", "PUT requires an individual measurement route");
    }
    if (request.method != "POST" &&
        (request.path == "/api/v1/config" || request.path == "/api/v1/operations" ||
            request.path == "/api/v1/measurements" || request.path == "/api/v1/control/claim" ||
            request.path == "/api/v1/control/release")) {
        return HttpError(405, "method_not_allowed", "This API route requires POST");
    }
    if (request.method != "DELETE" && request.method != "PUT" && request.path.find("/api/v1/measurements/") == 0U) {
        return HttpError(405, "method_not_allowed", "Measurement update or deletion requires PUT or DELETE");
    }
    Json body = request.body.empty() ? Json::MakeObject() : ParseJson(request.body);
    if (body.type != Json::Type::OBJECT) {
        throw std::runtime_error("JSON body must be an object");
    }
    if (request.path == "/api/v1/control/claim" && request.method == "POST") {
        return Claim(body);
    }
    if (request.path == "/api/v1/control/release" && request.method == "POST") {
        Allowed(body, {});
        return Release(HeaderToken(request));
    }
    return Write(request, body);
}

HttpResponse HttpApi::Handle(const HttpRequest &request)
{
    try {
        return Route(request);
    } catch (const std::exception &error) {
        return HttpError(400, "invalid_request", error.what());
    }
}

void HttpApi::BeginShutdown()
{
    shuttingDown_ = true;
    for (auto &session : sessions_) {
        session.second.releasing = true;
        service_.BeginReleaseControl(session.first);
    }
    Poll();
}

bool HttpApi::Drained() const
{
    for (const auto &item : records_) {
        if (!item.second.done) {
            return false;
        }
    }
    for (const auto &item : sessions_) {
        if (service_.ControlOwner() == item.first) {
            return false;
        }
    }
    return true;
}

Json HttpApi::ConfigurationJson(const VnaService::InstrumentSettings &settings)
{
    Json config = Json::MakeObject();
    config["startHz"] = Json(settings.startHz);
    config["stopHz"] = Json(settings.stopHz);
    config["points"] = Json(settings.points);
    config["ifBandwidthHz"] = Json(settings.ifBandwidthHz);
    config["triggerSource"] = Json(settings.triggerSource == VnaService::TriggerSource::IMMEDIATE ? "IMM" : "MAN");
    config["sweepMode"] = Json(settings.sweepMode == VnaService::SweepMode::HOLD     ? "HOLD"
                               : settings.sweepMode == VnaService::SweepMode::SINGLE ? "SING"
                                                                                     : "CONT");
    return config;
}

Json HttpApi::ControlJson(const VnaService::InstrumentSnapshot &snapshot, const HttpRequest &request) const
{
    Json control = Json::MakeObject();
    const std::string owner = snapshot.controlOwner;
    const std::string requester = HeaderToken(request);
    const bool mine = !owner.empty() && owner != "SCPI" && owner == requester && !snapshot.releasing;
    control["owner"] = Json(owner.empty() ? "none" : owner == "SCPI" ? "scpi" : "web");
    control["hasControl"] = Json(mine);
    control["releasing"] = Json(snapshot.releasing);
    double remaining = 0;
    const auto session = sessions_.find(owner);
    if (session != sessions_.end()) {
        remaining =
            (std::max)(0.0, std::chrono::duration<double, std::milli>(session->second.expires - now_()).count());
    }
    control["leaseRemainingMs"] = Json(remaining);
    const std::string reason = mine                 ? std::string()
                               : snapshot.releasing ? "Control is releasing"
                               : owner == "SCPI"    ? "SCPI controller active"
                               : owner.empty()      ? "Claim control to write"
                                                    : "Another Web session controls instrument";
    control["readOnlyReason"] = Json(reason);
    return control;
}

Json HttpApi::TraceJson(const VnaCore::MeasurementFrame &frame, const VnaService::Measurement &measurement,
    const std::vector<std::size_t> &indexes)
{
    Json trace = MeasurementJson(measurement);
    Json real = Json::MakeArray();
    Json imaginary = Json::MakeArray();
    Json validity = Json::MakeArray();
    const auto result = VnaMeasurement::MeasurementProcessor::Calculate(frame, measurement);
    for (auto index : indexes) {
        const bool valid = result.available && index < result.points.size() && result.points[index].valid;
        real.array.push_back(valid ? Json(result.points[index].real) : Json());
        imaginary.array.push_back(valid ? Json(result.points[index].imag) : Json());
        validity.array.emplace_back(valid);
    }
    trace["available"] = Json(result.available);
    trace["real"] = std::move(real);
    trace["imag"] = std::move(imaginary);
    trace["valid"] = std::move(validity);
    return trace;
}

HttpApi::HttpWriteResult HttpApi::StartWrite(const HttpRequest &request, const Json &body, const std::string &id)
{
    HttpWriteResult result;
    auto &operationValue = result.operation;
    if (request.path == "/api/v1/config" && request.method == "POST") {
        Allowed(body, {"requestId", "expectedRevision", "startHz", "stopHz", "points", "ifBandwidthHz"});
        const auto expected = Revision(body);
        if (expected != service_.Settings().revision) {
            result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
            return result;
        }
        const auto patch = ConfigurationPatch(body);
        operationValue = service_.ApplyConfiguration(patch, expected, id);
    } else if (request.path == "/api/v1/operations" && request.method == "POST") {
        Allowed(body, {"requestId", "action"});
        operationValue = StartAction(body, id);
    } else if (request.path == "/api/v1/measurements" && request.method == "POST") {
        Allowed(body, {"requestId", "expectedRevision", "name", "kind", "receiverKind", "receiverPort", "sourcePort"});
        const auto expected = Revision(body);
        if (expected != service_.Settings().revision) {
            result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
            return result;
        }
        const auto measurement = MeasurementDefinition(body);
        operationValue = service_.DefineMeasurement(measurement, expected, id);
    } else if (request.path.find("/api/v1/measurements/") == 0U &&
               (request.method == "DELETE" || request.method == "PUT")) {
        return StartMeasurementWrite(request, body, id);
    } else {
        result.response = HttpError(404, "route_not_found", "Unknown API route");
        return result;
    }
    result.admitted = true;
    return result;
}

HttpApi::HttpWriteResult HttpApi::StartMeasurementWrite(
    const HttpRequest &request, const Json &body, const std::string &id)
{
    HttpWriteResult result;
    const bool update = request.method == "PUT";
    Allowed(body, update ? std::vector<std::string>{"requestId", "expectedRevision", "kind", "receiverKind",
                               "receiverPort", "sourcePort"}
                         : std::vector<std::string>{"requestId", "expectedRevision"});
    const auto expected = Revision(body);
    if (expected != service_.Settings().revision) {
        result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
        return result;
    }
    const std::string name = request.path.substr(std::string("/api/v1/measurements/").size());
    if (!SafeName(name)) {
        throw std::runtime_error("Invalid measurement name");
    }
    if (update) {
        Text(body, "kind");
        // 名称只取自路径，正文不能把更新变成重命名或新增测量。
        Json definition = body;
        definition["name"] = Json(name);
        result.operation = service_.UpdateMeasurement(MeasurementDefinition(definition), expected, id);
    } else {
        result.operation = service_.DeleteMeasurement(name, expected, id);
    }
    result.admitted = true;
    return result;
}

VnaService::ConfigPatch HttpApi::ConfigurationPatch(const Json &body)
{
    VnaService::ConfigPatch patch;
    if (body.Find("startHz")) {
        patch.hasStartHz = true;
        patch.startHz = Numeric(body, "startHz");
    }
    if (body.Find("stopHz")) {
        patch.hasStopHz = true;
        patch.stopHz = Numeric(body, "stopHz");
    }
    if (body.Find("points")) {
        patch.hasPoints = true;
        patch.points = PositiveInteger(body, "points", SWEEP_POINT_LIMIT);
    }
    if (body.Find("ifBandwidthHz")) {
        patch.hasIfBandwidthHz = true;
        patch.ifBandwidthHz = Numeric(body, "ifBandwidthHz");
    }
    return patch;
}

VnaService::Measurement HttpApi::MeasurementDefinition(const Json &body)
{
    VnaService::Measurement measurement;
    measurement.name = Text(body, "name");
    const auto kind = Text(body, "receiverKind");
    if (!SafeName(measurement.name) || kind.size() != 1U ||
        (kind != "a" && kind != "b" && kind != "A" && kind != "B")) {
        throw std::runtime_error("Invalid measurement name or receiver kind");
    }
    measurement.receiverKind = kind.front();
    measurement.receiverPort = PositiveInteger(body, "receiverPort", VnaCore::PHYSICAL_PORT_COUNT);
    measurement.sourcePort = PositiveInteger(body, "sourcePort", VnaCore::PHYSICAL_PORT_COUNT);
    if (body.Find("kind")) {
        const auto measurementKind = Text(body, "kind");
        if (measurementKind != "receiver" && measurementKind != "sParameter") {
            throw std::runtime_error("Unsupported measurement kind");
        }
        measurement.kind = measurementKind == "sParameter" ? VnaService::MeasurementKind::S_PARAMETER
                                                           : VnaService::MeasurementKind::RECEIVER;
    }
    return measurement;
}

VnaService::Operation HttpApi::StartAction(const Json &body, const std::string &id)
{
    const std::string action = Text(body, "action");
    if (action == "single") {
        return service_.RunSingle(id);
    } else if (action == "continuous") {
        return service_.RunContinuous(id);
    } else if (action == "hold") {
        return service_.SetSweepMode(VnaService::SweepMode::HOLD, id);
    } else if (action == "trigger") {
        return service_.Trigger(id);
    } else {
        throw std::runtime_error("Unknown operation action");
    }
}

bool HttpApi::RetryResponse(const HttpSession &session, const HttpWriteIdentity &request, HttpResponse &response) const
{
    for (const HttpRetry &retry : session.retries) {
        if (retry.requestId != request.requestId) {
            continue;
        }
        if (retry.fingerprint != request.fingerprint) {
            response = HttpError(409, "request_id_conflict", "requestId was used with different content");
            return true;
        }
        if (records_.count(retry.operationId) == 0U) {
            response = HttpError(410, "operation_expired", "Original operation record has expired");
            return true;
        }
        response = Accepted(retry.operationId, request.owner);
        return true;
    }
    return false;
}

HttpResponse HttpApi::RecordWrite(
    const HttpRequest &request, const Json &body, HttpSession &session, const HttpWriteIdentity &identity)
{
    const auto time = now_();
    const std::string operationId = "op-" + std::to_string(++sequence_);
    HttpOperationRecord record;
    record.owner = identity.owner;
    record.created = time;
    const auto reserved = records_.emplace(operationId, std::move(record)).first;
    bool admitted = false;
    try {
        session.retries.push_back({identity.requestId, identity.fingerprint, operationId});
        auto submitted = StartWrite(request, body, identity.owner);
        if (!submitted.admitted) {
            RollbackWrite(session, operationId);
            return submitted.response;
        }
        reserved->second.operation = submitted.operation;
        admitted = true;
        if (Ready(submitted.operation)) {
            const auto result = submitted.operation.get();
            if (!result.ok) {
                RollbackWrite(session, operationId);
                return HttpError(result.errorCode == VnaService::Detail::INPUT_BUFFER_OVERRUN ? 429U : 409U,
                    "operation_rejected", result.message);
            }
        }
    } catch (...) {
        if (!admitted) {
            RollbackWrite(session, operationId);
        }
        throw;
    }
    if (session.retries.size() > RETRY_LIMIT) {
        session.retries.pop_front();
    }
    session.touched = time;
    return Accepted(operationId, identity.owner);
}

void HttpApi::RollbackWrite(HttpSession &session, const std::string &operationId) noexcept
{
    records_.erase(operationId);
    if (!session.retries.empty() && session.retries.back().operationId == operationId) {
        session.retries.pop_back();
    }
}

}  // 命名空间 VnaWeb
