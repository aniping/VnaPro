/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_history_test.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"
#include "web/calibration_json.h"
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
    const auto field = value.Find(key);
    Require(field != nullptr, "Missing calibration field: " + key);
    return *field;
}

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    const auto separator = path.find('?');
    request.path = path.substr(0U, separator);
    request.query = separator == std::string::npos ? "" : path.substr(separator + 1U);
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    if (request.method == "POST") {
        request.body = VnaWeb::StringifyJson(body, VnaWeb::CALIBRATION_JSON_BYTE_LIMIT);
    }
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

void Complete(HttpApi &api, const HttpResponse &accepted, const std::string &token)
{
    Require(accepted.status == 202U, "Calibration rejected: " + VnaWeb::StringifyJson(accepted.json));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto result =
            Request(api, "/api/v1/operations/" + Field(accepted.json, "operationId").string, Json(), token);
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", "Calibration operation failed: " + VnaWeb::StringifyJson(result.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "Calibration protocol timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json Action(HttpApi &api, const std::string &action, const std::string &id)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedRevision"] = Field(Request(api, "/api/v1/calibration").json, "revision");
    body["action"] = Json(action);
    return body;
}

VnaService::InstrumentProfile Profile()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    return profile;
}

Json BudgetDocument(const std::string &identity)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::TWO_PORT;
    data->port1 = 2U;
    data->port2 = 4U;
    for (unsigned index = 0U; index < VnaService::MAX_SWEEP_POINTS; ++index) {
        data->frequencyHz.push_back(10000000.0 + index * 1000.0);
    }
    const VnaCalibration::Complex value{0.12345678901234567, 0.12345678901234567};
    VnaCalibration::TwoPortTerms terms;
    terms.forwardReflection = {value, value, value};
    terms.reverseReflection = terms.forwardReflection;
    terms.forwardTransmission = {value, value, value};
    terms.reverseTransmission = terms.forwardTransmission;
    data->twoPortTerms.assign(data->frequencyHz.size(), terms);
    VnaService::CalibrationSet set;
    set.name = "Retained byte budget vector";
    set.deviceIdentity = identity;
    set.includeIsolation = true;
    set.ifBandwidthHz = 1000.0;
    set.powerDbm.assign(data->frequencyHz.size(), 0.0);
    set.data = data;
    const auto document = VnaWeb::CalibrationJson::Export(set);
    Require(VnaWeb::StringifyJson(document, VnaWeb::CALIBRATION_JSON_BYTE_LIMIT).size() > 4U * 1024U * 1024U,
        "Budget regression uses a real large coefficient document");
    return document;
}

void FillHistoryBudget(
    HttpApi &api, const std::string &token, const Json &document, Json &firstRequest, std::string &firstOperation)
{
    for (unsigned index = 0U; index < VnaService::MAX_CALIBRATION_SETS; ++index) {
        auto body = Action(api, "import", "budget-" + std::to_string(index));
        body["data"] = document;
        const auto response = Request(api, "/api/v1/calibration", body, token);
        if (response.status == 429U) {
            Require(Field(Field(response.json, "error"), "code").string == "retry_history_full",
                "Budget rejected explicitly");
            return;
        }
        Complete(api, response, token);
        if (index == 0U) {
            firstRequest = body;
            firstOperation = Field(response.json, "operationId").string;
        }
        auto remove = Action(api, "delete", "budget-delete-" + std::to_string(index));
        remove["setId"] = Field(Field(Request(api, "/api/v1/calibration").json, "sets").array.front(), "id");
        Complete(api, Request(api, "/api/v1/calibration", remove, token), token);
    }
    throw std::runtime_error("Large canonical request history must not grow by sessions times file limit");
}

void AdvancePastExpiry(HttpApi &api, const std::string &token, HttpApi::Clock::time_point &clock)
{
    auto renew = Json::MakeObject();
    renew["sessionId"] = Json(token);
    for (unsigned count = 0U; count < 3U; ++count) {
        clock += std::chrono::seconds(21);
        Require(Request(api, "/api/v1/control/claim", renew).status == 200U, "Controller retains lease across expiry");
    }
}
}  // 匿名命名空间

namespace VnaCalibrationProtocolTest {
void CheckHistoryBudget()
{
    VnaServiceTest::Fixture fixture(Profile());
    auto clock = HttpApi::Clock::now();
    HttpApi api(fixture.service, [&clock] {
        return clock;
    });
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    const auto document = BudgetDocument(fixture.service.Identity());
    Json firstRequest;
    std::string firstOperation;
    FillHistoryBudget(api, token, document, firstRequest, firstOperation);
    Require(
        Field(Request(api, "/api/v1/calibration", firstRequest, token).json, "operationId").string == firstOperation,
        "Full byte budget still returns retained idempotent operation");
    AdvancePastExpiry(api, token, clock);
    Require(Request(api, "/api/v1/calibration", firstRequest, token).status == 410U,
        "Expired retry keeps a tombstone without retaining coefficient bytes");
    auto recovered = Action(api, "import", "budget-after-expiry");
    recovered["data"] = document;
    Complete(api, Request(api, "/api/v1/calibration", recovered, token), token);
}
}  // 命名空间 VnaCalibrationProtocolTest
