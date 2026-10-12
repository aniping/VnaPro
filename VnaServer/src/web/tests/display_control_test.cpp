/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "fixture.h"
#include "scpi/scpi_engine.h"
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

const Json &Field(const Json &value, const std::string &field)
{
    const auto item = value.Find(field);
    Require(item != nullptr, "Missing field: " + field);
    return *item;
}

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const Json &body = Json(),
    const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    request.headers["x-vna-session"] = token;
    return api.Handle(request);
}

void Complete(HttpApi &api, const HttpResponse &accepted, const std::string &token, bool success = true)
{
    Require(accepted.status == 202U, "Operation accepted: " + VnaWeb::StringifyJson(accepted.json));
    const auto path = "/api/v1/operations/" + Field(accepted.json, "operationId").string;
    const auto deadline = HttpApi::Clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto response = Request(api, "GET", path, Json(), token);
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == (success ? "completed" : "failed"),
                "Operation completion: " + VnaWeb::StringifyJson(response.json));
            return;
        }
        Require(HttpApi::Clock::now() < deadline, "Operation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json Change(HttpApi &api, const std::string &action, const std::string &id)
{
    const auto status = Request(api, "GET", "/api/v1/status");
    Json body = Json::MakeObject();
    body["action"] = Json(action);
    body["requestId"] = Json(id);
    body["traceName"] = Json("Trc1");
    body["expectedDisplayRevision"] = Field(Field(status.json, "display"), "revision");
    return body;
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &command)
{
    Require(engine.SubmitLine(command), "SCPI accepts complete line");
    const auto deadline = HttpApi::Clock::now() + std::chrono::seconds(3);
    while (engine.HasPending()) {
        engine.Poll();
        Require(HttpApi::Clock::now() < deadline, "SCPI drain timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return engine.TakeOutput();
}

void VerifyObserver(HttpApi &controller, HttpApi &observer)
{
    const auto first = Field(Request(controller, "GET", "/api/v1/status").json, "display");
    const auto second = Field(Request(observer, "GET", "/api/v1/status").json, "display");
    Require(VnaWeb::StringifyJson(first) == VnaWeb::StringifyJson(second),
        "Independent observers share every display field");
}

void HttpWritesPersistAndScpiReadsSameState()
{
    VnaServiceTest::Fixture fixture;
    HttpApi controller(fixture.service);
    HttpApi observer(fixture.service);
    const auto claim = Request(controller, "POST", "/api/v1/control/claim", Json::MakeObject());
    const auto token = Field(claim.json, "sessionId").string;
    auto body = Change(controller, "format", "format-real");
    body["format"] = Json("real");
    const auto accepted = Request(controller, "POST", "/api/v1/display", body, token);
    Complete(controller, accepted, token);
    Require(Field(Request(controller, "POST", "/api/v1/display", body, token).json, "operationId").string ==
                Field(accepted.json, "operationId").string,
        "Retry returns original operation before stale revision check");
    body["requestId"] = Json("stale-format");
    Require(Request(controller, "POST", "/api/v1/display", body, token).status == 409U,
        "Fresh request with old display revision cannot overwrite state");
    body = Change(controller, "scale", "scale-fixed");
    body["min"] = Json(-3.0);
    body["max"] = Json(7.0);
    body["referencePosition"] = Json(8.0);
    Complete(controller, Request(controller, "POST", "/api/v1/display", body, token), token);
    body = Change(controller, "layout", "layout-grid");
    body["layout"] = Json("grid");
    Complete(controller, Request(controller, "POST", "/api/v1/display", body, token), token);
    VerifyObserver(controller, observer);
    HttpApi refreshed(fixture.service);
    VerifyObserver(controller, refreshed);
    Require(Request(observer, "POST", "/api/v1/display", body).status == 403U, "Observer cannot write shared state");
    Request(controller, "POST", "/api/v1/control/release", Json::MakeObject(), token);
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "CALC1:FORM?") == "REAL\n", "SCPI sees HTTP format");
    Require(std::stod(Command(scpi, "DISP:WIND1:TRAC1:Y:PDIV?")) == 1.0 &&
                std::stod(Command(scpi, "DISP:WIND1:TRAC1:Y:RLEV?")) == 5.0,
        "SCPI sees HTTP scale and reference");
    Command(scpi, "CALC1:FORM PHAS;:DISP:WIND1:TRAC1:Y:PDIV 20");
    VerifyObserver(controller, observer);
    const auto display = Field(Request(observer, "GET", "/api/v1/status").json, "display");
    const auto &presentation = Field(Field(display, "presentations"), "Trc1");
    Require(Field(presentation, "format").string == "phase" &&
                Field(Field(presentation, "scale"), "max").number - Field(Field(presentation, "scale"), "min").number ==
                    200.0,
        "HTTP observers read SCPI writes from the same display workspace");
}

void FullFrameMarkerSurvivesDecimation()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto claim = Request(api, "POST", "/api/v1/control/claim", Json::MakeObject());
    const auto token = Field(claim.json, "sessionId").string;
    Json config = Json::MakeObject();
    config["requestId"] = Json("large-sweep");
    config["expectedRevision"] = Json(std::to_string(fixture.service.Settings().revision));
    config["points"] = Json(10001U);
    Complete(api, Request(api, "POST", "/api/v1/config", config, token), token);
    Json single = Json::MakeObject();
    single["requestId"] = Json("single");
    single["action"] = Json("single");
    Complete(api, Request(api, "POST", "/api/v1/operations", single, token), token);
    auto marker = Change(api, "marker", "original-marker");
    marker["enabled"] = Json(true);
    marker["sampleIndex"] = Json(1001U);
    marker["acquisitionRevision"] = Json(std::to_string(fixture.service.Settings().acquisitionRevision));
    Complete(api, Request(api, "POST", "/api/v1/display", marker, token), token);
    const auto frame = Request(api, "GET", "/api/v1/frame");
    const auto &indexes = Field(frame.json, "sampleIndexes").array;
    Require(indexes.size() == 2001U && indexes.front().number == 0.0 && indexes.back().number == 10000.0,
        "HTTP sends original sample indexes alongside the physical frequency axis");
    for (const auto &index : indexes) {
        Require(index.number != 1001.0, "Chosen marker is intentionally omitted by transmission decimation");
    }
    const auto &trace = Field(frame.json, "traces").array.front();
    const auto &readout = Field(Field(trace, "projection"), "marker");
    Require(Field(readout, "available").boolean && Field(readout, "sampleIndex").number == 1001.0 &&
                Field(readout, "frequencyHz").number == fixture.service.Snapshot().core.frame->plan.frequencyHz[1001U],
        "Marker readout remains exact outside the displayed sample array");
    Require(Field(frame.json, "displayRevision").string == std::to_string(fixture.service.Display().revision),
        "Projection carries the exact display settings revision");
    Request(api, "POST", "/api/v1/control/release", Json::MakeObject(), token);
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(std::stod(Command(scpi, "CALC1:MARK1:X?")) == Field(readout, "frequencyHz").number,
        "SCPI reads the same full-frame Marker coordinate");
}
}  // 匿名命名空间

int main()
{
    try {
        HttpWritesPersistAndScpiReadsSameState();
        FullFrameMarkerSurvivesDecimation();
        std::cout << "PASS HTTP/SCPI 共享状态、刷新、权限、CAS 和原始 Marker\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
