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
    const auto found = value.Find(key);
    Require(found != nullptr, "Missing preview field: " + key);
    return *found;
}

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    const auto separator = path.find('?');
    request.path = path.substr(0U, separator);
    request.query = separator == std::string::npos ? "" : path.substr(separator + 1U);
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    if (request.method == "POST") {
        request.body = VnaWeb::StringifyJson(body);
    }
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

void Complete(HttpApi &api, const HttpResponse &accepted, const std::string &token)
{
    Require(accepted.status == 202U, "Preview setup rejected: " + VnaWeb::StringifyJson(accepted.json));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto response =
            Request(api, "/api/v1/operations/" + Field(accepted.json, "operationId").string, Json(), token);
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", VnaWeb::StringifyJson(response.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Preview setup timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json Action(HttpApi &api, const std::string &kind, const std::string &requestId)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(requestId);
    body["action"] = Json(kind);
    body["expectedRevision"] = Field(Request(api, "/api/v1/calibration").json, "revision");
    return body;
}

void SavedStandardPreviewIsReadOnlyAndRaw()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    VnaServiceTest::Fixture fixture(profile);
    HttpApi api(fixture.service);
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    auto begin = Action(api, "begin", "preview-begin");
    begin["method"] = Json("onePort");
    begin["port1"] = Json(1U);
    Complete(api, Request(api, "/api/v1/calibration", begin, token), token);
    const auto cal = Request(api, "/api/v1/calibration").json;
    const auto sessionId = Field(cal, "sessionId").string;
    const auto stepId = Field(Field(cal, "steps").array.front(), "id").string;
    const auto path = "/api/v1/calibration/preview?sessionId=" + sessionId + "&stepId=" + stepId;
    const auto empty = Request(api, path).json;
    Require(!Field(empty, "available").boolean && Field(empty, "stepId").string == stepId && !empty.Find("real"),
        "Unmeasured standard must not generate a curve");
    Require(Request(api, path + "&stepId=" + stepId).status == 400U, "Duplicate preview field rejected");
    Require(Request(api, "/api/v1/calibration/preview?sessionId=0&stepId=" + stepId).status == 400U,
        "Zero session preview rejected");
    std::vector<double> iq(16U, 0.0);
    iq[0U] = 1.0;
    iq[2U] = 2.0;
    fixture.device->SetSamples(iq);
    auto capture = Action(api, "capture", "preview-capture");
    capture["sessionId"] = Json(sessionId);
    capture["stepId"] = Json(stepId);
    Complete(api, Request(api, "/api/v1/calibration", capture, token), token);
    const auto before = Request(api, "/api/v1/status").json;
    const auto preview = Request(api, path).json;
    Require(Field(preview, "available").boolean && Field(preview, "parameter").string == "S11" &&
                Field(preview, "real").array.front().number == 2.0 &&
                std::abs(Field(preview, "dbValues").array.front().number - 6.020599913279624) < 1e-12 &&
                Field(preview, "phaseValues").array.front().number == 0.0,
        "Preview must use confirmed raw data and backend dB/phase projection");
    Require(Field(preview, "calibrationRevision").string == Field(Field(before, "calibration"), "revision").string &&
                Field(preview, "ifBandwidthHz").number == profile.defaultIfBandwidthHz &&
                Field(preview, "runId").string ==
                    Field(Field(Field(before, "calibration"), "steps").array.front(), "runId").string,
        "Preview binds saved standard identity and stimulus");
    const auto after = Request(api, "/api/v1/status").json;
    for (const auto key :
        {"revision", "acquisitionRevision", "config", "measurements", "display", "calibration", "runId"}) {
        Require(VnaWeb::StringifyJson(Field(after, key)) == VnaWeb::StringifyJson(Field(before, key)),
            "Observer preview does not mutate instrument state");
    }
    iq[2U] = 0.0;
    fixture.device->SetSamples(iq);
    capture = Action(api, "capture", "preview-zero");
    capture["sessionId"] = Json(sessionId);
    capture["stepId"] = Json(stepId);
    Complete(api, Request(api, "/api/v1/calibration", capture, token), token);
    const auto zero = Request(api, path).json;
    Require(Field(zero, "dbValues").array.front().type == Json::Type::NULL_VALUE &&
                Field(zero, "dbStates").array.front().string == "negativeInfinity" &&
                Field(zero, "phaseStates").array.front().string == "undefined",
        "Zero raw standard keeps display states");
    auto cancel = Action(api, "cancel", "preview-cancel");
    cancel["sessionId"] = Json(sessionId);
    Complete(api, Request(api, "/api/v1/calibration", cancel, token), token);
    const auto stale = Request(api, path).json;
    Require(
        !Field(stale, "available").boolean && Field(stale, "stepId").string == stepId, "Old session cannot preview");
    VnaWeb::HttpRequest wrongMethod;
    wrongMethod.method = "DELETE";
    wrongMethod.path = "/api/v1/calibration";
    Require(api.Handle(wrongMethod).status == 405U, "Calibration writes require POST");
}
}  // 匿名命名空间

int main()
{
    try {
        SavedStandardPreviewIsReadOnlyAndRaw();
        std::cout << "PASS raw calibration standard HTTP preview and immutable identities\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
