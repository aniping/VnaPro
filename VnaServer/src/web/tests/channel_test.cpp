/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
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

const Json &Field(const Json &value, const std::string &name)
{
    const auto *found = value.Find(name);
    Require(found != nullptr, "Missing field " + name);
    return *found;
}

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &owner = "")
{
    VnaWeb::HttpRequest request;
    const auto separator = path.find('?');
    request.path = path.substr(0U, separator);
    request.query = separator == std::string::npos ? "" : path.substr(separator + 1U);
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!owner.empty()) {
        request.headers["x-vna-session"] = owner;
    }
    return api.Handle(request);
}

Json Status(HttpApi &api)
{
    return Request(api, "/api/v1/status").json;
}

Json Action(const std::string &id, const std::string &action, unsigned channelId = 0U)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    if (!action.empty()) {
        body["action"] = Json(action);
    }
    if (channelId != 0U) {
        body["channelId"] = Json(channelId);
    }
    return body;
}

Json ChannelAction(HttpApi &api, const std::string &id, const std::string &action, unsigned channelId = 0U)
{
    auto body = Action(id, action, channelId);
    const auto status = Status(api);
    body["expectedRevision"] = Field(status, "revision");
    body["expectedDisplayRevision"] = Field(Field(status, "display"), "revision");
    return body;
}

std::string Accepted(const HttpResponse &response, const std::string &context = "")
{
    Require(response.status == 202U,
        "Expected accepted operation " + context + ": " + VnaWeb::StringifyJson(response.json));
    return Field(response.json, "operationId").string;
}

void Finish(HttpApi &api, const std::string &operation, const std::string &owner, bool success = true)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto response = Request(api, "/api/v1/operations/" + operation, Json(), owner);
        Require(response.status == 200U, "Read owned operation");
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == (success ? "completed" : "failed"),
                "Unexpected channel operation result: " + VnaWeb::StringifyJson(response.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Channel operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

const Json &Channel(const Json &status, unsigned id)
{
    for (const auto &item : Field(status, "channels").array) {
        if (Field(item, "channelId").number == id) {
            return item;
        }
    }
    throw std::runtime_error("Channel metadata missing");
}

std::string Trace(const Json &status, unsigned id)
{
    for (const auto &item : Field(status, "measurements").array) {
        if (Field(item, "channelId").number == id) {
            return Field(item, "name").string;
        }
    }
    throw std::runtime_error("Channel trace missing");
}

void Flow()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    const auto apply = [&](const std::string &path, const Json &body) {
        Finish(api, Accepted(Request(api, path, body, owner), Field(body, "requestId").string), owner);
    };
    const auto original = Field(Channel(Status(api), 1U), "config");
    apply("/api/v1/channels", ChannelAction(api, "new-ch2", "new"));
    auto status = Status(api);
    Require(Field(status, "activeChannelId").number == 2.0 && Field(status, "channels").array.size() == 2U,
        "New creates and selects an independent second channel");
    const auto firstTrace = Trace(status, 1U);
    const auto secondTrace = Trace(status, 2U);
    auto mismatch = ChannelAction(api, "zoom-owner-mismatch", "apply", 2U);
    mismatch["traceName"] = Json(firstTrace);
    mismatch["startHz"] = Json(2000000.0);
    mismatch["stopHz"] = Json(3000000.0);
    mismatch["min"] = Json(-30.0);
    mismatch["max"] = Json(-20.0);
    Require(Request(api, "/api/v1/stimulus-zoom", mismatch, owner).status == 409U,
        "A named zoom trace cannot be applied to a different explicit channel");
    mismatch["requestId"] = Json("zoom-unknown-owner");
    mismatch.object.erase("channelId");
    mismatch["traceName"] = Json("Missing");
    Require(Request(api, "/api/v1/stimulus-zoom", mismatch, owner).status == 400U,
        "Unknown trace ownership must fail before reading a fallback channel snapshot");
    auto config = Action("config-ch2", "", 2U);
    config["expectedRevision"] = Field(status, "revision");
    config["startHz"] = Json(20000000.0);
    config["stopHz"] = Json(30000000.0);
    config["points"] = Json(9U);
    config["powerDbm"] = Json(-17.5);
    apply("/api/v1/config", config);
    Require(VnaWeb::StringifyJson(Field(Channel(Status(api), 1U), "config")) == VnaWeb::StringifyJson(original),
        "Second-channel stimulus settings cannot change the first channel");
    const auto empty = Request(api, "/api/v1/frame?channelId=1");
    Require(
        empty.status == 200U && Field(empty.json, "channelId").number == 1.0 && !Field(empty.json, "available").boolean,
        "Unavailable frames still identify the requested channel");
    apply("/api/v1/sweep-control", Action("sample-ch1", "single", 1U));
    fixture.device->Arm();
    const auto sampling = Accepted(Request(api, "/api/v1/sweep-control", Action("sample-ch2", "single", 2U), owner));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        api.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Second channel did not begin sampling");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto retained = Request(api, "/api/v1/frame?channelId=1");
    Require(Field(retained.json, "current").boolean, "A pending Ch2 Single must not invalidate Ch1 complete data");
    fixture.device->Release();
    Finish(api, sampling, owner);
    const auto measured = fixture.service.ChannelSnapshot(2U).core.frame;
    Require(measured != nullptr && measured->plan.powerDbm.size() == 9U && measured->plan.powerDbm.front() == -17.5 &&
                measured->plan.powerDbm.back() == -17.5,
        "HTTP channel power must reach the physical acquisition plan at every frequency point");
    Require(Field(Field(Channel(Status(api), 2U), "config"), "powerDbm").number == -17.5,
        "Channel power readback must reflect the applied setting");
    apply("/api/v1/channels", ChannelAction(api, "select-ch1", "select", 1U));
    const auto targeted = Request(api, "/api/v1/frame?trace=" + secondTrace);
    Require(targeted.status == 200U && Field(targeted.json, "channelId").number == 2.0 &&
                Field(targeted.json, "frequencyHz").array.size() == 9U &&
                Field(targeted.json, "frequencyHz").array.front().number == 20000000.0 &&
                Field(targeted.json, "frequencyHz").array.back().number == 30000000.0,
        "Named trace requests must use their real channel frame and axis even while another channel is active");
    Require(Field(Status(api), "activeChannelId").number == 1.0, "Reading another channel cannot select it");
    Require(Request(api, "/api/v1/frame?channelId=1&trace=" + secondTrace).status == 400U,
        "Explicit channel and trace owner mismatches are rejected");
    const auto combined = Request(api, "/api/v1/frame?trace=" + firstTrace + "&trace=" + secondTrace);
    Require(combined.status == 200U && Field(combined.json, "channelFrames").array.size() == 2U,
        "Mixed-channel display data is grouped into independent frame responses");
    const auto globalRevision = Field(Status(api), "revision").string;
    for (const auto &frame : Field(combined.json, "channelFrames").array) {
        Require(frame.Find("channelFrames") == nullptr, "Per-channel frames cannot recursively duplicate all channels");
        Require(Field(frame, "measurementRevision").string == globalRevision,
            "All channel frames share the global measurement catalog revision");
        for (const auto &trace : Field(frame, "traces").array) {
            Require(Field(trace, "channelId").number == Field(frame, "channelId").number,
                "A channel frame may only contain traces measured on that channel");
        }
    }
    const auto beforeZoom = Field(Channel(Status(api), 1U), "config");
    auto zoom = ChannelAction(api, "zoom-second-only", "apply", 2U);
    zoom["traceName"] = Json(secondTrace);
    zoom["startHz"] = Json(23000000.0);
    zoom["stopHz"] = Json(27000000.0);
    zoom["min"] = Json(-30.0);
    zoom["max"] = Json(-20.0);
    apply("/api/v1/stimulus-zoom", zoom);
    const auto zoomed = Status(api);
    Require(Field(Field(Channel(zoomed, 2U), "config"), "startHz").number == 23000000.0 &&
                Field(Field(Channel(zoomed, 2U), "config"), "stopHz").number == 27000000.0 &&
                VnaWeb::StringifyJson(Field(Channel(zoomed, 1U), "config")) == VnaWeb::StringifyJson(beforeZoom) &&
                Field(Request(api, "/api/v1/frame?channelId=1").json, "current").boolean,
        "Ch2 stimulus zoom changes its physical span while Ch1 retains its settings and current held frame");
    apply("/api/v1/stimulus-zoom", ChannelAction(api, "zoom-out-second", "out", 2U));
    const auto copy = ChannelAction(api, "copy-ch2", "copy", 2U);
    const auto copied = Accepted(Request(api, "/api/v1/channels", copy, owner));
    Finish(api, copied, owner);
    status = Status(api);
    Require(Field(status, "activeChannelId").number == 3.0 &&
                VnaWeb::StringifyJson(Field(Channel(status, 3U), "config")) ==
                    VnaWeb::StringifyJson(Field(Channel(status, 2U), "config")),
        "Copy retains source-channel stimulus settings");
    Require(Accepted(Request(api, "/api/v1/channels", copy, owner)) == copied &&
                Field(Status(api), "channels").array.size() == 3U,
        "Channel creation replay cannot create a second copy");
    apply("/api/v1/channels", ChannelAction(api, "replay-select-ch2", "select", 2U));
    auto implicit = Action("replay-implicit-source", "source");
    implicit["source"] = Json("MAN");
    const auto implicitOperation = Accepted(Request(api, "/api/v1/sweep-control", implicit, owner));
    Finish(api, implicitOperation, owner);
    auto explicitTarget = Action("replay-explicit-source", "source", 2U);
    explicitTarget["source"] = Json("IMM");
    const auto explicitOperation = Accepted(Request(api, "/api/v1/sweep-control", explicitTarget, owner));
    Finish(api, explicitOperation, owner);
    apply("/api/v1/channels", ChannelAction(api, "replay-select-ch1", "select", 1U));
    Require(Accepted(Request(api, "/api/v1/sweep-control", implicit, owner)) == implicitOperation &&
                Accepted(Request(api, "/api/v1/sweep-control", explicitTarget, owner)) == explicitOperation,
        "Changing active channel cannot retarget accepted request replays");
    apply("/api/v1/channels", ChannelAction(api, "delete-ch3", "delete", 3U));
    apply("/api/v1/channels", ChannelAction(api, "delete-ch2", "delete", 2U));
    Require(Accepted(Request(api, "/api/v1/sweep-control", implicit, owner)) == implicitOperation &&
                Accepted(Request(api, "/api/v1/sweep-control", explicitTarget, owner)) == explicitOperation,
        "Deleting the original channel cannot invalidate or retarget an accepted request replay");
    const auto last = Request(api, "/api/v1/channels", ChannelAction(api, "delete-last-channel", "delete", 1U), owner);
    if (last.status == 202U) {
        Finish(api, Accepted(last), owner, false);
    } else {
        Require(last.status == 409U, "Last channel removal must be rejected");
    }
    Require(Field(Status(api), "channels").array.size() == 1U, "Deletion cannot leave an instrument without channels");
    Require(Request(api, "/api/v1/calibration?channelId=1").status == 200U &&
                Request(api, "/api/v1/calibration?channelId=2").status == 400U,
        "Calibration reads honor explicit existing channel identities");
}

void ExplicitTargetSurvivesSelection()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    Finish(api, Accepted(Request(api, "/api/v1/channels", ChannelAction(api, "target-new", "new"), owner)), owner);
    const auto original = Field(Field(Channel(Status(api), 1U), "config"), "startHz").number;
    fixture.device->Arm();
    const auto single =
        Accepted(Request(api, "/api/v1/sweep-control", Action("target-blocking-single", "single", 1U), owner));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        api.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Finite acquisition did not enter the barrier");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto select =
        Accepted(Request(api, "/api/v1/channels", ChannelAction(api, "target-select-first", "select", 1U), owner));
    auto config = Action("target-config-second", "", 2U);
    config["expectedRevision"] = Field(Status(api), "revision");
    config["startHz"] = Json(25000000.0);
    const auto configure = Accepted(Request(api, "/api/v1/config", config, owner), "target-config-second");
    fixture.device->Release();
    Finish(api, single, owner);
    Finish(api, select, owner);
    Finish(api, configure, owner);
    const auto status = Status(api);
    Require(Field(status, "activeChannelId").number == 1.0 &&
                Field(Field(Channel(status, 1U), "config"), "startHz").number == original &&
                Field(Field(Channel(status, 2U), "config"), "startHz").number == 25000000.0,
        "An explicit asynchronous target cannot change when an earlier queued selection chooses another channel");
    for (const auto &query : {"channelId=0", "channelId=2&channelId=2", "channelId=1.5", "channelId=999"}) {
        Require(Request(api, std::string("/api/v1/frame?") + query).status == 400U,
            "Invalid channel selectors must be rejected");
    }
    Require(Request(api, "/api/v1/channels").status == 405U, "Channel mutations require POST");
    auto invalidPower = Action("invalid-power", "", 2U);
    invalidPower["expectedRevision"] = Field(Status(api), "revision");
    invalidPower["powerDbm"] = Json(100.01);
    Finish(api, Accepted(Request(api, "/api/v1/config", invalidPower, owner), "invalid-power"), owner, false);
    Require(Field(Field(Channel(Status(api), 2U), "config"), "powerDbm").number == 0.0,
        "Rejected power candidates cannot change committed channel settings");
    invalidPower["requestId"] = Json("invalid-power-type");
    invalidPower["powerDbm"] = Json("-10");
    Require(Request(api, "/api/v1/config", invalidPower, owner).status == 400U,
        "Power configuration requires a numeric dBm value");
    const auto &capabilities = Field(status, "capabilities");
    Require(Field(capabilities, "minPowerDbm").number == -120.0 && Field(capabilities, "maxPowerDbm").number == 100.0,
        "Power capabilities expose the service plan bounds");
}

void CalibrationTargets()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    const auto apply = [&](const std::string &path, const Json &body) {
        Finish(api, Accepted(Request(api, path, body, owner), Field(body, "requestId").string), owner);
    };
    apply("/api/v1/channels", ChannelAction(api, "cal-new", "new"));
    apply("/api/v1/channels", ChannelAction(api, "cal-select-first", "select", 1U));
    auto begin = Action("cal-begin-second", "begin", 2U);
    begin["expectedRevision"] = Field(Request(api, "/api/v1/calibration?channelId=2").json, "revision");
    begin["method"] = Json("onePort");
    begin["port1"] = Json(1U);
    apply("/api/v1/calibration", begin);
    auto second = Request(api, "/api/v1/calibration?channelId=2").json;
    Require(Field(second, "collecting").boolean &&
                !Field(Request(api, "/api/v1/calibration?channelId=1").json, "collecting").boolean,
        "Calibration writes and reads must address the explicit channel independently of active selection");
    const auto sessionId = Field(second, "sessionId").string;
    const auto stepId = Field(Field(second, "steps").array.front(), "id").string;
    auto capture = Action("cal-capture-second", "capture", 2U);
    capture["expectedRevision"] = Field(second, "revision");
    capture["sessionId"] = Json(sessionId);
    capture["stepId"] = Json(stepId);
    apply("/api/v1/calibration", capture);
    const auto query = "&sessionId=" + sessionId + "&stepId=" + stepId;
    Require(Field(Request(api, "/api/v1/calibration/preview?channelId=2" + query).json, "available").boolean &&
                !Field(Request(api, "/api/v1/calibration/preview?channelId=1" + query).json, "available").boolean,
        "Saved standard preview must never fall back to a different channel session");
    second = Request(api, "/api/v1/calibration?channelId=2").json;
    auto cancel = Action("cal-cancel-second", "cancel", 2U);
    cancel["expectedRevision"] = Field(second, "revision");
    cancel["sessionId"] = Json(sessionId);
    apply("/api/v1/calibration", cancel);
    Require(Field(Status(api), "activeChannelId").number == 1.0,
        "Targeted calibration cannot switch the active measurement channel");
}
}  // 匿名命名空间

int main()
{
    try {
        Flow();
        ExplicitTargetSurvivesSelection();
        CalibrationTargets();
        std::cout << "Channel HTTP tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
