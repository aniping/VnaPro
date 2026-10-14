/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {

using VnaServiceTest::Fixture;
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;

constexpr std::chrono::seconds WAIT_TIMEOUT(3);

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &name)
{
    const auto field = value.Find(name);
    Require(field != nullptr, "图窗合同缺少字段：" + name);
    return *field;
}

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const Json &body = Json(),
    const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

Json Status(HttpApi &api)
{
    const auto response = Request(api, "GET", "/api/v1/status");
    Require(response.status == 200U, "观察窗口不能读取仪器状态");
    return response.json;
}

Json Display(HttpApi &api)
{
    return Field(Status(api), "display");
}

const Json &Presentation(const Json &display, const std::string &name)
{
    return Field(Field(display, "presentations"), name);
}

const Json &Diagram(const Json &display, unsigned number)
{
    for (const auto &diagram : Field(display, "diagrams").array) {
        if (Field(diagram, "number").number == number) {
            return diagram;
        }
    }
    throw std::runtime_error("后端没有返回预期图窗");
}

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "POST", "/api/v1/control/claim", Json::MakeObject());
    Require(response.status == 200U, "无法取得写控制权");
    return Field(response.json, "sessionId").string;
}

void Wait(const std::function<bool()> &condition)
{
    const auto deadline = HttpApi::Clock::now() + WAIT_TIMEOUT;
    while (!condition()) {
        Require(HttpApi::Clock::now() < deadline, "后端事务等待超时");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Finish(HttpApi &api, const HttpResponse &accepted, const std::string &token, bool success = true)
{
    if (accepted.status != 202U) {
        Require(
            !success && accepted.status == 409U, "图窗事务没有受理或明确拒绝：" + VnaWeb::StringifyJson(accepted.json));
        return;
    }
    const auto path = "/api/v1/operations/" + Field(accepted.json, "operationId").string;
    Wait([&] {
        const auto response = Request(api, "GET", path, Json(), token);
        Require(response.status == 200U, "无法读取原事务结果");
        const auto state = Field(response.json, "state").string;
        if (state == "pending") {
            return false;
        }
        Require(
            state == (success ? "completed" : "failed"), "事务完成结果错误：" + VnaWeb::StringifyJson(response.json));
        return true;
    });
}

Json DisplayChange(HttpApi &api, const std::string &action, const std::string &id, const std::string &name = "")
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedDisplayRevision"] = Field(Display(api), "revision");
    body["action"] = Json(action);
    if (!name.empty()) {
        body["traceName"] = Json(name);
    }
    return body;
}

Json TraceAction(HttpApi &api, const std::string &id)
{
    const auto status = Status(api);
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    body["expectedRevision"] = Field(status, "revision");
    body["expectedDisplayRevision"] = Field(Field(status, "display"), "revision");
    return body;
}

void Apply(HttpApi &api, Json body, const std::string &token)
{
    Finish(api, Request(api, "POST", "/api/v1/display", body, token), token);
}

struct Creation final {
    bool clone = false;
    bool newDiagram = false;
    std::string source;
};

std::string Create(HttpApi &api, const std::string &token, const std::string &id, const Creation &options)
{
    auto body = TraceAction(api, id);
    body["newDiagram"] = Json(options.newDiagram);
    if (options.clone && !options.source.empty()) {
        body["sourceName"] = Json(options.source);
    }
    Finish(api,
        Request(
            api, "POST", "/api/v1/measurement-actions/" + std::string(options.clone ? "clone" : "add"), body, token),
        token);
    return Field(Display(api), "selectedMeasurement").string;
}

void DeleteDiagram(HttpApi &api, const std::string &token, unsigned number, const std::string &id, bool success = true)
{
    auto body = TraceAction(api, id);
    body["diagram"] = Json(number);
    Finish(api, Request(api, "POST", "/api/v1/measurement-actions/delete-diagram", body, token), token, success);
}

void VerifyShared(HttpApi &writer, HttpApi &observer)
{
    Require(VnaWeb::StringifyJson(Display(writer)) == VnaWeb::StringifyJson(Display(observer)),
        "独立观察窗口没有读取同一后端图窗和标记状态");
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &command)
{
    Require(engine.SubmitLine(command), "SCPI 查询没有受理");
    Wait([&] {
        engine.Poll();
        return !engine.HasPending();
    });
    return engine.TakeOutput();
}

void SetSourceSettings(
    HttpApi &api, const std::string &token, const std::string &name, const std::string &idPrefix = "source")
{
    auto format = DisplayChange(api, "format", idPrefix + "-phase", name);
    format["format"] = Json("phase");
    Apply(api, format, token);
    auto scale = DisplayChange(api, "scale", idPrefix + "-scale", name);
    scale["min"] = Json(-11.0);
    scale["max"] = Json(29.0);
    Apply(api, scale, token);
    auto marker = DisplayChange(api, "marker", idPrefix + "-marker-two", name);
    marker["markerId"] = Json(2U);
    marker["enabled"] = Json(true);
    marker["frequencyHz"] = Json(12e6);
    marker["acquisitionRevision"] = Field(Status(api), "acquisitionRevision");
    Apply(api, marker, token);
}

void CloneCopiesSettingsAndDefaultAddUsesNewSettings()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto source = Field(Display(writer), "selectedMeasurement").string;
    SetSourceSettings(writer, token, source);
    const auto copied = Create(writer, token, "copy-to-new-diagram", {true, true, source});
    const auto afterCopy = Display(observer);
    Require(copied != source && Field(afterCopy, "selectedDiagram").number == 2.0 &&
                Field(Diagram(afterCopy, 2U), "measurementNames").array.front().string == copied,
        "复制到新图窗没有追加并激活独立迹线");
    const auto &original = Presentation(afterCopy, source);
    const auto &copy = Presentation(afterCopy, copied);
    Require(Field(copy, "format").string == "phase" && Field(Field(copy, "scale"), "min").number == -11.0 &&
                VnaWeb::StringifyJson(Field(copy, "markers")) == VnaWeb::StringifyJson(Field(original, "markers")),
        "Clone 没有复制活动迹线的尺度或标记集合");
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "DISP:WIND2:STAT?") == "1\n" && Command(scpi, "DISP:WIND2:CAT?") == "\"1\"\n" &&
                Command(scpi, "DISP:WIND2:SIZE?") == "NORM\n" && Command(scpi, "CALC1:FORM?") == "PHAS\n" &&
                Command(scpi, "CALC1:MARK2:STAT?") == "1\n",
        "SCPI 观察者没有共享 HTTP 复制后的图窗、格式与标记");
    Require(std::stod(Command(scpi, "DISP:WIND2:TRAC1:Y:SCAL:PDIV?")) == 4.0,
        "SCPI 图窗局部槽的尺度没有读取复制后的后端值");
    auto marker = DisplayChange(writer, "marker", "copy-marker-off", copied);
    marker["markerId"] = Json(2U);
    marker["enabled"] = Json(false);
    Apply(writer, marker, token);
    const auto independent = Display(observer);
    Require(Field(Field(Presentation(independent, source), "markers").array[1], "enabled").boolean,
        "目标迹线的值语义标记修改了源集合");
    const auto created = Create(writer, token, "default-add-diagram", {false, true, ""});
    const auto afterAdd = Display(observer);
    const auto &defaults = Presentation(afterAdd, created);
    Require(Field(afterAdd, "selectedDiagram").number == 3.0 && Field(defaults, "format").string == "db" &&
                Field(Field(defaults, "scale"), "min").number == -90.0 &&
                !Field(Field(defaults, "marker"), "enabled").boolean,
        "Display 的默认新增错误地复制旧相位、尺度或标记");
    VerifyShared(writer, observer);
    HttpApi refreshed(fixture.service);
    VerifyShared(writer, refreshed);
}

void DeleteDiagramRemovesDefinitionsAndMarkers()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto removed = Create(writer, token, "create-removed", {true, true, ""});
    SetSourceSettings(writer, token, removed);
    const auto remaining = Create(writer, token, "create-remaining", {false, true, ""});
    DeleteDiagram(writer, token, 2U, "delete-middle-diagram");
    const auto status = Status(observer);
    const auto &display = Field(status, "display");
    Require(Field(status, "measurements").array.size() == 2U && Field(display, "diagrams").array.size() == 2U &&
                Field(display, "selectedDiagram").number == 2.0 &&
                Field(display, "selectedMeasurement").string == remaining &&
                Field(Diagram(display, 2U), "primaryMeasurement").string == remaining,
        "删除中间图窗的连续编号丢失活动迹线归属");
    Require(Field(display, "presentations").Find(removed) == nullptr, "删除图窗后被删迹线和标记集合仍存在");
    VerifyShared(writer, observer);
    VnaScpi::ScpiEngine scpi(fixture.service);
    const auto catalog = Command(scpi, "CALC1:PAR:CAT:EXT?");
    Require(catalog.find(remaining) != std::string::npos && catalog.find(removed) == std::string::npos,
        "SCPI 目录没有读取 HTTP 图窗删除事务");
    Require(Command(scpi, "CALC1:FORM?") == "MLOG\n", "SCPI 活动迹线与 HTTP 选择不同");
    Require(Command(scpi, "DISP:WIND2:CAT?") == "\"1\"\n" && Command(scpi, "DISP:WIND3:STAT?") == "0\n",
        "SCPI 窗口目录没有共享 HTTP 删除和连续重编号");
}

void InvalidTargetAndLastGuardsAreAtomic()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    const auto original = Status(api);
    DeleteDiagram(api, token, 1U, "last-diagram-rejected", false);
    auto badTarget = TraceAction(api, "unknown-target-rejected");
    badTarget["diagram"] = Json(42U);
    Finish(api, Request(api, "POST", "/api/v1/measurement-actions/clone", badTarget, token), token, false);
    auto deletion = Json::MakeObject();
    deletion["requestId"] = Json("last-trace-rejected");
    deletion["expectedRevision"] = Field(Status(api), "revision");
    const auto name = Field(Display(api), "selectedMeasurement").string;
    Finish(api, Request(api, "DELETE", "/api/v1/measurements/" + name, deletion, token), token, false);
    const auto final = Status(api);
    Require(Field(final, "revision").string == Field(original, "revision").string &&
                VnaWeb::StringifyJson(Field(final, "measurements")) ==
                    VnaWeb::StringifyJson(Field(original, "measurements")) &&
                VnaWeb::StringifyJson(Field(final, "display")) == VnaWeb::StringifyJson(Field(original, "display")),
        "失败的新增或删除事务部分改变目录、归属、标记或版本");
}

void QueuedCloneChecksBothVersions()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    auto hold = Json::MakeObject();
    hold["requestId"] = Json("serial-hold");
    hold["action"] = Json("hold");
    Finish(api, Request(api, "POST", "/api/v1/operations", hold, token), token);
    fixture.device->Arm();
    auto single = Json::MakeObject();
    single["requestId"] = Json("serial-single");
    single["action"] = Json("single");
    const auto finite = Request(api, "POST", "/api/v1/operations", single, token);
    Wait([&] {
        Request(api, "GET", "/api/v1/status");
        return fixture.device->HasEntered();
    });
    auto first = TraceAction(api, "queued-clone-first");
    auto second = first;
    second["requestId"] = Json("queued-clone-second");
    const auto acceptedFirst = Request(api, "POST", "/api/v1/measurement-actions/clone", first, token);
    const auto acceptedSecond = Request(api, "POST", "/api/v1/measurement-actions/clone", second, token);
    Require(acceptedFirst.status == 202U && acceptedSecond.status == 202U, "阻塞有限帧期间未按 FIFO 受理候选");
    fixture.device->Release();
    Finish(api, finite, token);
    Finish(api, acceptedFirst, token);
    Finish(api, acceptedSecond, token, false);
    Require(Field(Status(api), "measurements").array.size() == 2U, "相同旧 CAS 两次复制都提交了");
    auto staleDisplay = TraceAction(api, "stale-display-clone");
    auto format = DisplayChange(api, "format", "advance-display-only");
    format["format"] = Json("real");
    Apply(api, format, token);
    const auto saved = Status(api);
    Finish(api, Request(api, "POST", "/api/v1/measurement-actions/clone", staleDisplay, token), token, false);
    Require(Field(Status(api), "revision").string == Field(saved, "revision").string &&
                VnaWeb::StringifyJson(Display(api)) == VnaWeb::StringifyJson(Field(saved, "display")),
        "仅显示版本过期的 Clone 部分提交目录");
}

void MixedFormatsAndEmptyDiagramRemainExplicit()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    const auto copy = Create(writer, token, "same-diagram-copy", {true, false, ""});
    auto format = DisplayChange(writer, "format", "mixed-smith", copy);
    format["format"] = Json("smith");
    Apply(writer, format, token);
    auto display = Display(observer);
    Require(Field(display, "diagrams").array.size() == 1U &&
                Field(Diagram(display, 1U), "measurementNames").array.size() == 2U &&
                Field(Presentation(display, original), "format").string == "db" &&
                Field(Presentation(display, copy), "format").string == "smith",
        "修改单条格式自动拆窗或强改同图其他格式");
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "DISP:WIND1:CAT?") == "\"1,2\"\n", "SCPI 局部目录没有保留混合格式的两条迹线");
    const auto extra = Create(writer, token, "new-diagram-extra", {false, true, ""});
    auto unfeed = DisplayChange(writer, "displayed", "leave-empty-diagram", extra);
    unfeed["diagram"] = Json(2U);
    unfeed["enabled"] = Json(false);
    Apply(writer, unfeed, token);
    display = Display(observer);
    Require(Field(Diagram(display, 2U), "measurementNames").array.empty() &&
                Field(Status(observer), "measurements").array.size() == 3U,
        "解除显示槽关联误删目录或空图窗");
    Require(Command(scpi, "DISP:WIND2:STAT?") == "1\n" && Command(scpi, "DISP:WIND2:CAT?") == "\"EMPTY\"\n",
        "SCPI 观察者将持久空图窗误判为已删除");
    auto select = DisplayChange(writer, "diagramSelect", "select-empty");
    select["diagram"] = Json(2U);
    Apply(writer, select, token);
    const auto inserted = Create(writer, token, "fill-empty-diagram", {false, false, ""});
    display = Display(observer);
    Require(Field(display, "diagrams").array.size() == 2U &&
                Field(Diagram(display, 2U), "measurementNames").array.front().string == inserted,
        "在活动空图窗新增又创建了额外窗或替换旧图");
    auto split = DisplayChange(writer, "split", "split-every-trace");
    Apply(writer, split, token);
    display = Display(observer);
    Require(Field(display, "diagrams").array.size() == Field(display, "displayedNames").array.size(),
        "Split All 没有每条已显示迹线创建独立图窗");
    auto overlay = DisplayChange(writer, "overlay", "overlay-mixed");
    Apply(writer, overlay, token);
    Require(Field(Display(observer), "diagrams").array.size() == 1U, "Overlay All 仍按格式分成多图");
    VerifyShared(writer, observer);
}

void MovingTraceAndDeletingActiveEmptyDiagramKeepSelectionShared()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    const auto second = Create(writer, token, "move-second-diagram", {false, true, ""});
    const auto third = Create(writer, token, "move-third-diagram", {false, true, ""});
    SetSourceSettings(writer, token, original);
    const auto before = Status(writer);
    auto move = DisplayChange(writer, "select", "move-existing-trace", original);
    move["diagram"] = Json(3U);
    Apply(writer, move, token);
    const auto moved = Status(observer);
    const auto &display = Field(moved, "display");
    Require(Field(Diagram(display, 1U), "measurementNames").array.empty() &&
                Field(Diagram(display, 3U), "measurementNames").array.size() == 2U &&
                Field(display, "selectedDiagram").number == 3.0 &&
                Field(display, "selectedMeasurement").string == original,
        "移动已有迹线必须更新后端归属和选择，保留空源图窗");
    Require(Field(moved, "revision").string == Field(before, "revision").string &&
                Field(moved, "acquisitionRevision").string == Field(before, "acquisitionRevision").string &&
                VnaWeb::StringifyJson(Presentation(display, original)) ==
                    VnaWeb::StringifyJson(Presentation(Field(before, "display"), original)),
        "移动图窗不能重建采集计划、复制迹线或丢失尺度与标记");
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "DISP:WIND1:CAT?") == "\"EMPTY\"\n" && Command(scpi, "DISP:WIND3:CAT?") == "\"1,2\"\n",
        "SCPI 没有共享移动后的空源图和目标槽");
    auto select = DisplayChange(writer, "diagramSelect", "select-empty-before-delete");
    select["diagram"] = Json(1U);
    Apply(writer, select, token);
    DeleteDiagram(writer, token, 1U, "delete-active-empty-diagram");
    const auto after = Display(observer);
    Require(Field(after, "diagrams").array.size() == 2U && Field(after, "selectedDiagram").number == 1.0 &&
                Field(after, "selectedMeasurement").string == second &&
                Field(Diagram(after, 1U), "primaryMeasurement").string == second &&
                Field(Diagram(after, 2U), "measurementNames").array.size() == 2U &&
                Field(Status(observer), "measurements").array.size() == 3U,
        "删除活动空图必须选中回退图所属迹线，不能删除其他图的测量");
    Require(Field(Presentation(after, third), "format").string == "db" && Command(scpi, "CALC1:FORM?") == "MLOG\n" &&
                Command(scpi, "DISP:WIND3:STAT?") == "0\n",
        "删除重排后协议选择和图窗目录不一致");
    VerifyShared(writer, observer);
    HttpApi refreshed(fixture.service);
    VerifyShared(writer, refreshed);
}

void CloneWithoutDestinationUsesSourceDiagram()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto source = Field(Display(writer), "selectedMeasurement").string;
    Create(writer, token, "copy-source-target", {false, true, ""});
    auto move = DisplayChange(writer, "select", "copy-source-move", source);
    move["diagram"] = Json(2U);
    Apply(writer, move, token);
    auto select = DisplayChange(writer, "diagramSelect", "copy-source-empty-active");
    select["diagram"] = Json(1U);
    Apply(writer, select, token);
    Require(Field(Display(writer), "selectedMeasurement").string == source, "空图保留活动测量前置条件失效");
    const auto copied = Create(writer, token, "copy-to-source-diagram", {true, false, source});
    const auto after = Display(observer);
    Require(Field(after, "selectedDiagram").number == 2.0 &&
                Field(Diagram(after, 1U), "measurementNames").array.empty() &&
                Field(Diagram(after, 2U), "measurementNames").array.size() == 3U &&
                Field(Diagram(after, 2U), "primaryMeasurement").string == copied,
        "未指定目标的复制必须留在源迹线同图，不能被活动空图截获");
    VerifyShared(writer, observer);
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "DISP:WIND1:CAT?") == "\"EMPTY\"\n" && Command(scpi, "DISP:WIND2:CAT?") == "\"1,2,3\"\n",
        "协议观察没有共享同图复制的后端归属");
}

void DeleteActiveTraceKeepsDiagramContextAndSavedSettings()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto first = Field(Display(writer), "selectedMeasurement").string;
    const auto second = Create(writer, token, "deletion-second", {false, false, ""});
    const auto third = Create(writer, token, "deletion-third", {false, true, ""});
    const auto fourth = Create(writer, token, "deletion-fourth", {false, false, ""});
    SetSourceSettings(writer, token, second, "deletion-second-settings");
    SetSourceSettings(writer, token, third, "deletion-third-settings");
    const auto before = Status(writer);
    const auto &beforeDisplay = Field(before, "display");
    Require(Field(Diagram(beforeDisplay, 1U), "primaryMeasurement").string == second &&
                Field(beforeDisplay, "selectedMeasurement").string == fourth,
        "双图活动记忆前置条件不成立");
    auto deletion = Json::MakeObject();
    deletion["requestId"] = Json("delete-active-in-second-window");
    deletion["expectedRevision"] = Field(before, "revision");
    Finish(writer, Request(writer, "DELETE", "/api/v1/measurements/" + fourth, deletion, token), token);
    const auto after = Status(observer);
    const auto &display = Field(after, "display");
    Require(Field(display, "selectedDiagram").number == 2.0 && Field(display, "selectedMeasurement").string == third &&
                Field(Diagram(display, 2U), "primaryMeasurement").string == third &&
                Field(Diagram(display, 1U), "primaryMeasurement").string == second,
        "删除活动迹线不能跳到首图或覆盖其他图窗的活动记忆");
    for (const auto &name : {first, second, third}) {
        Require(VnaWeb::StringifyJson(Presentation(display, name)) ==
                    VnaWeb::StringifyJson(Presentation(beforeDisplay, name)),
            "删除其他迹线改变了已有格式、刻度或 Marker 设置：" + name);
    }
    Require(Field(after, "acquisitionRevision").string == Field(before, "acquisitionRevision").string,
        "相同测量需求的删除不能重建采集计划");
    VnaScpi::ScpiEngine scpi(fixture.service);
    Require(Command(scpi, "CALC1:FORM?") == "PHAS\n" && Command(scpi, "CALC1:MARK2:STAT?") == "1\n" &&
                Command(scpi, "DISP:WIND2:CAT?") == "\"1\"\n",
        "SCPI 没有共享删除后的正确图窗、活动格式与保存 Marker");
    VerifyShared(writer, observer);
    HttpApi refreshed(fixture.service);
    VerifyShared(writer, refreshed);
}

void SpecifiedParameterCreationIsOneTransaction()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    for (const auto &parameter : {"S44", "S3_4"}) {
        const auto before = Status(writer);
        auto body = TraceAction(writer, std::string("create-requested-") + parameter);
        body["parameter"] = Json(parameter);
        body["newDiagram"] = Json(std::string(parameter) == "S3_4");
        Finish(writer, Request(writer, "POST", "/api/v1/measurement-actions/add", body, token), token);
        const auto after = Status(observer);
        const auto &measurements = Field(after, "measurements").array;
        const auto &display = Field(after, "display");
        const auto name = Field(measurements.back(), "name").string;
        Require(
            std::stoull(Field(after, "revision").string) == std::stoull(Field(before, "revision").string) + 1U &&
                measurements.size() == Field(before, "measurements").array.size() + 1U &&
                Field(measurements.back(), "parameter").string == (std::string(parameter) == "S44" ? "S44" : "S34") &&
                Field(display, "selectedMeasurement").string == name &&
                Field(display, "selectedDiagram").number == (std::string(parameter) == "S44" ? 1.0 : 2.0) &&
                Field(Presentation(display, name), "format").string == "db",
            "指定参数创建应仅提交一次目录版本，并原子安排默认显示与目标图窗");
        VerifyShared(writer, observer);
    }
}

void InvalidRequestedCreationLeavesNoTrace()
{
    VnaService::InstrumentProfile profile;
    profile.portCount = 2U;
    Fixture fixture(profile);
    HttpApi writer(fixture.service);
    const auto token = Claim(writer);
    const auto before = Status(writer);
    const auto unchanged = [&] {
        const auto after = Status(writer);
        Require(Field(after, "revision").string == Field(before, "revision").string &&
                    Field(after, "acquisitionRevision").string == Field(before, "acquisitionRevision").string &&
                    VnaWeb::StringifyJson(Field(after, "measurements")) ==
                        VnaWeb::StringifyJson(Field(before, "measurements")) &&
                    VnaWeb::StringifyJson(Field(after, "display")) == VnaWeb::StringifyJson(Field(before, "display")),
            "拒绝新迹线请求不得残留默认测量、图窗、标记或选择");
    };
    for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
        for (unsigned source = 1U; source <= 4U; ++source) {
            if (receiver <= 2U && source <= 2U) {
                continue;
            }
            const auto parameter = "S" + std::to_string(receiver) + std::to_string(source);
            auto body = TraceAction(writer, "rejected-create-" + parameter);
            body["parameter"] = Json(parameter);
            body["newDiagram"] = Json(true);
            Finish(writer, Request(writer, "POST", "/api/v1/measurement-actions/add", body, token), token, false);
            unchanged();
        }
    }
    for (const auto &parameter : {"S5_1", "S1__2", "s44"}) {
        auto body = TraceAction(writer, std::string("malformed-create-") + parameter);
        body["parameter"] = Json(parameter);
        Finish(writer, Request(writer, "POST", "/api/v1/measurement-actions/add", body, token), token, false);
        unchanged();
    }
    for (const auto &parameter : {Json(), Json(44U), Json(true), Json(""), Json::MakeArray()}) {
        auto body = TraceAction(writer, "bad-type-create-" + VnaWeb::StringifyJson(parameter));
        body["parameter"] = parameter;
        Require(Request(writer, "POST", "/api/v1/measurement-actions/add", body, token).status == 400U,
            "指定参数必须是非空字符串");
        unchanged();
    }
    auto clone = TraceAction(writer, "clone-parameter-rejected");
    clone["parameter"] = Json("S11");
    Require(Request(writer, "POST", "/api/v1/measurement-actions/clone", clone, token).status == 400U,
        "复制入口不允许指定参数");
    unchanged();
    for (const auto &field : {"expectedRevision", "expectedDisplayRevision"}) {
        auto stale = TraceAction(writer, std::string("stale-create-") + field);
        stale["parameter"] = Json("S22");
        stale[field] = Json("0");
        Finish(writer, Request(writer, "POST", "/api/v1/measurement-actions/add", stale, token), token, false);
        unchanged();
    }
}

void RelativeCreationCommitsDirectoryAndSplitTogether()
{
    for (const auto &position : {"above", "below", "left", "right"}) {
        Fixture fixture;
        HttpApi writer(fixture.service);
        HttpApi observer(fixture.service);
        const auto token = Claim(writer);
        const auto before = Status(writer);
        auto body = TraceAction(writer, std::string("create-relative-") + position);
        body["diagram"] = Json(1U);
        body["position"] = Json(position);
        body["parameter"] = Json("S22");
        Finish(writer, Request(writer, "POST", "/api/v1/measurement-actions/add", body, token), token);
        const auto after = Status(observer);
        Require(std::stoull(Field(after, "revision").string) == std::stoull(Field(before, "revision").string) + 1U &&
                    Field(after, "measurements").array.size() == Field(before, "measurements").array.size() + 1U &&
                    Field(Field(after, "display"), "diagrams").array.size() == 2U &&
                    Field(Field(after, "display"), "selectedDiagram").number == 2.0,
            "指定参数、相对分割、新图窗与选择必须在一个目录事务内完成");
        const auto regions = fixture.service.Display().diagramLayout.Regions();
        const bool vertical = std::string(position) == "left" || std::string(position) == "right";
        for (const auto &region : regions) {
            Require(region.width == (vertical ? 0.5 : 1.0) && region.height == (vertical ? 1.0 : 0.5),
                "相对创建没有精确二分目标图窗");
        }
        VerifyShared(writer, observer);
    }
}

void InvalidRelativeCreationDoesNotLeaveDefinitions()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    const auto token = Claim(writer);
    const auto before = Status(writer);
    for (unsigned mode = 0U; mode < 5U; ++mode) {
        auto body = TraceAction(writer, "invalid-relative-" + std::to_string(mode));
        body["parameter"] = Json("S22");
        body["position"] = mode == 0U ? Json(2U) : Json(mode == 1U ? "north" : "above");
        if (mode != 2U) {
            body["diagram"] = Json(mode == 4U ? 127U : 1U);
        }
        if (mode == 3U) {
            body["newDiagram"] = Json(true);
        }
        const auto response = Request(writer, "POST", "/api/v1/measurement-actions/add", body, token);
        if (mode == 4U) {
            Finish(writer, response, token, false);
        } else {
            Require(response.status == 400U, "无效相对位置必须在协议边界拒绝");
        }
        const auto after = Status(writer);
        Require(Field(after, "revision").string == Field(before, "revision").string &&
                    VnaWeb::StringifyJson(Field(after, "measurements")) ==
                        VnaWeb::StringifyJson(Field(before, "measurements")) &&
                    VnaWeb::StringifyJson(Field(after, "display")) == VnaWeb::StringifyJson(Field(before, "display")),
            "失败不能残留新测量、空图窗或自定义布局");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        CloneCopiesSettingsAndDefaultAddUsesNewSettings();
        DeleteDiagramRemovesDefinitionsAndMarkers();
        InvalidTargetAndLastGuardsAreAtomic();
        QueuedCloneChecksBothVersions();
        MixedFormatsAndEmptyDiagramRemainExplicit();
        MovingTraceAndDeletingActiveEmptyDiagramKeepSelectionShared();
        CloneWithoutDestinationUsesSourceDiagram();
        DeleteActiveTraceKeepsDiagramContextAndSavedSettings();
        SpecifiedParameterCreationIsOneTransaction();
        InvalidRequestedCreationLeavesNoTrace();
        RelativeCreationCommitsDirectoryAndSplitTogether();
        InvalidRelativeCreationDoesNotLeaveDefinitions();
        std::cout << "12 个 HTTP 图窗、目录、标记与 CAS 事务验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
