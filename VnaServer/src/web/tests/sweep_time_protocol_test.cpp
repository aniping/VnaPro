/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"
#include "scpi/scpi_query_policy.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using VnaWeb::Json;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &name)
{
    const auto *field = value.Find(name);
    Require(field != nullptr, "Missing field: " + name);
    return *field;
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Require(engine.SubmitLine(line), "SCPI request rejected: " + line);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "SCPI request timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (engine.HasPending());
    return engine.TakeOutput();
}

VnaWeb::HttpResponse Request(
    VnaWeb::HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &owner = "")
{
    VnaWeb::HttpRequest request;
    request.path = path;
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!owner.empty()) {
        request.headers["x-vna-session"] = owner;
    }
    return api.Handle(request);
}

Json TimingPatch(VnaWeb::HttpApi &api, const std::string &id)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedRevision"] = Field(Request(api, "/api/v1/status").json, "revision");
    return body;
}

void Complete(VnaWeb::HttpApi &api, const VnaWeb::HttpResponse &accepted, const std::string &owner)
{
    Require(accepted.status == 202U, "Timing write must be accepted");
    const auto path = "/api/v1/operations/" + Field(accepted.json, "operationId").string;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto result = Request(api, path, Json(), owner);
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", "Timing write failed: " + VnaWeb::StringifyJson(result.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Timing write timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ScpiTimeCommandsShareCommittedConfiguration()
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    Require(Command(engine, "SENS1:SWE:TIME:AUTO?") == "1\n", "Initial sweep time must be automatic");
    Require(std::stod(Command(engine, "SENS1:SWE:TIME?")) == 0.0, "Unknown Auto host time must remain explicitly zero");
    for (const auto minimum : {"MIN", "MINIMUM"}) {
        Require(Command(engine, std::string("SENS1:SWE:TIME? ") + minimum).empty() &&
                    Command(engine, "SYST:ERR?").find("-221,") == 0U,
            "Unavailable hardware minimum must fail explicitly instead of returning the unknown-time sentinel");
    }
    Require(std::stod(Command(engine, "SENS1:SWE:TIME? MAX")) == 100000.0, "Maximum requested host time is queryable");
    Require(Command(engine, "SENSe1:SWEep:TIME 250MS;:SENS1:SWE:TIME:AUTO?") == "0\n" &&
                std::stod(Command(engine, "SENS1:SWE:TIME?")) == 0.25,
        "Setting a numeric sweep time must turn Auto off and convert milliseconds to seconds");
    Require(
        Command(engine, "SENS1:SWE:TIME:AUTO ON;:SENS1:SWE:TIME:AUTO?") == "1\n", "Explicit Auto On must be readable");
    Require(std::stod(Command(engine, "SENS1:SWE:TIME:AUTO OFF;:SENS1:SWE:TIME?")) == 0.25,
        "Auto Off must retain the last requested manual duration");
    VnaWeb::HttpApi api(fixture.service);
    const auto status = Request(api, "/api/v1/status").json;
    const auto &config = Field(status, "config");
    Require(!Field(config, "sweepTimeAuto").boolean && Field(config, "sweepTimeSeconds").number == 0.25,
        "HTTP and SCPI must read the same committed sweep timing");
    for (const auto line : {"SENS:SWE:TIME -1", "SENS:SWE:TIME 0", "SENS:SWE:TIME NAN", "SENS:SWE:TIME INF",
             "SENS:SWE:TIME 100001", "SENS:SWE:TIME 1HZ", "SENS:SWE:TIME:AUTO MAYBE"}) {
        Command(engine, std::string("*CLS;") + line);
        const auto error = Command(engine, "SYST:ERR?");
        Require(!error.empty() && error.find("0,") != 0U && std::stod(Command(engine, "SENS:SWE:TIME?")) == 0.25,
            "Invalid sweep time must not mutate the committed duration");
    }
}

void HttpTimingWritesAreTypedVersionedAndIdempotent()
{
    VnaServiceTest::Fixture fixture;
    VnaWeb::HttpApi api(fixture.service);
    const auto status = Request(api, "/api/v1/status").json;
    Require(Field(Field(status, "config"), "sweepTimeAuto").boolean, "HTTP default sweep timing must be Auto");
    const auto owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    auto body = TimingPatch(api, "manual-time");
    body["sweepTimeSeconds"] = Json(2.0);
    const auto accepted = Request(api, "/api/v1/config", body, owner);
    Complete(api, accepted, owner);
    Require(Field(Request(api, "/api/v1/config", body, owner).json, "operationId").string ==
                Field(accepted.json, "operationId").string,
        "Retry must return the same timing operation without resubmitting");
    const auto changed = Request(api, "/api/v1/status").json;
    Require(!Field(Field(changed, "config"), "sweepTimeAuto").boolean &&
                Field(Field(changed, "config"), "sweepTimeSeconds").number == 2.0 &&
                Field(changed, "acquisitionRevision").string != Field(status, "acquisitionRevision").string,
        "Committed manual timing must advance the real acquisition plan revision");
    body = TimingPatch(api, "typed-time");
    body["sweepTimeAuto"] = Json("OFF");
    Require(Request(api, "/api/v1/config", body, owner).status == 400U, "Auto state must be a JSON boolean");
    body = TimingPatch(api, "auto-time");
    body["sweepTimeAuto"] = Json(true);
    Complete(api, Request(api, "/api/v1/config", body, owner), owner);
    Require(Field(Field(Request(api, "/api/v1/status").json, "config"), "sweepTimeAuto").boolean,
        "HTTP Auto On must commit through the same configuration path");
}

void TimingQueriesRemainObserverSafe()
{
    std::vector<std::string> parent;
    for (const auto &unit : VnaScpi::SplitProgram("SENSe2:SWEep:TIME?;:SENS2:SWE:TIME:AUTO?")) {
        const auto command = VnaScpi::ParseCommand(unit, parent);
        Require(
            VnaScpi::IsObserverQuery(VnaScpi::RouteChannelCommand(command)), "Timing queries must remain read-only");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        ScpiTimeCommandsShareCommittedConfiguration();
        HttpTimingWritesAreTypedVersionedAndIdempotent();
        TimingQueriesRemainObserverSafe();
        std::cout << "PASS sweep timing SCPI/HTTP configuration and typed writes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
