#include "web/HttpApi.h"

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

#include "vna_service/InstrumentService.h"
#include "web/JsonEncoder.h"
#include "web/JsonParser.h"

namespace vna_web {
HttpResponse httpError(unsigned status, const std::string &code, const std::string &message)
{
    HttpResponse response;
    response.status = status;
    response.json = Json::makeObject();
    Json error = Json::makeObject();
    error["code"] = Json(code);
    error["message"] = Json(message);
    response.json["error"] = std::move(error);
    return response;
}

namespace {
std::string text(const Json &value, const std::string &key, bool optional = false)
{
    const Json *item = value.find(key);
    if (item == nullptr && optional) {
        return std::string();
    }
    if (item == nullptr || item->type != Json::Type::String) {
        throw std::runtime_error(key + " must be a string");
    }
    return item->string;
}

double numeric(const Json &value, const std::string &key)
{
    const Json *item = value.find(key);
    if (item == nullptr || item->type != Json::Type::Number || !std::isfinite(item->number)) {
        throw std::runtime_error(key + " must be finite numeric data");
    }
    return item->number;
}

unsigned positiveInteger(const Json &value, const std::string &key, unsigned maximum)
{
    const double number = numeric(value, key);
    if (number < 1 || number > maximum || std::floor(number) != number) {
        throw std::runtime_error(key + " out of range");
    }
    return static_cast<unsigned>(number);
}

std::uint64_t revision(const Json &value)
{
    const std::string string = text(value, "expectedRevision");
    if (string.empty()) {
        throw std::runtime_error("expectedRevision is empty");
    }
    std::uint64_t result = 0;
    for (char digit : string) {
        if (digit < '0' || digit > '9' ||
            result > ((std::numeric_limits<std::uint64_t>::max)() - (digit - '0')) / 10U) {
            throw std::runtime_error("expectedRevision must be a decimal uint64 string");
        }
        result = result * 10U + static_cast<unsigned>(digit - '0');
    }
    return result;
}

void allowed(const Json &value, const std::vector<std::string> &keys)
{
    if (value.type != Json::Type::Object) {
        throw std::runtime_error("JSON body must be an object");
    }
    for (const auto &item : value.object) {
        if (std::find(keys.begin(), keys.end(), item.first) == keys.end()) {
            throw std::runtime_error("Unknown member: " + item.first);
        }
    }
}

bool ready(const vna_service::Operation &value)
{
    return value.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string coreState(vna_core::CoreState value)
{
    switch (value) {
        case vna_core::CoreState::Uninitialized:
            return "Uninitialized";
        case vna_core::CoreState::Ready:
            return "Ready";
        case vna_core::CoreState::Running:
            return "Running";
        case vna_core::CoreState::Faulted:
            return "Faulted";
        case vna_core::CoreState::Closed:
            return "Closed";
    }
    return "Uninitialized";
}

Json measurementJson(const vna_service::Measurement &measurement)
{
    Json value = Json::makeObject();
    value["name"] = Json(measurement.name);
    value["receiverKind"] = Json(std::string(1, measurement.receiverKind));
    value["receiverPort"] = Json(measurement.receiverPort);
    value["sourcePort"] = Json(measurement.sourcePort);
    value["parameter"] = Json(measurement.parameter());
    return value;
}

bool safeName(const std::string &value)
{
    if (value.empty() || value.size() > 128U || value == "." || value == "..") {
        return false;
    }
    for (unsigned char character : value) {
        if (character < 32U || character == '/' || character == '\\') {
            return false;
        }
    }
    return true;
}
}  // namespace

class HttpApi::Impl {
public:
    struct Retry {
        std::string requestId, fingerprint, operationId;
    };

    struct Session {
        Clock::time_point expires, touched;
        bool releasing = false;
        std::deque<Retry> retries;
    };

    struct Record {
        vna_service::Operation operation;
        std::string owner;
        Clock::time_point created, completed;
        bool done = false;
        vna_service::OperationResult result;
    };

    Impl(vna_service::InstrumentService &value, Now clock) : service(value), now(std::move(clock))
    {
    }

    vna_service::InstrumentService &service;
    Now now;
    std::map<std::string, Session> sessions;
    std::map<std::string, Record> records;
    std::uint64_t sequence = 0;
    bool shuttingDown = false;
    std::random_device random;

    std::string token()
    {
        std::ostringstream value;
        value << "web-" << std::hex << std::setfill('0');
        for (unsigned index = 0; index < 4U; ++index) {
            value << std::setw(8) << static_cast<std::uint32_t>(random());
        }
        return value.str();
    }

    std::string headerToken(const HttpRequest &request) const
    {
        const auto found = request.headers.find("x-vna-session");
        return found == request.headers.end() ? std::string() : found->second;
    }

    void poll()
    {
        service.poll();
        const auto time = now();
        for (auto &item : records) {
            Record &record = item.second;
            if (!record.done && ready(record.operation)) {
                record.done = true;
                record.result = record.operation.get();
                record.completed = time;
            }
        }
        for (auto record = records.begin(); record != records.end();) {
            if (record->second.done && time - record->second.completed >= std::chrono::seconds(60)) {
                record = records.erase(record);
            } else {
                ++record;
            }
        }
        for (auto &item : sessions) {
            Session &session = item.second;
            if (service.controlOwner() == item.first &&
                (shuttingDown || time >= session.expires || session.releasing)) {
                session.releasing = true;
                service.beginReleaseControl(item.first);
                if (!service.hasPending()) {
                    service.releaseControl(item.first);
                }
            }
        }
        for (auto session = sessions.begin(); session != sessions.end();) {
            if (service.controlOwner() != session->first &&
                time - session->second.touched >= std::chrono::seconds(60)) {
                session = sessions.erase(session);
            } else {
                ++session;
            }
        }
    }

    HttpResponse status(const HttpRequest &request)
    {
        const auto snapshot = service.snapshot();
        const auto &settings = snapshot.settings;
        HttpResponse response;
        Json &value = response.json = Json::makeObject();
        value["identity"] = Json(service.identity());
        value["backend"] = Json("NativeSimulator");
        value["state"] = Json(coreState(snapshot.core.state));
        value["error"] = Json(snapshot.core.error);
        value["pending"] = Json(snapshot.pending);
        value["revision"] = Json(std::to_string(settings.revision));
        value["runId"] = Json(std::to_string(snapshot.core.runId));
        value["activeConfigValid"] = Json(
            snapshot.core.state != vna_core::CoreState::Faulted && snapshot.core.state != vna_core::CoreState::Closed &&
            snapshot.core.configRevision != 0U && snapshot.core.configRevision == settings.revision);
        Json config = Json::makeObject();
        config["startHz"] = Json(settings.startHz);
        config["stopHz"] = Json(settings.stopHz);
        config["points"] = Json(settings.points);
        config["ifBandwidthHz"] = Json(settings.ifBandwidthHz);
        config["triggerSource"] = Json(settings.triggerSource == vna_service::TriggerSource::Immediate ? "IMM" : "MAN");
        config["sweepMode"] = Json(settings.sweepMode == vna_service::SweepMode::Hold     ? "HOLD"
                                   : settings.sweepMode == vna_service::SweepMode::Single ? "SING"
                                                                                          : "CONT");
        value["config"] = std::move(config);
        Json measurements = Json::makeArray();
        for (const auto &measurement : settings.measurements) {
            measurements.array.push_back(measurementJson(measurement));
        }
        value["measurements"] = std::move(measurements);
        Json control = Json::makeObject();
        const std::string owner = snapshot.controlOwner;
        const std::string requester = headerToken(request);
        const bool mine = !owner.empty() && owner != "SCPI" && owner == requester && !snapshot.releasing;
        control["owner"] = Json(owner.empty() ? "none" : owner == "SCPI" ? "scpi" : "web");
        control["hasControl"] = Json(mine);
        control["releasing"] = Json(snapshot.releasing);
        double remaining = 0;
        const auto session = sessions.find(owner);
        if (session != sessions.end()) {
            remaining =
                (std::max)(0.0, std::chrono::duration<double, std::milli>(session->second.expires - now()).count());
        }
        control["leaseRemainingMs"] = Json(remaining);
        const std::string reason = mine                 ? std::string()
                                   : snapshot.releasing ? "Control is releasing"
                                   : owner == "SCPI"    ? "SCPI controller active"
                                   : owner.empty()      ? "Claim control to write"
                                                        : "Another Web session controls instrument";
        control["readOnlyReason"] = Json(reason);
        value["control"] = std::move(control);
        return response;
    }

    HttpResponse frame()
    {
        const auto snapshot = service.snapshot();
        HttpResponse response;
        Json &value = response.json = Json::makeObject();
        if (!snapshot.core.frame) {
            value["available"] = Json(false);
            value["current"] = Json(false);
            value["reason"] = Json("No completed frame");
            return response;
        }
        const auto &frame = *snapshot.core.frame;
        const std::size_t count = frame.plan.frequencyHz.size();
        if (count == 0U) {
            return httpError(409, "no_physical_axis", "Frame does not have a verified physical frequency axis");
        }
        const unsigned display = static_cast<unsigned>((std::min)(count, static_cast<std::size_t>(2001U)));
        std::vector<std::size_t> indexes;
        for (unsigned index = 0; index < display; ++index) {
            indexes.push_back(display == 1U ? 0U : index * (count - 1U) / (display - 1U));
        }
        value["available"] = Json(true);
        value["current"] = Json(
            !snapshot.finitePending && snapshot.core.state != vna_core::CoreState::Faulted &&
            snapshot.core.state != vna_core::CoreState::Closed && frame.configRevision == snapshot.settings.revision &&
            frame.configRevision == snapshot.core.configRevision && frame.runId == snapshot.core.runId);
        value["channelId"] = Json(frame.channelId);
        value["runId"] = Json(std::to_string(frame.runId));
        value["revision"] = Json(std::to_string(frame.configRevision));
        value["frameSeq"] = Json(std::to_string(frame.frameSeq));
        value["originalPointCount"] = Json(static_cast<unsigned>(count));
        value["displayPointCount"] = Json(display);
        value["decimated"] = Json(count > display);
        Json axis = Json::makeArray();
        for (auto index : indexes) {
            axis.array.emplace_back(frame.plan.frequencyHz[index]);
        }
        value["frequencyHz"] = std::move(axis);
        Json traces = Json::makeArray();
        for (const auto &measurement : snapshot.settings.measurements) {
            if (traces.array.size() == 4U) {
                break;
            }
            Json trace = measurementJson(measurement);
            Json real = Json::makeArray(), imaginary = Json::makeArray(), validity = Json::makeArray();
            const auto port =
                std::find_if(frame.ports.begin(), frame.ports.end(), [&measurement](const vna_core::PortFrame &item) {
                    return item.sourcePort == measurement.sourcePort;
                });
            const bool available = port != frame.ports.end() && port->iq.size() == count * 16U &&
                                   (frame.plan.receiverMask & (1U << (measurement.receiverPort - 1U))) != 0U;
            const unsigned component =
                (measurement.receiverPort - 1U) * 4U + (measurement.receiverKind == 'b' ? 2U : 0U);
            for (auto index : indexes) {
                const double re = available ? port->iq[index * 16U + component] : 0;
                const double im = available ? port->iq[index * 16U + component + 1U] : 0;
                const bool valid = available && std::isfinite(re) && std::isfinite(im);
                real.array.push_back(valid ? Json(re) : Json());
                imaginary.array.push_back(valid ? Json(im) : Json());
                validity.array.emplace_back(valid);
            }
            trace["available"] = Json(available);
            trace["real"] = std::move(real);
            trace["imag"] = std::move(imaginary);
            trace["valid"] = std::move(validity);
            traces.array.push_back(std::move(trace));
        }
        value["traces"] = std::move(traces);
        return response;
    }

    HttpResponse operation(const std::string &id, const std::string &requester)
    {
        const auto found = records.find(id);
        if (found == records.end()) {
            return httpError(404, "operation_not_found", "Operation is unknown or expired");
        }
        if (requester.empty() || requester != found->second.owner) {
            return httpError(403, "not_operation_owner", "Operation result belongs to another session");
        }
        const Record &record = found->second;
        HttpResponse response;
        response.json = Json::makeObject();
        response.json["operationId"] = Json(id);
        response.json["state"] = Json(!record.done ? "pending" : record.result.ok ? "completed" : "failed");
        Json value;
        if (record.done) {
            value = Json::makeObject();
            value["ok"] = Json(record.result.ok);
            value["errorCode"] = Json(static_cast<double>(record.result.errorCode));
            value["message"] = Json(record.result.message);
        }
        response.json["result"] = std::move(value);
        return response;
    }

    HttpResponse accepted(const std::string &id, const std::string &owner)
    {
        HttpResponse response = operation(id, owner);
        response.status = 202;
        response.json.object.erase("result");
        return response;
    }

    HttpResponse claim(const Json &body)
    {
        allowed(body, {"sessionId"});
        const std::string requested = text(body, "sessionId", true);
        if (service.controlReleasing()) {
            return httpError(409, "control_releasing", "Previous controller is still completing accepted operations");
        }
        if (!service.controlOwner().empty() && service.controlOwner() != requested) {
            return httpError(409, "control_busy", "Another controller holds the instrument");
        }
        std::string id = requested;
        if (id.empty()) {
            if (sessions.size() >= 32U) {
                return httpError(429, "session_limit", "Web session budget is full");
            }
            do {
                id = token();
            } while (sessions.count(id) != 0U);
            sessions[id] = Session();
        } else if (sessions.count(id) == 0U) {
            return httpError(403, "invalid_session", "Session is unknown or expired");
        }
        if (service.controlOwner().empty() && !service.tryAcquireControl(id)) {
            return httpError(409, "control_busy", "Instrument control is unavailable");
        }
        Session &session = sessions[id];
        session.releasing = false;
        session.expires = now() + std::chrono::seconds(30);
        session.touched = now();
        HttpResponse response;
        response.json = Json::makeObject();
        response.json["sessionId"] = Json(id);
        response.json["hasControl"] = Json(true);
        response.json["leaseSeconds"] = Json(30U);
        return response;
    }

    HttpResponse release(const std::string &id)
    {
        const auto found = sessions.find(id);
        if (found == sessions.end()) {
            return httpError(403, "invalid_session", "Missing or expired session");
        }
        if (service.controlOwner() != id) {
            return httpError(403, "not_controller", "Session does not hold instrument control");
        }
        found->second.releasing = true;
        found->second.touched = now();
        service.beginReleaseControl(id);
        if (!service.hasPending()) {
            service.releaseControl(id);
        }
        HttpResponse response;
        response.json = Json::makeObject();
        response.json["releasing"] = Json(true);
        return response;
    }

    HttpResponse write(const HttpRequest &request, const Json &body)
    {
        const std::string id = headerToken(request);
        const auto found = sessions.find(id);
        if (found == sessions.end()) {
            return httpError(403, "invalid_session", "X-Vna-Session is missing or expired");
        }
        Session &session = found->second;
        if (service.controlOwner() != id || session.releasing || service.controlReleasing() ||
            now() >= session.expires) {
            return httpError(409, "not_controller", "Session does not have active write control");
        }
        const std::string requestId = text(body, "requestId");
        if (!safeName(requestId)) {
            throw std::runtime_error("requestId must contain 1..128 safe characters");
        }
        const std::string fingerprint = request.method + '\n' + request.path + '\n' + stringifyJson(body);
        for (const Retry &retry : session.retries) {
            if (retry.requestId != requestId) {
                continue;
            }
            if (retry.fingerprint != fingerprint) {
                return httpError(409, "request_id_conflict", "requestId was used with different content");
            }
            if (records.count(retry.operationId) == 0U) {
                return httpError(410, "operation_expired", "Original operation record has expired");
            }
            return accepted(retry.operationId, id);
        }
        if (records.size() >= 128U) {
            return httpError(429, "operation_limit", "Operation record budget is full");
        }
        vna_service::Operation operationValue;
        if (request.path == "/api/v1/config" && request.method == "POST") {
            allowed(body, {"requestId", "expectedRevision", "startHz", "stopHz", "points", "ifBandwidthHz"});
            const auto expected = revision(body);
            if (expected != service.settings().revision) {
                return httpError(409, "revision_conflict", "Configuration revision changed");
            }
            vna_service::ConfigPatch patch;
            if (body.find("startHz")) {
                patch.hasStartHz = true;
                patch.startHz = numeric(body, "startHz");
            }
            if (body.find("stopHz")) {
                patch.hasStopHz = true;
                patch.stopHz = numeric(body, "stopHz");
            }
            if (body.find("points")) {
                patch.hasPoints = true;
                patch.points = positiveInteger(body, "points", 10001U);
            }
            if (body.find("ifBandwidthHz")) {
                patch.hasIfBandwidthHz = true;
                patch.ifBandwidthHz = numeric(body, "ifBandwidthHz");
            }
            operationValue = service.applyConfiguration(patch, expected, id);
        } else if (request.path == "/api/v1/operations" && request.method == "POST") {
            allowed(body, {"requestId", "action"});
            const std::string action = text(body, "action");
            if (action == "single") {
                operationValue = service.runSingle(id);
            } else if (action == "continuous") {
                operationValue = service.runContinuous(id);
            } else if (action == "hold") {
                operationValue = service.setSweepMode(vna_service::SweepMode::Hold, id);
            } else if (action == "trigger") {
                operationValue = service.trigger(id);
            } else {
                throw std::runtime_error("Unknown operation action");
            }
        } else if (request.path == "/api/v1/measurements" && request.method == "POST") {
            allowed(body, {"requestId", "expectedRevision", "name", "receiverKind", "receiverPort", "sourcePort"});
            const auto expected = revision(body);
            if (expected != service.settings().revision) {
                return httpError(409, "revision_conflict", "Configuration revision changed");
            }
            vna_service::Measurement measurement;
            measurement.name = text(body, "name");
            const auto kind = text(body, "receiverKind");
            if (!safeName(measurement.name) || kind.size() != 1U ||
                (kind != "a" && kind != "b" && kind != "A" && kind != "B")) {
                throw std::runtime_error("Invalid measurement name or receiver kind");
            }
            measurement.receiverKind = kind.front();
            measurement.receiverPort = positiveInteger(body, "receiverPort", 4U);
            measurement.sourcePort = positiveInteger(body, "sourcePort", 4U);
            operationValue = service.defineMeasurement(measurement, expected, id);
        } else if (request.path.find("/api/v1/measurements/") == 0U && request.method == "DELETE") {
            allowed(body, {"requestId", "expectedRevision"});
            const auto expected = revision(body);
            if (expected != service.settings().revision) {
                return httpError(409, "revision_conflict", "Configuration revision changed");
            }
            const std::string name = request.path.substr(std::string("/api/v1/measurements/").size());
            if (!safeName(name)) {
                throw std::runtime_error("Invalid measurement name");
            }
            operationValue = service.deleteMeasurement(name, expected, id);
        } else {
            return httpError(404, "route_not_found", "Unknown API route");
        }
        if (ready(operationValue)) {
            const auto failed = operationValue.get();
            if (!failed.ok) {
                return httpError(failed.errorCode == -363 ? 429U : 409U, "operation_rejected", failed.message);
            }
        }
        const std::string operationId = "op-" + std::to_string(++sequence);
        Record record;
        record.operation = operationValue;
        record.owner = id;
        record.created = now();
        records.emplace(operationId, std::move(record));
        session.retries.push_back({requestId, fingerprint, operationId});
        if (session.retries.size() > 32U) {
            session.retries.pop_front();
        }
        session.touched = now();
        return accepted(operationId, id);
    }

    HttpResponse handle(const HttpRequest &request)
    {
        poll();  // Enforce lease expiry even between event-loop ticks.
        if (request.method == "GET") {
            if (request.path == "/api/v1/status") {
                return status(request);
            }
            if (request.path == "/api/v1/frame") {
                return frame();
            }
            const std::string prefix = "/api/v1/operations/";
            if (request.path.find(prefix) == 0U) {
                return operation(request.path.substr(prefix.size()), headerToken(request));
            }
            return httpError(404, "route_not_found", "Unknown API route");
        }
        if (shuttingDown) {
            return httpError(503, "shutting_down", "Server is shutting down");
        }
        if (request.method != "POST" && request.method != "DELETE") {
            return httpError(405, "method_not_allowed", "Unsupported API method");
        }
        if (request.method != "POST" &&
            (request.path == "/api/v1/config" || request.path == "/api/v1/operations" ||
                request.path == "/api/v1/measurements" || request.path == "/api/v1/control/claim" ||
                request.path == "/api/v1/control/release")) {
            return httpError(405, "method_not_allowed", "This API route requires POST");
        }
        if (request.method != "DELETE" && request.path.find("/api/v1/measurements/") == 0U) {
            return httpError(405, "method_not_allowed", "Measurement deletion requires DELETE");
        }
        Json body = request.body.empty() ? Json::makeObject() : parseJson(request.body);
        if (body.type != Json::Type::Object) {
            throw std::runtime_error("JSON body must be an object");
        }
        if (request.path == "/api/v1/control/claim" && request.method == "POST") {
            return claim(body);
        }
        if (request.path == "/api/v1/control/release" && request.method == "POST") {
            allowed(body, {});
            return release(headerToken(request));
        }
        return write(request, body);
    }
};

HttpApi::HttpApi(vna_service::InstrumentService &service, Now now) : impl_(new Impl(service, std::move(now)))
{
}

HttpApi::~HttpApi() = default;

HttpResponse HttpApi::handle(const HttpRequest &request)
{
    try {
        return impl_->handle(request);
    } catch (const std::exception &error) {
        return httpError(400, "invalid_request", error.what());
    }
}

void HttpApi::poll()
{
    impl_->poll();
}

void HttpApi::beginShutdown()
{
    impl_->shuttingDown = true;
    for (auto &session : impl_->sessions) {
        session.second.releasing = true;
        impl_->service.beginReleaseControl(session.first);
    }
    impl_->poll();
}

bool HttpApi::drained() const
{
    for (const auto &item : impl_->records) {
        if (!item.second.done) {
            return false;
        }
    }
    for (const auto &item : impl_->sessions) {
        if (impl_->service.controlOwner() == item.first) {
            return false;
        }
    }
    return true;
}
}  // namespace vna_web
