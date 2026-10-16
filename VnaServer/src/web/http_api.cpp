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
#include "vna_core/sweep_plan.h"
#include "vna_measurement/measurement_processor.h"
#include "vna_service/instrument_profile.h"
#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"
#include "web/calibration_json.h"
#include "web/calibration_preview_json.h"
#include "web/display_json.h"
#include "web/frame_array_json.h"
#include "web/frame_selection.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"
#include "web/stimulus_zoom_json.h"
#include "web/trace_action_adapter.h"

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
// 大校准文件也保存精确幂等指纹；总字节上限避免会话数乘上传大小放大常驻内存。
constexpr std::size_t RETRY_BYTE_LIMIT = 64U * 1024U * 1024U;
constexpr std::size_t OPERATION_LIMIT = 128U;
constexpr std::size_t NAME_LENGTH_LIMIT = 128U;
// 即使请求128条迹线，也不同时保留全部完整频轴的复数结果。
constexpr std::size_t CALCULATION_BATCH_SIZE = 4U;
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

Json MeasurementJson(const VnaService::Measurement &measurement, const VnaCalibration::CalibrationPtr &calibration = {})
{
    Json value = Json::MakeObject();
    value["name"] = Json(measurement.name);
    value["channelId"] = Json(measurement.channelId);
    value["receiverKind"] = Json(std::string(1, measurement.receiverKind));
    value["receiverPort"] = Json(measurement.receiverPort);
    value["sourcePort"] = Json(measurement.sourcePort);
    value["parameter"] = Json(measurement.Parameter());
    const bool ratio = measurement.kind == VnaService::MeasurementKind::S_PARAMETER;
    value["kind"] = Json(ratio ? "sParameter" : "receiver");
    value["unit"] = Json(ratio ? "1" : "a.u.");
    const bool corrected =
        ratio && calibration && calibration->Covers(measurement.receiverPort, measurement.sourcePort);
    value["calibration"] = Json(!ratio ? "raw" : corrected ? "calibrated" : "uncalibrated");
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

std::vector<VnaService::Measurement> FrameMeasurements(
    const VnaService::InstrumentSnapshot &snapshot, const std::string &query)
{
    if (!query.empty()) {
        return SelectFrameMeasurements(query, snapshot.settings.measurements);
    }
    std::vector<VnaService::Measurement> result;
    for (const auto &name : snapshot.display.displayedNames) {
        const auto found = std::find_if(snapshot.settings.measurements.begin(), snapshot.settings.measurements.end(),
            [&name](const VnaService::Measurement &measurement) {
                return measurement.name == name;
            });
        if (found != snapshot.settings.measurements.end()) {
            result.push_back(*found);
        }
    }
    return result;
}

void AppendFrameAxis(
    Json &value, const VnaCore::MeasurementFrame &frame, const std::vector<std::size_t> &indexes, bool packed)
{
    value["frequencyHz"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&frame, &indexes](std::size_t position) {
            return FrameArrayNumber{frame.plan.frequencyHz[indexes[position]], true};
        },
        packed);
    value["sampleIndexes"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&indexes](std::size_t position) {
            return FrameArrayNumber{static_cast<double>(indexes[position]), true};
        },
        packed);
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
            // 活动控制者可持续续租；操作过期时释放大正文，保留请求ID用于返回410。
            const auto session = sessions_.find(record->second.owner);
            if (session != sessions_.end()) {
                for (auto &retry : session->second.retries) {
                    if (retry.operationId == record->first) {
                        std::string().swap(retry.fingerprint);
                    }
                }
            }
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
    value["backend"] = Json(service_.IsSimulator() ? "NativeSimulator" : "HardwareDriver");
    const auto &profile = service_.Profile();
    Json capabilities = Json::MakeObject();
    capabilities["portCount"] = Json(profile.portCount);
    capabilities["minFrequencyHz"] = Json(profile.minFrequencyHz);
    capabilities["maxFrequencyHz"] = Json(profile.maxFrequencyHz);
    capabilities["minSweepPoints"] = Json(VnaService::MIN_SWEEP_POINTS);
    capabilities["maxSweepPoints"] = Json(VnaService::MAX_SWEEP_POINTS);
    capabilities["minPowerDbm"] = Json(VnaCore::MIN_POWER_DBM);
    capabilities["maxPowerDbm"] = Json(VnaCore::MAX_POWER_DBM);
    value["capabilities"] = std::move(capabilities);
    value["state"] = Json(CoreStateName(snapshot.core.state));
    value["error"] = Json(snapshot.core.error);
    value["pending"] = Json(snapshot.pending);
    value["revision"] = Json(std::to_string(settings.revision));
    value["acquisitionRevision"] = Json(std::to_string(settings.acquisitionRevision));
    value["runId"] = Json(std::to_string(snapshot.core.runId));
    value["activeConfigValid"] =
        Json(snapshot.core.state != VnaCore::CoreState::FAULTED && snapshot.core.state != VnaCore::CoreState::CLOSED &&
             snapshot.core.configRevision != 0U && snapshot.core.configRevision == settings.acquisitionRevision);
    value["config"] = ConfigurationJson(settings);
    value["calibration"] = CalibrationJson::Snapshot(snapshot.calibration);
    Json measurements = Json::MakeArray();
    for (const auto &measurement : settings.measurements) {
        const auto channel =
            std::find_if(snapshot.channels.begin(), snapshot.channels.end(), [&measurement](const auto &item) {
                return item.id == measurement.channelId;
            });
        measurements.array.push_back(MeasurementJson(
            measurement, channel == snapshot.channels.end() ? VnaCalibration::CalibrationPtr{} : channel->correction));
    }
    value["measurements"] = std::move(measurements);
    value["display"] = DisplayJson::State(snapshot.display, settings.measurements);
    value["stimulusZoom"] = StimulusZoomJson::Status(snapshot.stimulusZoom);
    value["activeChannelId"] = Json(snapshot.activeChannelId);
    auto channels = Json::MakeArray();
    for (const auto &channel : snapshot.channels) {
        auto item = Json::MakeObject();
        item["channelId"] = Json(channel.id);
        item["name"] = Json(channel.name);
        item["state"] = Json(CoreStateName(channel.core.state));
        item["error"] = Json(channel.core.error);
        item["acquisitionRevision"] = Json(std::to_string(channel.settings.acquisitionRevision));
        item["runId"] = Json(std::to_string(channel.core.runId));
        item["activeConfigValid"] = Json(
            channel.core.state != VnaCore::CoreState::FAULTED && channel.core.state != VnaCore::CoreState::CLOSED &&
            channel.core.configRevision != 0U && channel.core.configRevision == channel.settings.acquisitionRevision);
        item["config"] = ConfigurationJson(channel.settings);
        item["calibration"] = CalibrationJson::Snapshot(channel.calibration);
        item["stimulusZoom"] = StimulusZoomJson::Status(channel.stimulusZoom);
        channels.array.push_back(std::move(item));
    }
    value["channels"] = std::move(channels);
    value["control"] = ControlJson(snapshot, request);
    return response;
}

HttpResponse HttpApi::Frame(const std::string &query) const
{
    const auto snapshot = service_.Snapshot();
    const auto options = ParseFrameQuery(query);
    const auto selected = FrameMeasurements(snapshot, options.selection);
    unsigned target = options.channelId == 0U ? snapshot.activeChannelId : options.channelId;
    if (options.channelId == 0U && !options.selection.empty() && !selected.empty() &&
        std::all_of(selected.begin(), selected.end(), [&selected](const VnaService::Measurement &measurement) {
            return measurement.channelId == selected.front().channelId;
        })) {
        target = selected.front().channelId;
    }
    const auto channel = std::find_if(snapshot.channels.begin(), snapshot.channels.end(), [target](const auto &item) {
        return item.id == target;
    });
    if (channel == snapshot.channels.end()) {
        throw std::invalid_argument("Frame channel does not exist");
    }
    if (options.channelId != 0U && !options.selection.empty() &&
        std::any_of(selected.begin(), selected.end(), [target](const VnaService::Measurement &measurement) {
            return measurement.channelId != target;
        })) {
        throw std::invalid_argument("Trace does not belong to the requested channel");
    }
    auto response = ChannelFrame(snapshot, *channel, selected, options.packed);
    if (response.status != 200U || options.channelId != 0U || snapshot.channels.size() <= 1U) {
        return response;
    }
    auto frames = Json::MakeArray();
    for (const auto &item : snapshot.channels) {
        auto encoded = ChannelFrame(snapshot, item, selected, options.packed);
        if (encoded.status != 200U) {
            return encoded;
        }
        frames.array.push_back(std::move(encoded.json));
    }
    response.json["channelFrames"] = std::move(frames);
    return response;
}

HttpResponse HttpApi::ChannelFrame(const VnaService::InstrumentSnapshot &snapshot,
    const VnaService::InstrumentChannelSnapshot &channel, const std::vector<VnaService::Measurement> &selection,
    bool packed)
{
    std::vector<VnaService::Measurement> selected;
    for (const auto &measurement : selection) {
        if (measurement.channelId == channel.id) {
            selected.push_back(measurement);
        }
    }
    HttpResponse response;
    Json &value = response.json = Json::MakeObject();
    value["channelId"] = Json(channel.id);
    value["displayRevision"] = Json(std::to_string(snapshot.display.revision));
    value["calibrationRevision"] = Json(std::to_string(channel.calibration.revision));
    if (!channel.core.frame) {
        value["available"] = Json(false);
        value["current"] = Json(false);
        value["reason"] = Json("No completed frame");
        return response;
    }
    const auto &frame = *channel.core.frame;
    const std::size_t count = frame.plan.frequencyHz.size();
    if (count == 0U) {
        return HttpError(409, "no_physical_axis", "Frame does not have a verified physical frequency axis");
    }
    if (count > VnaService::MAX_SWEEP_POINTS || selected.size() > VnaDisplay::MAX_DISPLAY_TRACES) {
        return HttpError(409, "frame_capacity_exceeded", "Frame exceeds the supported full-sweep display capacity");
    }
    std::vector<std::size_t> indexes;
    indexes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        indexes.push_back(index);
    }
    value["available"] = Json(true);
    value["current"] = Json(!channel.finitePending && channel.core.state != VnaCore::CoreState::FAULTED &&
                            channel.core.state != VnaCore::CoreState::CLOSED && frame.channelId == channel.id &&
                            frame.configRevision == channel.settings.acquisitionRevision &&
                            frame.configRevision == channel.core.configRevision && frame.runId == channel.core.runId);
    value["channelId"] = Json(frame.channelId);
    value["runId"] = Json(std::to_string(frame.runId));
    value["revision"] = Json(std::to_string(frame.configRevision));
    value["measurementRevision"] = Json(std::to_string(snapshot.settings.revision));
    value["frameSeq"] = Json(std::to_string(frame.frameSeq));
    value["originalPointCount"] = Json(static_cast<unsigned>(count));
    value["displayPointCount"] = Json(static_cast<unsigned>(count));
    value["decimated"] = Json(false);
    AppendFrameAxis(value, frame, indexes, packed);
    Json traces = Json::MakeArray();
    for (std::size_t begin = 0U; begin < selected.size(); begin += CALCULATION_BATCH_SIZE) {
        const auto end = (std::min)(selected.size(), begin + CALCULATION_BATCH_SIZE);
        const std::vector<VnaService::Measurement> batch(selected.begin() + begin, selected.begin() + end);
        const auto results =
            VnaMeasurement::MeasurementProcessor::CalculateMany(frame, batch, channel.correction.get());
        for (std::size_t offset = 0U; offset < batch.size(); ++offset) {
            const auto &measurement = batch[offset];
            const auto presentation = std::find_if(snapshot.display.traces.begin(), snapshot.display.traces.end(),
                [&measurement](const VnaDisplay::TracePresentation &item) {
                    return item.name == measurement.name;
                });
            if (presentation != snapshot.display.traces.end()) {
                traces.array.push_back(TraceJson(frame, measurement, *presentation, indexes, results[offset], packed));
            }
        }
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
    const auto outputLimit =
        request.path == "/api/v1/calibration" ? CALIBRATION_JSON_BYTE_LIMIT : JsonEncoder::DEFAULT_OUTPUT_LIMIT;
    const std::string fingerprint = request.method + '\n' + request.path + '\n' + StringifyJson(body, outputLimit);
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
    if ((request.path == "/api/v1/display" || request.path == "/api/v1/sweep-control" ||
            request.path == "/api/v1/channels" || request.path == "/api/v1/stimulus-zoom" ||
            request.path.find("/api/v1/measurement-actions/") == 0U) &&
        request.method != "POST") {
        return HttpError(405, "method_not_allowed", "Instrument display actions require POST");
    }
    if (request.method == "GET") {
        if (request.path == "/api/v1/status") {
            return Status(request);
        }
        if (request.path == "/api/v1/frame") {
            return Frame(request.query);
        }
        if (request.path == "/api/v1/calibration") {
            std::string remaining;
            const auto channelId = QueryChannel(request.query, remaining);
            if (!remaining.empty()) {
                throw std::invalid_argument("Unknown calibration query field");
            }
            HttpResponse response;
            response.json = CalibrationJson::Snapshot(service_.Calibration(channelId));
            return response;
        }
        if (request.path == "/api/v1/calibration/preview") {
            std::string remaining;
            const auto channelId = QueryChannel(request.query, remaining);
            const auto query = CalibrationPreviewJson::ParseQuery(remaining);
            HttpResponse response;
            response.json =
                CalibrationPreviewJson::Encode(service_.CalibrationPreview(query.sessionId, query.stepId, channelId));
            return response;
        }
        const std::string calibrationPrefix = "/api/v1/calibration/sets/";
        if (request.path.find(calibrationPrefix) == 0U) {
            std::string remaining;
            const auto channelId = QueryChannel(request.query, remaining);
            return CalibrationSetFile(request.path.substr(calibrationPrefix.size()), remaining, channelId);
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
            request.path == "/api/v1/control/release" || request.path == "/api/v1/calibration")) {
        return HttpError(405, "method_not_allowed", "This API route requires POST");
    }
    if (request.method != "DELETE" && request.method != "PUT" && request.path.find("/api/v1/measurements/") == 0U) {
        return HttpError(405, "method_not_allowed", "Measurement update or deletion requires PUT or DELETE");
    }
    const unsigned nodeLimit = request.path == "/api/v1/calibration" ? CALIBRATION_JSON_NODE_LIMIT : 4096U;
    Json body = request.body.empty() ? Json::MakeObject() : ParseJson(request.body, nodeLimit);
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
    config["powerDbm"] = Json(settings.powerDbm);
    config["triggerSource"] = Json(settings.triggerSource == VnaService::TriggerSource::IMMEDIATE ? "IMM" : "MAN");
    config["sweepMode"] = Json(settings.sweepMode == VnaService::SweepMode::HOLD     ? "HOLD"
                               : settings.sweepMode == VnaService::SweepMode::SINGLE ? "SING"
                                                                                     : "CONT");
    config["selectedSweepMode"] = Json(settings.selectedSweepMode == VnaService::SweepMode::SINGLE ? "SING" : "CONT");
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
    const VnaDisplay::TracePresentation &presentation, const std::vector<std::size_t> &indexes,
    const VnaMeasurement::TraceResult &result, bool packed)
{
    Json trace = MeasurementJson(measurement);
    if (measurement.kind == VnaService::MeasurementKind::S_PARAMETER) {
        trace["calibration"] = Json(result.calibrated ? "calibrated" : "uncalibrated");
    }
    const auto validAt = [&result, &indexes](std::size_t position) {
        const auto index = indexes[position];
        return result.available && index < result.points.size() && result.points[index].valid;
    };
    trace["available"] = Json(result.available);
    trace["real"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&result, &indexes, &validAt](std::size_t position) {
            const bool valid = validAt(position);
            return FrameArrayNumber{valid ? result.points[indexes[position]].real : 0.0, valid};
        },
        packed);
    trace["imag"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&result, &indexes, &validAt](std::size_t position) {
            const bool valid = validAt(position);
            return FrameArrayNumber{valid ? result.points[indexes[position]].imag : 0.0, valid};
        },
        packed);
    trace["valid"] = FrameArrayJson::Booleans(indexes.size(), validAt, packed);
    const auto projection = VnaDisplay::TraceProjection::Calculate(
        presentation, result, frame.plan.frequencyHz, measurement, frame.configRevision);
    trace["projection"] = DisplayJson::Projection(projection, presentation.format, indexes, packed);
    return trace;
}

HttpApi::HttpWriteResult HttpApi::StartWrite(const HttpRequest &request, const Json &body, const std::string &id)
{
    HttpWriteResult result;
    auto &operationValue = result.operation;
    if (request.path == "/api/v1/config" && request.method == "POST") {
        return StartConfigurationWrite(body, id);
    } else if (request.path == "/api/v1/channels" && request.method == "POST") {
        return StartChannelWrite(body, id);
    } else if (request.path == "/api/v1/calibration" && request.method == "POST") {
        const auto channelId = Channel(body);
        const auto expected = Revision(body);
        const auto change = CalibrationJson::Change(body);
        const auto snapshot = service_.Calibration(channelId);
        const bool cancelCurrent = change.kind == VnaService::CalibrationChangeKind::CANCEL &&
                                   change.sessionId == snapshot.sessionId && snapshot.collecting &&
                                   expected <= snapshot.revision;
        if (expected != snapshot.revision && !cancelCurrent) {
            result.response = HttpError(409, "revision_conflict", "Calibration revision changed");
            return result;
        }
        result.operation = service_.ApplyCalibration(change, expected, id, channelId);
    } else if (request.path == "/api/v1/display" && request.method == "POST") {
        return StartDisplayWrite(body, id);
    } else if (request.path == "/api/v1/stimulus-zoom" && request.method == "POST") {
        return StartStimulusZoomWrite(body, id);
    } else if ((request.path == "/api/v1/measurement-actions/clone" ||
                   request.path == "/api/v1/measurement-actions/add" ||
                   request.path == "/api/v1/measurement-actions/all-sparameters" ||
                   request.path == "/api/v1/measurement-actions/delete-diagram") &&
               request.method == "POST") {
        operationValue = TraceActionAdapter::Execute(service_, request.path, body, id);
    } else if (request.path == "/api/v1/operations" && request.method == "POST") {
        Allowed(body, {"requestId", "action", "channelId"});
        operationValue = StartAction(body, id);
    } else if (request.path == "/api/v1/sweep-control" && request.method == "POST") {
        operationValue = StartSweepControl(body, id);
    } else if (request.path == "/api/v1/measurements" && request.method == "POST") {
        return StartDefinitionWrite(body, id);
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

HttpApi::HttpWriteResult HttpApi::StartConfigurationWrite(const Json &body, const std::string &id)
{
    Allowed(body, {"requestId", "expectedRevision", "startHz", "stopHz", "centerHz", "spanHz", "points",
                      "ifBandwidthHz", "powerDbm", "channelId"});
    const auto channelId = Channel(body);
    HttpWriteResult result;
    const auto expected = Revision(body);
    if (expected != service_.Settings().revision) {
        result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
        return result;
    }
    result.operation = service_.ApplyConfiguration(ConfigurationPatch(body), expected, id, channelId);
    result.admitted = true;
    return result;
}

unsigned HttpApi::Channel(const Json &body, bool required) const
{
    if (body.Find("channelId") == nullptr && !required) {
        return service_.ActiveChannelId();
    }
    const auto channelId = PositiveInteger(body, "channelId", (std::numeric_limits<unsigned>::max)());
    if (!service_.HasChannel(channelId)) {
        throw std::invalid_argument("Channel does not exist");
    }
    return channelId;
}

unsigned HttpApi::QueryChannel(const std::string &query, std::string &remaining) const
{
    unsigned channelId = service_.ActiveChannelId();
    bool found = false;
    remaining.clear();
    for (std::size_t begin = 0U; begin < query.size();) {
        const auto end = query.find('&', begin);
        const auto field = query.substr(begin, end == std::string::npos ? end : end - begin);
        if (field.empty()) {
            throw std::invalid_argument("Empty calibration query field");
        }
        if (field.find("channelId=") == 0U) {
            if (found) {
                throw std::invalid_argument("Duplicate channelId query");
            }
            found = true;
            channelId = ParseFrameQuery(field).channelId;
        } else {
            remaining += (remaining.empty() ? "" : "&") + field;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
        if (begin == query.size()) {
            throw std::invalid_argument("Empty calibration query field");
        }
    }
    if (!service_.HasChannel(channelId)) {
        throw std::invalid_argument("Channel does not exist");
    }
    return channelId;
}

HttpApi::HttpWriteResult HttpApi::StartChannelWrite(const Json &body, const std::string &id)
{
    const auto action = Text(body, "action");
    Allowed(body, action == "new" || action == "copy" ? std::vector<std::string>{"requestId", "action", "channelId",
                                                            "newDiagram", "expectedRevision", "expectedDisplayRevision"}
                                                      : std::vector<std::string>{"requestId", "action", "channelId",
                                                            "expectedRevision", "expectedDisplayRevision"});
    HttpWriteResult result;
    const auto expected = Revision(body);
    const auto expectedDisplay = DisplayJson::Revision(body, "expectedDisplayRevision");
    if (expected != service_.Settings().revision || expectedDisplay != service_.Display().revision) {
        result.response = HttpError(409, "revision_conflict", "Channel configuration or display revision changed");
        return result;
    }
    if (action == "new" || action == "copy") {
        if (action == "new" && body.Find("channelId") != nullptr) {
            throw std::invalid_argument("New channel numbers are assigned by the instrument");
        }
        VnaService::ChannelCreationOptions options;
        options.copyChannel = action == "copy";
        options.sourceChannelId = options.copyChannel ? Channel(body) : 0U;
        if (const auto *value = body.Find("newDiagram")) {
            if (value->type != Json::Type::BOOLEAN) {
                throw std::invalid_argument("newDiagram must be a boolean");
            }
            options.newDiagram = value->boolean;
        }
        result.operation = service_.CreateChannel(options, expected, expectedDisplay, id);
    } else if (action == "select") {
        result.operation = service_.SelectChannel(Channel(body, true), expectedDisplay, id);
    } else if (action == "delete") {
        result.operation = service_.DeleteChannel(Channel(body, true), expected, expectedDisplay, id);
    } else {
        throw std::invalid_argument("Unknown channel action");
    }
    result.admitted = true;
    return result;
}

HttpApi::HttpWriteResult HttpApi::StartDefinitionWrite(const Json &body, const std::string &id)
{
    Allowed(body, {"requestId", "expectedRevision", "name", "parameter", "kind", "receiverKind", "receiverPort",
                      "sourcePort", "channelId"});
    HttpWriteResult result;
    const auto expected = Revision(body);
    if (expected != service_.Settings().revision) {
        result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
        return result;
    }
    result.operation = service_.DefineMeasurement(MeasurementDefinition(body), expected, id, Channel(body));
    result.admitted = true;
    return result;
}

HttpApi::HttpWriteResult HttpApi::StartMeasurementWrite(
    const HttpRequest &request, const Json &body, const std::string &id)
{
    HttpWriteResult result;
    const bool update = request.method == "PUT";
    Allowed(body, update ? std::vector<std::string>{"requestId", "expectedRevision", "parameter", "kind",
                               "receiverKind", "receiverPort", "sourcePort", "channelId"}
                         : std::vector<std::string>{"requestId", "expectedRevision", "channelId"});
    const auto expected = Revision(body);
    if (expected != service_.Settings().revision) {
        result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
        return result;
    }
    const std::string name = request.path.substr(std::string("/api/v1/measurements/").size());
    if (!SafeName(name)) {
        throw std::runtime_error("Invalid measurement name");
    }
    if (body.Find("channelId") != nullptr && service_.MeasurementChannel(name) != Channel(body)) {
        result.response =
            HttpError(409, "channel_owner_conflict", "Measurement does not belong to the requested channel");
        return result;
    }
    if (update) {
        if (!body.Find("parameter")) {
            Text(body, "kind");
        }
        // 名称只取自路径，正文不能把更新变成重命名或新增测量。
        Json definition = body;
        definition["name"] = Json(name);
        result.operation = service_.UpdateMeasurement(
            MeasurementDefinition(definition), expected, id, body.Find("channelId") == nullptr ? 0U : Channel(body));
    } else {
        result.operation =
            service_.DeleteMeasurement(name, expected, id, body.Find("channelId") == nullptr ? 0U : Channel(body));
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
    if (body.Find("centerHz")) {
        patch.hasCenterHz = true;
        patch.centerHz = Numeric(body, "centerHz");
    }
    if (body.Find("spanHz")) {
        patch.hasSpanHz = true;
        patch.spanHz = Numeric(body, "spanHz");
    }
    if (body.Find("points")) {
        patch.hasPoints = true;
        patch.points = PositiveInteger(body, "points", SWEEP_POINT_LIMIT);
    }
    if (body.Find("ifBandwidthHz")) {
        patch.hasIfBandwidthHz = true;
        patch.ifBandwidthHz = Numeric(body, "ifBandwidthHz");
    }
    if (body.Find("powerDbm")) {
        patch.hasPowerDbm = true;
        patch.powerDbm = Numeric(body, "powerDbm");
    }
    return patch;
}

VnaService::Measurement HttpApi::MeasurementDefinition(const Json &body)
{
    VnaService::Measurement measurement;
    measurement.name = Text(body, "name");
    if (body.Find("parameter")) {
        if (body.Find("kind") || body.Find("receiverKind") || body.Find("receiverPort") || body.Find("sourcePort") ||
            !SafeName(measurement.name) || !measurement.SetParameter(Text(body, "parameter"))) {
            throw std::runtime_error("Parameter must be supported and cannot be mixed with receiver fields");
        }
        return measurement;
    }
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

HttpApi::HttpWriteResult HttpApi::StartDisplayWrite(const Json &body, const std::string &id)
{
    HttpWriteResult result;
    const auto expected = DisplayJson::Revision(body, "expectedDisplayRevision");
    if (expected != service_.Display().revision) {
        result.response = HttpError(409, "display_revision_conflict", "Display revision changed");
        return result;
    }
    result.operation = service_.ApplyDisplay(DisplayJson::Change(body), expected, id);
    result.admitted = true;
    return result;
}

HttpApi::HttpWriteResult HttpApi::StartStimulusZoomWrite(const Json &body, const std::string &id)
{
    HttpWriteResult result;
    const auto change = StimulusZoomJson::Change(body);
    const auto expected = DisplayJson::Revision(body, "expectedRevision");
    const auto expectedDisplay = DisplayJson::Revision(body, "expectedDisplayRevision");
    const auto channelId = body.Find("channelId") != nullptr || change.traceName.empty()
                               ? Channel(body)
                               : service_.MeasurementChannel(change.traceName);
    if (channelId == 0U) {
        throw std::invalid_argument("Stimulus zoom trace does not exist");
    }
    if (!change.traceName.empty() && service_.MeasurementChannel(change.traceName) != channelId) {
        result.response =
            HttpError(409, "channel_owner_conflict", "Stimulus zoom trace does not belong to the requested channel");
        return result;
    }
    const auto snapshot = service_.Snapshot(channelId);
    if (expected != snapshot.settings.revision) {
        result.response = HttpError(409, "revision_conflict", "Configuration revision changed");
        return result;
    }
    if (expectedDisplay != snapshot.display.revision) {
        result.response = HttpError(409, "display_revision_conflict", "Display revision changed");
        return result;
    }
    result.operation = service_.ApplyStimulusZoom(change, expected, expectedDisplay, id, channelId);
    result.admitted = true;
    return result;
}

VnaService::Operation HttpApi::StartAction(const Json &body, const std::string &id)
{
    const std::string action = Text(body, "action");
    const auto channelId = Channel(body);
    if (action == "single") {
        return service_.RunSingle(id, channelId);
    } else if (action == "continuous") {
        return service_.RunContinuous(id, channelId);
    } else if (action == "hold") {
        return service_.SetSweepMode(VnaService::SweepMode::HOLD, id, channelId);
    } else if (action == "trigger") {
        return service_.Trigger(id, channelId);
    } else {
        throw std::runtime_error("Unknown operation action");
    }
}

VnaService::Operation HttpApi::StartSweepControl(const Json &body, const std::string &id)
{
    const auto action = Text(body, "action");
    const auto channelId = Channel(body);
    if (action == "mode") {
        Allowed(body, {"requestId", "action", "mode", "channelId"});
        const auto mode = Text(body, "mode");
        if (mode != "CONT" && mode != "SING") {
            throw std::runtime_error("Sweep mode must be CONT or SING");
        }
        return service_.SelectSweepMode(
            mode == "CONT" ? VnaService::SweepMode::CONTINUOUS : VnaService::SweepMode::SINGLE, id, channelId);
    }
    if (action == "source") {
        Allowed(body, {"requestId", "action", "source", "channelId"});
        const auto source = Text(body, "source");
        if (source != "IMM" && source != "MAN") {
            throw std::runtime_error("Trigger source must be IMM or MAN");
        }
        return service_.SelectTriggerSource(
            source == "IMM" ? VnaService::TriggerSource::IMMEDIATE : VnaService::TriggerSource::MANUAL, id, channelId);
    }
    Allowed(body, {"requestId", "action", "channelId"});
    if (action == "start") {
        return service_.StartSweep(id, channelId);
    }
    if (action == "stop") {
        return service_.StopSweep(id, channelId);
    }
    if (action == "trigger") {
        return service_.Trigger(id, channelId);
    }
    if (action == "single") {
        return service_.RunSelectedSingle(id, channelId);
    }
    throw std::runtime_error("Unknown sweep control action");
}

bool HttpApi::RetryResponse(const HttpSession &session, const HttpWriteIdentity &request, HttpResponse &response) const
{
    for (const HttpRetry &retry : session.retries) {
        if (retry.requestId != request.requestId) {
            continue;
        }
        if (records_.count(retry.operationId) == 0U) {
            response = HttpError(410, "operation_expired", "Original operation record has expired");
            return true;
        }
        if (retry.fingerprint != request.fingerprint) {
            response = HttpError(409, "request_id_conflict", "requestId was used with different content");
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
    if (!RetryBudgetAllows(session, identity.fingerprint.size())) {
        return HttpError(
            429, "retry_history_full", "Retained request bytes exceed budget; retry after history expires");
    }
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

bool HttpApi::RetryBudgetAllows(const HttpSession &session, std::size_t addedBytes) const noexcept
{
    std::size_t retainedBytes = 0U;
    for (const auto &entry : sessions_) {
        for (const auto &retry : entry.second.retries) {
            retainedBytes += retry.fingerprint.size();
        }
    }
    if (session.retries.size() >= RETRY_LIMIT) {
        retainedBytes -= session.retries.front().fingerprint.size();
    }
    return retainedBytes <= RETRY_BYTE_LIMIT && addedBytes <= RETRY_BYTE_LIMIT - retainedBytes;
}

void HttpApi::RollbackWrite(HttpSession &session, const std::string &operationId) noexcept
{
    records_.erase(operationId);
    if (!session.retries.empty() && session.retries.back().operationId == operationId) {
        session.retries.pop_back();
    }
}

}  // 命名空间 VnaWeb
