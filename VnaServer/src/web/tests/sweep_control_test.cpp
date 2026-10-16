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

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &key)
{
    const auto *field = value.Find(key);
    Require(field != nullptr, "Missing field " + key);
    return *field;
}

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &owner = "")
{
    VnaWeb::HttpRequest request;
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    request.path = path;
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!owner.empty()) {
        request.headers["x-vna-session"] = owner;
    }
    return api.Handle(request);
}

Json Command(
    const std::string &id, const std::string &action, const std::string &key = "", const std::string &value = "")
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["action"] = Json(action);
    if (!key.empty()) {
        body[key] = Json(value);
    }
    return body;
}

std::string Accepted(const HttpResponse &response)
{
    Require(response.status == 202U, "Expected 202: " + VnaWeb::StringifyJson(response.json));
    return Field(response.json, "operationId").string;
}

void Finish(HttpApi &api, const std::string &id, const std::string &owner)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto result = Request(api, "/api/v1/operations/" + id, Json(), owner);
        Require(result.status == 200U, "Owned operation is readable");
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", "Sweep failed: " + VnaWeb::StringifyJson(result.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Sweep operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json Config(HttpApi &api)
{
    return Field(Request(api, "/api/v1/status").json, "config");
}

void State(HttpApi &api, const std::string &mode, const std::string &source, const std::string &runtime)
{
    const auto config = Config(api);
    Require(Field(config, "selectedSweepMode").string == mode && Field(config, "triggerSource").string == source &&
                Field(config, "sweepMode").string == runtime,
        "Selected mode/source must be independent of runtime: " + VnaWeb::StringifyJson(config));
}

void Flow()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto claimed = Request(api, "/api/v1/control/claim", Json::MakeObject());
    const auto owner = Field(claimed.json, "sessionId").string;
    const auto apply = [&](const Json &body) {
        Finish(api, Accepted(Request(api, "/api/v1/sweep-control", body, owner)), owner);
    };
    apply(Command("manual", "source", "source", "MAN"));
    apply(Command("single-mode", "mode", "mode", "SING"));
    State(api, "SING", "MAN", "HOLD");
    Require(!fixture.core.ReadView().frame, "Selecting settings cannot acquire data");
    apply(Command("arm-single", "start"));
    State(api, "SING", "MAN", "SING");
    Require(!fixture.core.ReadView().frame, "Starting Manual only arms the sweep");

    fixture.device->Arm();
    const auto trigger = Command("manual-frame", "trigger");
    const auto id = Accepted(Request(api, "/api/v1/sweep-control", trigger, owner));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        api.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Manual trigger did not start acquisition");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(Accepted(Request(api, "/api/v1/sweep-control", trigger, owner)) == id, "Retry reuses accepted operation");
    Require(Request(api, "/api/v1/sweep-control", Command("manual-frame", "stop"), owner).status == 409U,
        "Conflicting retry is rejected");
    Require(Request(api, "/api/v1/operations/" + id).status == 403U, "Operation remains private to owner");
    fixture.device->Release();
    Finish(api, id, owner);
    State(api, "SING", "MAN", "HOLD");
    const auto completed = fixture.core.ReadView().frame;
    Require(completed != nullptr, "Manual Single publishes a complete frame");
    Require(Accepted(Request(api, "/api/v1/sweep-control", trigger, owner)) == id &&
                fixture.core.ReadView().frame == completed,
        "Completed trigger retry cannot acquire again");

    apply(Command("continuous-mode", "mode", "mode", "CONT"));
    State(api, "CONT", "MAN", "CONT");
    Require(Field(Request(api, "/api/v1/status").json, "state").string == "Running",
        "Selecting Continuous must arm Manual without a separate Start command");
    const auto run = fixture.core.ReadView().runId;
    Require(fixture.core.ReadView().frame == completed, "Arming Manual Continuous preserves the previous frame");
    apply(Command("manual-1", "trigger"));
    const auto first = fixture.core.ReadView().frame;
    State(api, "CONT", "MAN", "CONT");
    apply(Command("manual-2", "trigger"));
    const auto second = fixture.core.ReadView().frame;
    Require(first && second && first->runId == run && second->runId == run && second->frameSeq == first->frameSeq + 1U,
        "Each Manual trigger acquires one frame while Continuous stays armed");
    apply(Command("stop-manual", "stop"));
    State(api, "CONT", "MAN", "HOLD");

    apply(Command("quick-manual", "single"));
    State(api, "SING", "MAN", "SING");
    Require(fixture.core.ReadView().frame == second, "Toolbar Single preserves Manual and only arms");
    apply(Command("stop-quick", "stop"));
    apply(Command("free-run", "source", "source", "IMM"));
    apply(Command("free-single", "start"));
    State(api, "SING", "IMM", "HOLD");
    Require(fixture.core.ReadView().frame->runId == fixture.core.ReadView().runId,
        "Free Run Single completes one new frame");
    apply(Command("free-continuous-mode", "mode", "mode", "CONT"));
    State(api, "CONT", "IMM", "CONT");
    apply(Command("stop-free", "stop"));
    State(api, "CONT", "IMM", "HOLD");
}

void ContinuousSelectionStartsWithoutStartCommand()
{
    for (const auto source : {"IMM", "MAN"}) {
        VnaServiceTest::Fixture fixture;
        HttpApi api(fixture.service);
        const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
        const auto apply = [&](const Json &body) {
            Finish(api, Accepted(Request(api, "/api/v1/sweep-control", body, owner)), owner);
        };
        apply(Command("auto-select-single", "mode", "mode", "SING"));
        apply(Command("auto-select-source", "source", "source", source));
        State(api, "SING", source, "HOLD");
        Require(!fixture.core.ReadView().frame, "Selecting Single and a stopped source must not acquire");
        if (std::string(source) == "IMM") {
            fixture.device->Arm();
        }
        const auto continuous = Command("auto-select-continuous", "mode", "mode", "CONT");
        const auto selection = Accepted(Request(api, "/api/v1/sweep-control", continuous, owner));
        Finish(api, selection, owner);
        State(api, "CONT", source, "CONT");
        Require(Field(Request(api, "/api/v1/status").json, "state").string == "Running",
            "Switching Single to Continuous must start or arm the retained source without Start");
        const auto run = fixture.core.ReadView().runId;
        if (std::string(source) == "IMM") {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!fixture.device->HasEntered()) {
                api.Poll();
                Require(std::chrono::steady_clock::now() < deadline, "Continuous selection did not begin Free Run");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Require(!fixture.core.ReadView().frame, "Selecting Continuous with Manual must only arm");
        }
        apply(Command("auto-repeat-continuous", "mode", "mode", "CONT"));
        State(api, "CONT", source, "CONT");
        Require(fixture.core.ReadView().runId == run &&
                    Accepted(Request(api, "/api/v1/sweep-control", continuous, owner)) == selection,
            "Repeated Continuous selection and request replay must retain the active run");
        fixture.device->Release();
        if (std::string(source) == "MAN") {
            apply(Command("auto-manual-trigger", "trigger"));
            const auto frame = fixture.core.ReadView().frame;
            Require(frame && frame->runId == run && frame->frameSeq == 1U,
                "Manual Continuous must accept a trigger immediately after selection without Start");
            State(api, "CONT", "MAN", "CONT");
        }
        apply(Command("auto-stop", "stop"));
        apply(Command("auto-repeat-stopped-continuous", "mode", "mode", "CONT"));
        State(api, "CONT", source, "HOLD");
        Require(fixture.core.ReadView().runId == run,
            "Selecting the already selected Continuous mode after Stop must remain idempotent");
    }
}

void SourceChangesPreserveRuntime()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    const auto apply = [&](const Json &body) {
        Finish(api, Accepted(Request(api, "/api/v1/sweep-control", body, owner)), owner);
    };
    fixture.device->Arm();
    apply(Command("source-start-continuous", "start"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        api.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Continuous sweep did not enter the frame barrier");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto manual = Command("source-running-manual", "source", "source", "MAN");
    const auto switching = Accepted(Request(api, "/api/v1/sweep-control", manual, owner));
    Require(Field(Request(api, "/api/v1/operations/" + switching, Json(), owner).json, "state").string == "pending",
        "Source change must wait until the in-flight frame has finished");
    State(api, "CONT", "IMM", "CONT");
    fixture.device->Release();
    Finish(api, switching, owner);
    State(api, "CONT", "MAN", "CONT");
    Require(Field(Request(api, "/api/v1/status").json, "state").string == "Running",
        "Selecting Manual during Continuous must leave the channel running and armed");
    const auto armed = fixture.core.ReadView();
    Require(armed.frame != nullptr, "Source handoff must finish and preserve the preceding complete frame");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(fixture.core.ReadView().frame == armed.frame, "Source change to Manual must not trigger a frame");
    Require(Accepted(Request(api, "/api/v1/sweep-control", manual, owner)) == switching &&
                fixture.core.ReadView().runId == armed.runId,
        "Replaying a source change must reuse its operation instead of restarting the channel");

    apply(Command("source-manual-frame-1", "trigger"));
    const auto first = fixture.core.ReadView().frame;
    State(api, "CONT", "MAN", "CONT");
    apply(Command("source-manual-frame-2", "trigger"));
    const auto second = fixture.core.ReadView().frame;
    Require(first && second && first->runId == armed.runId && second->runId == armed.runId &&
                second->frameSeq == first->frameSeq + 1U,
        "Each trigger after switching to Manual must acquire exactly one frame of the armed Continuous run");
    State(api, "CONT", "MAN", "CONT");
    Require(Field(Request(api, "/api/v1/status").json, "state").string == "Running",
        "Manual Continuous must remain armed after repeated triggers");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(fixture.core.ReadView().frame == second, "Manual Continuous must not acquire without another trigger");

    apply(Command("source-stop", "stop"));
    const auto stoppedRun = fixture.core.ReadView().runId;
    apply(Command("source-stopped-free-run", "source", "source", "IMM"));
    State(api, "CONT", "IMM", "HOLD");
    apply(Command("source-stopped-manual", "source", "source", "MAN"));
    State(api, "CONT", "MAN", "HOLD");
    Require(Field(Request(api, "/api/v1/status").json, "state").string == "Ready" &&
                fixture.core.ReadView().runId == stoppedRun && fixture.core.ReadView().frame == second,
        "Changing source on a stopped channel must keep it stopped without starting or acquiring");

    apply(Command("source-select-single", "mode", "mode", "SING"));
    apply(Command("source-arm-single", "start"));
    State(api, "SING", "MAN", "SING");
    Require(fixture.core.ReadView().frame == second, "Starting Manual Single must only arm the channel");
    fixture.device->Arm();
    const auto freeRun = Command("source-single-free-run", "source", "source", "IMM");
    const auto singleSwitch = Accepted(Request(api, "/api/v1/sweep-control", freeRun, owner));
    const auto singleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        api.Poll();
        Require(std::chrono::steady_clock::now() < singleDeadline, "Free Run source change did not acquire Single");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(Field(Request(api, "/api/v1/operations/" + singleSwitch, Json(), owner).json, "state").string == "pending",
        "Single source change cannot report completion before its full frame is acquired");
    Require(Request(api, "/api/v1/sweep-control", Command("source-single-overlap", "trigger"), owner).status == 409U,
        "The finite source-change acquisition must reject overlapping triggers");
    Require(Accepted(Request(api, "/api/v1/sweep-control", freeRun, owner)) == singleSwitch,
        "Replaying the finite source change must not duplicate its acquisition");
    fixture.device->Release();
    Finish(api, singleSwitch, owner);
    State(api, "SING", "IMM", "HOLD");
    const auto completed = fixture.core.ReadView();
    Require(Field(Request(api, "/api/v1/status").json, "state").string == "Ready" && completed.frame &&
                completed.frame != second && completed.frame->runId == completed.runId &&
                completed.frame->frameSeq == 1U,
        "Switching an armed Manual Single to Free Run must complete one new frame before reporting success");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(fixture.core.ReadView().frame == completed.frame,
        "Single source handoff must stop after one frame and retain the Single selection");
}

void Validation()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto command = Command("unowned", "start");
    Require(Request(api, "/api/v1/sweep-control", command).status == 403U, "Writes require an owner");
    Require(Request(api, "/api/v1/sweep-control").status == 405U, "Control endpoint requires POST");
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    const auto before = VnaWeb::StringifyJson(Config(api));
    for (const auto &invalid :
        {Command("hold-mode", "mode", "mode", "HOLD"), Command("external", "source", "source", "EXT"),
            Command("mixed", "start", "source", "MAN"), Command("unknown", "restartAll"), Command("missing", "mode")}) {
        Require(Request(api, "/api/v1/sweep-control", invalid, owner).status == 400U,
            "Unsupported or mixed command is rejected");
    }
    Require(VnaWeb::StringifyJson(Config(api)) == before && !fixture.core.ReadView().frame,
        "Rejected commands leave settings and acquisition unchanged");
}
}  // 匿名命名空间

int main()
{
    try {
        Flow();
        ContinuousSelectionStartsWithoutStartCommand();
        SourceChangesPreserveRuntime();
        Validation();
        std::cout << "Sweep control HTTP flow passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
