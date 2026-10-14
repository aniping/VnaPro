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
#include "vna_core/frame_layout.h"
#include "vna_service/instrument_profile.h"
#include "web/calibration_preview_json.h"
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

void FullSavedStandardPreviewPreservesAllSamples(unsigned points)
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = points;
    VnaServiceTest::Fixture fixture(profile);
    HttpApi api(fixture.service);
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    auto begin = Action(api, "begin", "full-preview-begin");
    begin["method"] = Json("onePort");
    begin["port1"] = Json(1U);
    Complete(api, Request(api, "/api/v1/calibration", begin, token), token);
    const auto cal = Request(api, "/api/v1/calibration").json;
    const auto sessionId = Field(cal, "sessionId").string;
    const auto stepId = Field(Field(cal, "steps").array.front(), "id").string;
    std::vector<double> iq(points * VnaCore::IQ_VALUES_PER_POINT, 0.0);
    for (std::size_t point = 0U; point < points; ++point) {
        iq[point * VnaCore::IQ_VALUES_PER_POINT] = 1.0;
        iq[point * VnaCore::IQ_VALUES_PER_POINT + 2U] = 0.01;
    }
    // 旧两千零一点均匀抽点会漏掉原始下标 2，必须保留这个单点窄峰。
    iq[2U * VnaCore::IQ_VALUES_PER_POINT + 2U] = 0.5;
    iq[2U * VnaCore::IQ_VALUES_PER_POINT + 3U] = 0.25;
    iq[3U * VnaCore::IQ_VALUES_PER_POINT + 2U] = 0.0;
    fixture.device->SetSamples(std::move(iq));
    auto capture = Action(api, "capture", "full-preview-capture");
    capture["sessionId"] = Json(sessionId);
    capture["stepId"] = Json(stepId);
    Complete(api, Request(api, "/api/v1/calibration", capture, token), token);
    const auto before = Request(api, "/api/v1/status").json;
    const auto path = "/api/v1/calibration/preview?sessionId=" + sessionId + "&stepId=" + stepId;
    const auto response = Request(api, path);
    Require(response.status == 200U && Field(response.json, "available").boolean &&
                Field(response.json, "originalPointCount").number == points &&
                Field(response.json, "displayPointCount").number == points &&
                !Field(response.json, "decimated").boolean,
        "校准标准预览必须保留完整扫频点数");
    const auto &preview = response.json;
    for (const auto key : {"frequencyHz", "sampleIndexes", "real", "imag", "valid", "dbValues", "dbStates",
             "phaseValues", "phaseStates"}) {
        Require(Field(preview, key).type == Json::Type::ARRAY && Field(preview, key).array.size() == points,
            std::string("校准标准预览数组必须完整：") + key);
    }
    const auto saved = fixture.service.CalibrationPreview(std::stoull(sessionId), stepId);
    for (std::size_t point = 0U; point < points; ++point) {
        Require(Field(preview, "sampleIndexes").array[point].number == point &&
                    Field(preview, "frequencyHz").array[point].number == saved.frequencyHz[point] &&
                    Field(preview, "valid").array[point].boolean == saved.trace.points[point].valid,
            "每个预览点都必须对应已确认标准的同一原始索引和频率");
        if (saved.trace.points[point].valid) {
            Require(Field(preview, "real").array[point].number == saved.trace.points[point].real &&
                        Field(preview, "imag").array[point].number == saved.trace.points[point].imag,
                "完整标准 IQ 不得被抽取或重采样");
        }
    }
    Require(Field(preview, "real").array[2U].number == 0.5 && Field(preview, "imag").array[2U].number == 0.25 &&
                std::abs(Field(preview, "dbValues").array[2U].number + 5.051499783199059) < 1e-12 &&
                std::abs(Field(preview, "phaseValues").array[2U].number - 26.56505117707799) < 1e-12,
        "窄峰的原始复数、幅度和相位均使用后端完整投影");
    Require(Field(preview, "dbValues").array[3U].type == Json::Type::NULL_VALUE &&
                Field(preview, "dbStates").array[3U].string == "negativeInfinity" &&
                Field(preview, "phaseStates").array[3U].string == "undefined",
        "完整预览保留零信号的显式状态");
    Require(Field(preview, "sessionId").string == sessionId && Field(preview, "stepId").string == stepId &&
                Field(preview, "runId").string == std::to_string(saved.step.runId) &&
                Field(preview, "frameSeq").string == std::to_string(saved.step.frameSeq) &&
                Field(preview, "calibrationRevision").string == std::to_string(saved.calibrationRevision),
        "大点数预览仍绑定已确认标准身份");
    Require(VnaWeb::StringifyJson(preview).size() < VnaWeb::JsonEncoder::DEFAULT_OUTPUT_LIMIT,
        "最大点数单条预览必须在普通 JSON 输出预算内完整编码");
    const auto after = Request(api, "/api/v1/status").json;
    for (const auto key :
        {"revision", "acquisitionRevision", "config", "measurements", "display", "calibration", "runId"}) {
        Require(VnaWeb::StringifyJson(Field(after, key)) == VnaWeb::StringifyJson(Field(before, key)),
            "读取完整预览不得变更仪器状态");
    }
}

void OversizedSavedStandardIsRejected()
{
    VnaService::CalibrationPreviewResult preview;
    preview.available = true;
    preview.trace.available = true;
    preview.frequencyHz.resize(VnaService::MAX_SWEEP_POINTS + 1U, 1e7);
    preview.trace.points.resize(preview.frequencyHz.size());
    bool rejected = false;
    try {
        VnaWeb::CalibrationPreviewJson::Encode(preview);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    Require(rejected, "超出仪器最大扫频点数的标准预览必须拒绝，不能静默截断");
}
}  // 匿名命名空间

int main()
{
    try {
        SavedStandardPreviewIsReadOnlyAndRaw();
        FullSavedStandardPreviewPreservesAllSamples(4000U);
        FullSavedStandardPreviewPreservesAllSamples(VnaService::MAX_SWEEP_POINTS);
        OversizedSavedStandardIsRejected();
        std::cout << "PASS raw calibration standard HTTP preview and immutable identities\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
