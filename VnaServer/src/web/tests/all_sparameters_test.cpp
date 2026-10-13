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

const Json &Field(const Json &value, const std::string &name)
{
    const auto *field = value.Find(name);
    Require(field != nullptr, "整组合同缺少字段：" + name);
    return *field;
}

HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    request.path = path;
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

Json Status(HttpApi &api)
{
    const auto response = Request(api, "/api/v1/status");
    Require(response.status == 200U, "观察者不能读取状态");
    return response.json;
}

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "/api/v1/control/claim", Json::MakeObject());
    Require(response.status == 200U, "无法取得控制权");
    return Field(response.json, "sessionId").string;
}

void Finish(HttpApi &api, const HttpResponse &accepted, const std::string &token, bool success = true)
{
    if (accepted.status != 202U) {
        Require(!success && accepted.status == 409U, "整组事务未正确受理：" + VnaWeb::StringifyJson(accepted.json));
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const auto path = "/api/v1/operations/" + Field(accepted.json, "operationId").string;
    for (;;) {
        const auto response = Request(api, path, Json(), token);
        Require(response.status == 200U, "整组结果无法读回");
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(
                state == (success ? "completed" : "failed"), "整组结果错误：" + VnaWeb::StringifyJson(response.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline, "整组事务超时");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json Action(HttpApi &api, const std::string &id)
{
    const auto status = Status(api);
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedRevision"] = Field(status, "revision");
    body["expectedDisplayRevision"] = Field(Field(status, "display"), "revision");
    body["confirmed"] = Json(true);
    return body;
}

std::string SavedState(const Json &status)
{
    std::string result;
    for (const auto &name : {"revision", "acquisitionRevision", "config", "measurements", "display"}) {
        result += VnaWeb::StringifyJson(Field(status, name));
    }
    return result;
}

void MatrixReplacesOldDefinitionsAndCanRepeat()
{
    for (unsigned ports : {2U, 4U}) {
        VnaService::InstrumentProfile profile;
        profile.portCount = ports;
        VnaServiceTest::Fixture fixture(profile);
        HttpApi writer(fixture.service);
        HttpApi observer(fixture.service);
        const auto token = Claim(writer);
        auto old = Json::MakeObject();
        old["requestId"] = Json("define-old-raw");
        old["expectedRevision"] = Field(Status(writer), "revision");
        old["name"] = Json("OldRaw");
        old["parameter"] = Json("a1,1");
        Finish(writer, Request(writer, "/api/v1/measurements", old, token), token);
        const auto before = Status(writer);
        for (unsigned repeat = 0U; repeat < 2U; ++repeat) {
            Finish(writer,
                Request(writer, "/api/v1/measurement-actions/all-sparameters",
                    Action(writer, "replace-all-" + std::to_string(repeat)), token),
                token);
            const auto after = Status(observer);
            const auto &measurements = Field(after, "measurements").array;
            const auto &display = Field(after, "display");
            const auto &diagrams = Field(display, "diagrams").array;
            Require(
                measurements.size() == ports * ports && diagrams.size() == ports * ports &&
                    Field(display, "selectedMeasurement").string == "Trc1" &&
                    Field(display, "selectedDiagram").number == 1.0 && Field(display, "gridColumns").number == ports &&
                    Field(display, "gridRows").number == ports &&
                    VnaWeb::StringifyJson(Field(after, "config")) == VnaWeb::StringifyJson(Field(before, "config")) &&
                    std::stoull(Field(after, "revision").string) ==
                        std::stoull(Field(before, "revision").string) + repeat + 1U,
                "整组确认必须替换旧定义、精确创建 n² 图窗且不修改扫描配置");
            for (unsigned index = 0U; index < ports * ports; ++index) {
                const auto name = "Trc" + std::to_string(index + 1U);
                const auto parameter = "S" + std::to_string(index / ports + 1U) + std::to_string(index % ports + 1U);
                Require(Field(measurements[index], "name").string == name &&
                            Field(measurements[index], "parameter").string == parameter &&
                            Field(diagrams[index], "measurementNames").array.size() == 1U &&
                            Field(diagrams[index], "measurementNames").array.front().string == name,
                    "整组顺序必须按接收端口分行、源端口分列，每条独立图窗");
            }
            Require(VnaWeb::StringifyJson(Field(Status(writer), "display")) == VnaWeb::StringifyJson(display),
                "观察者和刷新读回必须共享整组结果");
        }
    }
}

void InvalidConfirmationsAndStaleVersionsAreAtomic()
{
    VnaServiceTest::Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    const auto before = SavedState(Status(api));
    const auto unchanged = [&] {
        Require(SavedState(Status(api)) == before, "整组拒绝不能改变任何保存状态");
    };
    auto missing = Action(api, "missing-confirmed");
    missing.object.erase("confirmed");
    Require(Request(api, "/api/v1/measurement-actions/all-sparameters", missing, token).status == 400U,
        "缺少破坏性确认必须拒绝");
    unchanged();
    for (const auto &value : {Json(), Json(false), Json(1U), Json("true")}) {
        auto body = Action(api, "bad-confirmed-" + VnaWeb::StringifyJson(value));
        body["confirmed"] = value;
        Require(Request(api, "/api/v1/measurement-actions/all-sparameters", body, token).status == 400U,
            "confirmed 必须是布尔 true");
        unchanged();
    }
    for (const auto &name : {"parameter", "sourceName", "diagram", "newDiagram", "portCount"}) {
        auto body = Action(api, std::string("unknown-field-") + name);
        body[name] = Json("S11");
        Require(Request(api, "/api/v1/measurement-actions/all-sparameters", body, token).status == 400U,
            "整组请求禁止客户端覆盖端口、参数或图窗策略");
        unchanged();
    }
    for (const auto &name : {"expectedRevision", "expectedDisplayRevision"}) {
        auto body = Action(api, std::string("stale-all-") + name);
        body[name] = Json("0");
        Finish(api, Request(api, "/api/v1/measurement-actions/all-sparameters", body, token), token, false);
        unchanged();
    }
    Require(Request(api, "/api/v1/measurement-actions/all-sparameters", Action(api, "no-control")).status == 403U,
        "无控制权观察者不能替换整组");
    unchanged();
}
}  // 匿名命名空间

int main()
{
    try {
        MatrixReplacesOldDefinitionsAndCanRepeat();
        InvalidConfirmationsAndStaleVersionsAreAtomic();
        std::cout << "PASS HTTP 整组替换与确认、双版本合同\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
