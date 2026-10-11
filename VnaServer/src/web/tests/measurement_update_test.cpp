/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fixture.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using VnaServiceTest::Fixture;
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;
constexpr std::chrono::seconds WAIT_TIMEOUT(3);
constexpr std::chrono::milliseconds POLL_INTERVAL(1);
const std::string MEASUREMENT_PATH = "/api/v1/measurements/Trc1";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &name)
{
    const auto found = value.Find(name);
    Require(found != nullptr, "Missing JSON field: " + name);
    return *found;
}

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const std::string &body = "",
    const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "POST", "/api/v1/control/claim", "{}");
    Require(response.status == 200U, "Claim succeeds");
    return Field(response.json, "sessionId").string;
}

std::string Accepted(const HttpResponse &response)
{
    Require(response.status == 202U, "Expected accepted operation, received " + std::to_string(response.status));
    return Field(response.json, "operationId").string;
}

void Complete(HttpApi &api, const std::string &id, const std::string &token, bool success = true)
{
    const auto deadline = HttpApi::Clock::now() + WAIT_TIMEOUT;
    for (;;) {
        const auto response = Request(api, "GET", "/api/v1/operations/" + id, "", token);
        Require(response.status == 200U, "Operation belongs to controller");
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == (success ? "completed" : "failed"), "Operation completion state");
            return;
        }
        Require(HttpApi::Clock::now() < deadline, "Operation completes before timeout");
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

Json UpdateBody(std::uint64_t revision, const std::string &requestId, unsigned receiver = 2U, unsigned source = 2U)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(requestId);
    body["expectedRevision"] = Json(std::to_string(revision));
    body["kind"] = Json("sParameter");
    body["receiverKind"] = Json("b");
    body["receiverPort"] = Json(receiver);
    body["sourcePort"] = Json(source);
    return body;
}

void PrepareS21(Fixture &fixture, HttpApi &api, const std::string &token)
{
    // 显式建立唯一 S21，兼容保留低频黄金向量的独立测试配置。
    const auto measurements = fixture.service.Settings().measurements;
    for (const auto &measurement : measurements) {
        auto body = Json::MakeObject();
        body["requestId"] = Json("delete-" + measurement.name);
        body["expectedRevision"] = Json(std::to_string(fixture.service.Settings().revision));
        const auto id = Accepted(
            Request(api, "DELETE", "/api/v1/measurements/" + measurement.name, VnaWeb::StringifyJson(body), token));
        Complete(api, id, token);
    }
    auto body = UpdateBody(fixture.service.Settings().revision, "define-S21", 2U, 1U);
    body["name"] = Json("Trc1");
    const auto id = Accepted(Request(api, "POST", "/api/v1/measurements", VnaWeb::StringifyJson(body), token));
    Complete(api, id, token);
}

void Single(HttpApi &api, const std::string &token, const std::string &requestId)
{
    const auto body = "{\"requestId\":\"" + requestId + "\",\"action\":\"single\"}";
    Complete(api, Accepted(Request(api, "POST", "/api/v1/operations", body, token)), token);
}

void VerifySwitched(Fixture &fixture, HttpApi &api, const std::string &token, std::uint64_t revision)
{
    const auto &settings = fixture.service.Settings();
    Require(settings.measurements.size() == 1U && settings.measurements.front().name == "Trc1" &&
                settings.measurements.front().Parameter() == "S22" && settings.selectedMeasurement == "Trc1",
        "Update preserves sole trace name, position and selection");
    Require(settings.revision == revision + 1U, "Update commits exactly one revision");
    const auto oldFrame = Request(api, "GET", "/api/v1/frame");
    Require(Field(oldFrame.json, "available").boolean && !Field(oldFrame.json, "current").boolean,
        "Previous S21 frame becomes stale");
    Single(api, token, "new-S22-frame");
    const auto frame = Request(api, "GET", "/api/v1/frame");
    const auto &trace = Field(frame.json, "traces").array.front();
    Require(Field(frame.json, "current").boolean && Field(trace, "name").string == "Trc1" &&
                Field(trace, "parameter").string == "S22" && Field(trace, "available").boolean,
        "New scan publishes current S22 under the retained trace name");
}

void UpdateAndRetry()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    PrepareS21(fixture, api, token);
    Single(api, token, "old-S21-frame");
    Require(Field(Request(api, "GET", "/api/v1/frame").json, "current").boolean, "Original S21 frame is current");
    const auto revision = fixture.service.Settings().revision;
    const auto body = VnaWeb::StringifyJson(UpdateBody(revision, "switch-S22"));
    const auto id = Accepted(Request(api, "PUT", MEASUREMENT_PATH, body, token));
    Require(Accepted(Request(api, "PUT", MEASUREMENT_PATH, body, token)) == id, "Pending retry has same operation");
    Complete(api, id, token);
    Require(Accepted(Request(api, "PUT", MEASUREMENT_PATH, body, token)) == id,
        "Completed retry precedes the stale revision check");
    Require(
        Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(UpdateBody(revision, "switch-S22", 1U, 1U)), token)
                .status == 409U,
        "Changed payload cannot reuse requestId");
    Require(Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(UpdateBody(revision, "stale-revision")), token)
                    .status == 409U,
        "New stale request is rejected");
    VerifySwitched(fixture, api, token, revision);
}

void RejectedUpdatesKeepCatalog()
{
    auto profile = VnaService::InstrumentProfile();
    profile.portCount = 2U;
    Fixture fixture(profile);
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    PrepareS21(fixture, api, token);
    const auto revision = fixture.service.Settings().revision;
    const auto valid = VnaWeb::StringifyJson(UpdateBody(revision, "read-only"));
    Require(Request(api, "PUT", "/api/v1/status", "{}").status == 405U,
        "Unsupported PUT route is rejected before session authentication");
    Require(Request(api, "PUT", MEASUREMENT_PATH, valid).status == 403U, "Read-only browser cannot update");
    const auto invalidPort = Accepted(
        Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(UpdateBody(revision, "invalid-port", 3U)), token));
    Complete(api, invalidPort, token, false);
    const auto invalidSource = Accepted(Request(
        api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(UpdateBody(revision, "invalid-source", 2U, 3U)), token));
    Complete(api, invalidSource, token, false);
    Require(Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(UpdateBody(revision, "zero-port", 0U)), token)
                    .status == 400U,
        "Non-positive port is malformed input");
    auto incomplete = UpdateBody(revision, "missing-kind");
    incomplete.object.erase("kind");
    Require(Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(incomplete), token).status == 400U,
        "Complete update requires measurement kind");
    auto rename = UpdateBody(revision, "rename-forbidden");
    rename["name"] = Json("Renamed");
    Require(Request(api, "PUT", MEASUREMENT_PATH, VnaWeb::StringifyJson(rename), token).status == 400U,
        "Body cannot rename a trace");
    const auto missing = Accepted(Request(api, "PUT", "/api/v1/measurements/Missing",
        VnaWeb::StringifyJson(UpdateBody(revision, "missing-name")), token));
    Complete(api, missing, token, false);
    Require(Request(api, "POST", MEASUREMENT_PATH, valid, token).status == 405U,
        "Individual measurement route rejects POST");
    Require(
        Request(api, "PUT", "/api/v1/config", valid, token).status == 405U, "Configuration retains POST-only contract");
    const auto &settings = fixture.service.Settings();
    Require(settings.revision == revision && settings.measurements.size() == 1U &&
                settings.measurements.front().name == "Trc1" && settings.measurements.front().Parameter() == "S21",
        "Every rejected update preserves configuration and catalog");
    Require(fixture.service.ErrorCount() == 0U, "HTTP failures do not enter SCPI error queue");
}
}  // 匿名命名空间

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"measurement-update-and-retry", UpdateAndRetry},
        {"measurement-update-rejections", RejectedUpdatesKeepCatalog}};
    unsigned failures = 0U;
    for (const auto &test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures == 0U ? 0 : 1;
}
