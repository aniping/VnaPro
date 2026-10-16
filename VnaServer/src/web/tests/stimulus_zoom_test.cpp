/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &key)
{
    const auto *field = value.Find(key);
    Require(field != nullptr, "Missing field " + key);
    return *field;
}

bool Equal(const Json &left, const Json &right)
{
    return VnaWeb::StringifyJson(left) == VnaWeb::StringifyJson(right);
}

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const Json &body = Json(),
    const std::string &owner = "")
{
    VnaWeb::HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!owner.empty()) {
        request.headers["x-vna-session"] = owner;
    }
    return api.Handle(request);
}

Json Status(HttpApi &api)
{
    const auto response = Request(api, "GET", "/api/v1/status");
    Require(response.status == 200U, "Observer can read status");
    return response.json;
}

Json Presentation(const Json &status, const std::string &name)
{
    return Field(Field(Field(status, "display"), "presentations"), name);
}

std::string Selected(const Json &status)
{
    return Field(Field(status, "display"), "selectedMeasurement").string;
}

Json Zoom(HttpApi &api, const std::string &id, bool out = false)
{
    const auto status = Status(api);
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedRevision"] = Field(status, "revision");
    body["expectedDisplayRevision"] = Field(Field(status, "display"), "revision");
    body["action"] = Json(out ? "out" : "apply");
    if (!out) {
        const auto &config = Field(status, "config");
        const auto start = Field(config, "startHz").number;
        const auto span = Field(config, "stopHz").number - start;
        body["traceName"] = Json(Selected(status));
        body["startHz"] = Json(start + span / 4.0);
        body["stopHz"] = Json(start + span * 3.0 / 4.0);
        body["min"] = Json(-42.0);
        body["max"] = Json(-12.0);
    }
    return body;
}

Json DisplayChange(HttpApi &api, const std::string &id, const std::string &action, const std::string &name)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedDisplayRevision"] = Field(Field(Status(api), "display"), "revision");
    body["action"] = Json(action);
    body["traceName"] = Json(name);
    return body;
}

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "POST", "/api/v1/control/claim", Json::MakeObject());
    Require(response.status == 200U, "Claim succeeds");
    return Field(response.json, "sessionId").string;
}

std::string Accepted(const HttpResponse &response)
{
    Require(response.status == 202U, "Expected 202: " + VnaWeb::StringifyJson(response.json));
    return Field(response.json, "operationId").string;
}

void Finish(
    HttpApi &api, const std::string &operation, const std::string &owner, const std::function<void()> &check = [] {
    })
{
    const auto deadline = HttpApi::Clock::now() + std::chrono::seconds(3);
    for (;;) {
        check();
        const auto response = Request(api, "GET", "/api/v1/operations/" + operation, Json(), owner);
        Require(response.status == 200U, "Owner reads accepted operation");
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", "Operation failed: " + VnaWeb::StringifyJson(response.json));
            return;
        }
        Require(HttpApi::Clock::now() < deadline, "Operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Apply(HttpApi &api, const std::string &path, const Json &body, const std::string &owner)
{
    Finish(api, Accepted(Request(api, "POST", "/api/v1/" + path, body, owner)), owner);
}

void RejectOperation(HttpApi &api, const Json &body, const std::string &owner)
{
    const auto response = Request(api, "POST", "/api/v1/stimulus-zoom", body, owner);
    if (response.status == 409U) {
        return;
    }
    const auto operation = Accepted(response);
    const auto deadline = HttpApi::Clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto result = Request(api, "GET", "/api/v1/operations/" + operation, Json(), owner);
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "failed", "Rejected zoom must not complete");
            return;
        }
        Require(HttpApi::Clock::now() < deadline, "Rejected operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ShapePermissionsAndStrictInput()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto initial = Status(api);
    const auto &zoom = Field(initial, "stimulusZoom");
    Require(zoom.object.size() == 4U && Field(zoom, "available").type == Json::Type::BOOLEAN &&
                Field(zoom, "canZoomOut").type == Json::Type::BOOLEAN &&
                Field(zoom, "reason").type == Json::Type::STRING && Field(zoom, "depth").type == Json::Type::NUMBER &&
                Field(zoom, "depth").number == 0.0 && !Field(zoom, "canZoomOut").boolean,
        "Zoom status has exactly four typed fields and no initial history");
    const auto body = Zoom(api, "protected");
    Require(Request(api, "POST", "/api/v1/stimulus-zoom", body).status == 403U, "Observer cannot write stimulus zoom");
    for (const auto *method : {"GET", "PUT", "DELETE"}) {
        Require(Request(api, method, "/api/v1/stimulus-zoom", body).status == 405U, "Zoom requires POST");
    }
    const auto owner = Claim(api);
    unsigned sequence = 0U;
    const auto invalid = [&](Json candidate) {
        candidate["requestId"] = Json("invalid-" + std::to_string(++sequence));
        const auto response = Request(api, "POST", "/api/v1/stimulus-zoom", candidate, owner);
        Require(response.status == 400U, "Malformed zoom rejected: " + VnaWeb::StringifyJson(response.json));
    };
    for (const auto *field : {"geometry", "points", "scale", "unknown"}) {
        auto candidate = body;
        candidate[field] = Json::MakeObject();
        invalid(candidate);
    }
    for (const auto *field :
        {"traceName", "startHz", "stopHz", "min", "max", "expectedRevision", "expectedDisplayRevision"}) {
        auto candidate = body;
        candidate.object.erase(field);
        invalid(candidate);
    }
    for (const auto *field : {"startHz", "stopHz", "min", "max"}) {
        auto candidate = body;
        candidate[field] = Json("1");
        invalid(candidate);
    }
    for (const auto *field : {"expectedRevision", "expectedDisplayRevision"}) {
        for (const auto *revision : {"", "-1", "1.0", "18446744073709551616"}) {
            auto candidate = body;
            candidate[field] = Json(revision);
            invalid(candidate);
        }
        auto candidate = body;
        candidate[field] = Json(1U);
        invalid(candidate);
    }
    auto candidate = body;
    candidate["action"] = Json("APPLY");
    invalid(candidate);
    candidate = body;
    candidate["traceName"] = Json("");
    invalid(candidate);
    candidate = Zoom(api, "out-with-range", true);
    candidate["startHz"] = Json(1.0);
    invalid(candidate);
    for (const auto *field : {"expectedRevision", "expectedDisplayRevision"}) {
        candidate = body;
        candidate["requestId"] = Json(std::string("stale-") + field);
        candidate[field] = Json("0");
        const auto response = Request(api, "POST", "/api/v1/stimulus-zoom", candidate, owner);
        const auto code = std::string(field) == "expectedRevision" ? "revision_conflict" : "display_revision_conflict";
        Require(response.status == 409U && Field(Field(response.json, "error"), "code").string == code,
            "Each stale revision is rejected before admission");
    }
    const auto after = Status(api);
    Require(Equal(Field(after, "config"), Field(initial, "config")) &&
                Equal(Field(after, "display"), Field(initial, "display")),
        "Rejected input and CAS conflicts preserve authoritative state");
}

void AtomicReadbackHistoryAndRetries()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    HttpApi observer(fixture.service);
    const auto owner = Claim(api);
    auto single = Json::MakeObject();
    single["requestId"] = Json("single-before-zoom");
    single["action"] = Json("single");
    Apply(api, "operations", single, owner);
    const auto initial = Status(api);
    const auto name = Selected(initial);
    const auto beforeScale = Field(Presentation(initial, name), "scale");
    const auto body = Zoom(api, "zoom-one");
    const auto operation = Accepted(Request(api, "POST", "/api/v1/stimulus-zoom", body, owner));
    Require(Request(api, "GET", "/api/v1/operations/" + operation).status == 403U,
        "Accepted zoom result is private to controller");
    Finish(api, operation, owner, [&] {
        const auto status = Status(observer);
        const auto start = Field(Field(status, "config"), "startHz").number;
        const auto min = Field(Field(Presentation(status, name), "scale"), "min").number;
        Require(
            (start == Field(Field(initial, "config"), "startHz").number && min == Field(beforeScale, "min").number) ||
                (start == Field(body, "startHz").number && min == Field(body, "min").number),
            "Frequency and scale are published atomically");
    });
    const auto first = Status(observer);
    const auto &config = Field(first, "config");
    Require(Field(config, "startHz").number == Field(body, "startHz").number &&
                Field(config, "stopHz").number == Field(body, "stopHz").number &&
                Field(Field(Presentation(first, name), "scale"), "min").number == -42.0 &&
                Field(Field(Presentation(first, name), "scale"), "max").number == -12.0,
        "Observers read real frequency and trace scale after accepted operation");
    for (const auto *field : {"points", "ifBandwidthHz", "sweepMode", "selectedSweepMode", "triggerSource"}) {
        Require(Equal(Field(config, field), Field(Field(initial, "config"), field)),
            "Zoom preserves " + std::string(field));
    }
    Require(Equal(Field(first, "runId"), Field(initial, "runId")) &&
                Equal(Field(first, "measurements"), Field(initial, "measurements")) &&
                Field(Field(first, "stimulusZoom"), "depth").number == 1.0,
        "Zoom preserves run identity and catalog and creates one history entry");
    const auto frame = Request(observer, "GET", "/api/v1/frame").json;
    Require(Field(frame, "available").boolean && !Field(frame, "current").boolean,
        "Previous frame is stale after changing real stimulus frequencies");
    Require(Accepted(Request(api, "POST", "/api/v1/stimulus-zoom", body, owner)) == operation &&
                Equal(Field(Status(api), "display"), Field(first, "display")),
        "Completed retry returns original operation without changing display or history");
    auto conflictingRetry = body;
    conflictingRetry["min"] = Json(-41.0);
    Require(Request(api, "POST", "/api/v1/stimulus-zoom", conflictingRetry, owner).status == 409U,
        "Request ID cannot be reused with different zoom content");
    Apply(api, "stimulus-zoom", Zoom(api, "zoom-two"), owner);
    Require(Field(Field(Status(api), "stimulusZoom"), "depth").number == 2.0, "Nested zoom records two levels");
    const auto out = Zoom(api, "out-one", true);
    const auto outId = Accepted(Request(api, "POST", "/api/v1/stimulus-zoom", out, owner));
    Finish(api, outId, owner);
    Require(Equal(Field(Field(Status(api), "config"), "startHz"), Field(config, "startHz")) &&
                Field(Field(Status(api), "stimulusZoom"), "depth").number == 1.0,
        "Zoom Out restores exactly the previous frequency range");
    Require(Accepted(Request(api, "POST", "/api/v1/stimulus-zoom", out, owner)) == outId &&
                Field(Field(Status(api), "stimulusZoom"), "depth").number == 1.0,
        "Zoom Out retry cannot pop another history entry");
    Apply(api, "stimulus-zoom", Zoom(api, "out-two", true), owner);
    const auto restored = Status(api);
    Require(Equal(Field(Field(restored, "config"), "startHz"), Field(Field(initial, "config"), "startHz")) &&
                Equal(Field(Presentation(restored, name), "scale"), beforeScale) &&
                Field(Field(restored, "stimulusZoom"), "depth").number == 0.0,
        "Last Zoom Out restores original stimulus and scale and empties history");
    RejectOperation(api, Zoom(api, "out-empty", true), owner);
}

void HistoryPreservesUnrelatedDisplayChanges()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto owner = Claim(api);
    const auto initial = Status(api);
    const auto name = Selected(initial);
    Apply(api, "stimulus-zoom", Zoom(api, "preserve-zoom"), owner);
    auto marker = DisplayChange(api, "preserve-marker", "marker", name);
    marker["enabled"] = Json(true);
    Apply(api, "display", marker, owner);
    auto clone = Zoom(api, "preserve-clone", true);
    clone.object.erase("action");
    clone["sourceName"] = Json(name);
    Apply(api, "measurement-actions/clone", clone, owner);
    const auto copy = Selected(Status(api));
    auto scale = DisplayChange(api, "preserve-other-scale", "scale", copy);
    scale["min"] = Json(-80.0);
    scale["max"] = Json(-20.0);
    Apply(api, "display", scale, owner);
    const auto beforeOut = Status(api);
    Apply(api, "stimulus-zoom", Zoom(api, "preserve-out", true), owner);
    const auto after = Status(api);
    Require(Selected(after) == copy && Equal(Field(after, "measurements"), Field(beforeOut, "measurements")) &&
                Equal(Field(Presentation(after, name), "markers"), Field(Presentation(beforeOut, name), "markers")) &&
                Equal(Presentation(after, copy), Presentation(beforeOut, copy)),
        "Zoom Out preserves selection, catalog, markers and another trace's presentation");
    Require(Equal(Field(Presentation(after, name), "scale"), Field(Presentation(initial, name), "scale")),
        "Zoom Out restores the zoomed trace scale even when another trace is selected");
}

void HistoryConflictsAndCircularFormats()
{
    for (const auto *kind : {"frequency", "scale", "format"}) {
        VnaServiceTest::Fixture fixture;
        HttpApi api(fixture.service);
        const auto owner = Claim(api);
        Apply(api, "stimulus-zoom", Zoom(api, "conflict-zoom"), owner);
        const auto name = Selected(Status(api));
        if (std::string(kind) == "frequency") {
            auto config = Json::MakeObject();
            config["requestId"] = Json("external-frequency");
            config["expectedRevision"] = Field(Status(api), "revision");
            config["startHz"] = Json(Field(Field(Status(api), "config"), "startHz").number + 1.0);
            Apply(api, "config", config, owner);
        } else {
            auto change = DisplayChange(api, "external-display", kind, name);
            if (std::string(kind) == "format") {
                change["format"] = Json("smith");
            } else {
                change["min"] = Json(-43.0);
            }
            Apply(api, "display", change, owner);
        }
        const auto before = Status(api);
        const auto &zoom = Field(before, "stimulusZoom");
        Require(!Field(zoom, "canZoomOut").boolean && !Field(zoom, "reason").string.empty(),
            "External stimulus/scale/format change disables unsafe history rollback");
        RejectOperation(api, Zoom(api, "conflict-out", true), owner);
        if (std::string(kind) == "format") {
            Require(!Field(zoom, "available").boolean, "Circular format disables stimulus rectangle zoom");
            RejectOperation(api, Zoom(api, "circular-apply"), owner);
        }
        const auto after = Status(api);
        Require(Equal(Field(before, "config"), Field(after, "config")) &&
                    Equal(Field(before, "display"), Field(after, "display")),
            "Rejected history rollback and circular zoom preserve state");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        ShapePermissionsAndStrictInput();
        AtomicReadbackHistoryAndRetries();
        HistoryPreservesUnrelatedDisplayChanges();
        HistoryConflictsAndCircularFormats();
        std::cout << "HTTP stimulus zoom tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
