/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "calibration_plan_test_support.h"
#include "calibration_protocol_test_support.h"
#include "fixture.h"
#include "web/json_encoder.h"

namespace {
using namespace VnaCalibrationProtocolTest;

VnaService::InstrumentProfile Profile()
{
    VnaService::InstrumentProfile profile;
    profile.defaultStartHz = 10000000.0;
    profile.defaultStopHz = 10004000.0;
    profile.defaultPoints = 5U;
    return profile;
}

void Failed(HttpApi &api, const HttpResponse &response, const std::string &token)
{
    Require(response.status == 202U, "Rejected before operation acceptance");
    const auto operation = Field(response.json, "operationId").string;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto result = Request(api, "/api/v1/operations/" + operation, Json(), token);
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "failed", "Incompatible calibration must fail: " + VnaWeb::StringifyJson(result.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Calibration failure completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Configure(HttpApi &api, const std::string &token, const std::string &field, double value)
{
    static unsigned sequence = 0U;
    auto body = Json::MakeObject();
    body["requestId"] = Json("applicability-config-" + std::to_string(++sequence));
    body["expectedRevision"] = Field(Request(api, "/api/v1/status").json, "revision");
    body[field] = Json(value);
    Complete(api, Request(api, "/api/v1/config", body, token), token);
}

Json Activate(HttpApi &api, const std::string &id, const std::string &request)
{
    auto body = Action(api, "activate", request);
    body["setId"] = Json(id);
    return body;
}

void RequireMode(HttpApi &api, const std::string &mode, unsigned exact, unsigned interpolated)
{
    const auto state = Request(api, "/api/v1/calibration").json;
    const auto &applicability = Field(state, "applicability");
    Require(Field(state, "effective").boolean && Field(state, "compatible").boolean &&
                Field(applicability, "prepared").boolean && Field(applicability, "frequencyMode").string == mode &&
                Field(applicability, "exactPoints").number == exact &&
                Field(applicability, "interpolatedPoints").number == interpolated,
        "HTTP must publish the prepared actual-axis classification and effective state together");
}

void RestoreSources(VnaServiceTest::Fixture &fixture)
{
    const auto source = CalibrationPlanTest::Source(fixture.service.Identity());
    auto foreign = std::make_shared<VnaService::CalibrationSet>(*source);
    foreign->id = 2U;
    foreign->name = "Other device";
    foreign->deviceIdentity = "Another physical device";
    std::string error;
    Require(fixture.service.RestoreCalibration({source, foreign}, 1U, error), error);
}

void CatalogMetadata(HttpApi &api)
{
    const auto initial = Request(api, "/api/v1/calibration").json;
    const auto &foreignSummary = Field(initial, "sets").array.back();
    Require(!Field(initial, "effective").boolean && !Field(initial, "enabled").boolean &&
                Field(foreignSummary, "deviceIdentity").string == "Another physical device" &&
                Field(foreignSummary, "powerMinDbm").number == 0.0 &&
                Field(Field(foreignSummary, "applicability"), "reasonCode").string == "deviceMismatch",
        "Catalog exposes stimulus/device and rejection reason without activating the saved selection");
}

void PreparedStatesAndBandwidth(HttpApi &api, VnaServiceTest::Fixture &fixture, const std::string &token)
{
    Complete(api, Request(api, "/api/v1/calibration", Activate(api, "1", "apply-original"), token), token);
    RequireMode(api, "exact", 5U, 0U);
    Configure(api, token, "points", 3.0);
    RequireMode(api, "subset", 3U, 0U);
    const auto subset = fixture.service.Snapshot().correction;
    Configure(api, token, "ifBandwidthHz", 10000.0);
    RequireMode(api, "subset", 3U, 0U);
    const auto changedBandwidth = Request(api, "/api/v1/calibration").json;
    Require(fixture.service.Snapshot().correction == subset &&
                Field(Field(changedBandwidth, "applicability"), "warnings").array.front().string == "ifbwChanged",
        "IFBW change retains the prepared coefficients with a persistent explicit warning");
}

void AtomicActivationAndRange(HttpApi &api, VnaServiceTest::Fixture &fixture, const std::string &token)
{
    Configure(api, token, "points", 4.0);
    RequireMode(api, "interpolated", 2U, 2U);
    const auto before = fixture.service.Snapshot();
    Failed(api, Request(api, "/api/v1/calibration", Activate(api, "2", "foreign-device"), token), token);
    Require(fixture.service.Snapshot().correction == before.correction &&
                fixture.service.Calibration().selectedSetId == before.calibration.selectedSetId &&
                fixture.service.Calibration().revision == before.calibration.revision &&
                fixture.service.Calibration().effective,
        "Atomic apply failure cannot disable or replace the committed calibration");
    Configure(api, token, "stopHz", 10005000.0);
    const auto blocked = Request(api, "/api/v1/calibration").json;
    Require(Field(blocked, "enabled").boolean && !Field(blocked, "effective").boolean &&
                Field(Field(blocked, "applicability"), "reasonCode").string == "outOfRange",
        "Any frequency outside the original range blocks all correction while retaining user intent");
    Failed(api, Request(api, "/api/v1/calibration", Activate(api, "1", "out-of-range"), token), token);
    Configure(api, token, "stopHz", 10004000.0);
    RequireMode(api, "interpolated", 2U, 2U);
    Require(fixture.service.Snapshot().correction == before.correction,
        "Returning to a prepared valid sweep restores the immutable cached correction");
}

void PreparedStatesAndAtomicActivation()
{
    VnaServiceTest::Fixture fixture(Profile());
    RestoreSources(fixture);
    HttpApi api(fixture.service);
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    CatalogMetadata(api);
    PreparedStatesAndBandwidth(api, fixture, token);
    AtomicActivationAndRange(api, fixture, token);
}
}  // 匿名命名空间

int main()
{
    try {
        PreparedStatesAndAtomicActivation();
        std::cout << "PASS calibration applicability HTTP states and atomic activation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
