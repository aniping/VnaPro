/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_protocol_test_support.h"

#include <chrono>
#include <stdexcept>
#include <thread>

#include "web/calibration_json.h"
#include "web/json_encoder.h"

namespace VnaCalibrationProtocolTest {
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

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body, const std::string &token)
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

}  // 命名空间 VnaCalibrationProtocolTest
