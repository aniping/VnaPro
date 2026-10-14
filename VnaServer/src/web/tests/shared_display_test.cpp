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
#include "scpi/scpi_engine.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using VnaServiceTest::Fixture;
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;
constexpr std::chrono::seconds WAIT_TIMEOUT(3);
constexpr std::chrono::milliseconds POLL_INTERVAL(1);

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

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const Json &body = Json(),
    const std::string &token = "")
{
    VnaWeb::HttpRequest request;
    request.method = method;
    request.path = path;
    if (body.type != Json::Type::NULL_VALUE) {
        request.body = VnaWeb::StringifyJson(body);
    }
    if (!token.empty()) {
        request.headers["x-vna-session"] = token;
    }
    return api.Handle(request);
}

Json Status(HttpApi &api)
{
    const auto response = Request(api, "GET", "/api/v1/status");
    Require(response.status == 200U, "Observer reads status without control ownership");
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

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "POST", "/api/v1/control/claim", Json::MakeObject());
    Require(response.status == 200U, "Writer acquires control lease");
    return Field(response.json, "sessionId").string;
}

std::string Accepted(const HttpResponse &response)
{
    Require(response.status == 202U, "Expected accepted operation, got " + std::to_string(response.status));
    return Field(response.json, "operationId").string;
}

void Complete(HttpApi &api, const std::string &id, const std::string &token, bool success = true)
{
    const auto deadline = HttpApi::Clock::now() + WAIT_TIMEOUT;
    for (;;) {
        const auto response = Request(api, "GET", "/api/v1/operations/" + id, Json(), token);
        Require(response.status == 200U, "Operation belongs to controller");
        const auto state = Field(response.json, "state").string;
        if (state != "pending") {
            Require(state == (success ? "completed" : "failed"), "Expected terminal operation result");
            return;
        }
        Require(HttpApi::Clock::now() < deadline, "Operation finishes before timeout");
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

Json Action(HttpApi &api, const std::string &action, const std::string &name, const std::string &id)
{
    auto result = Json::MakeObject();
    result["requestId"] = Json(id);
    result["expectedDisplayRevision"] = Field(Display(api), "revision");
    result["action"] = Json(action);
    if (!name.empty()) {
        result["traceName"] = Json(name);
    }
    return result;
}

void Apply(HttpApi &api, const Json &body, const std::string &token)
{
    Complete(api, Accepted(Request(api, "POST", "/api/v1/display", body, token)), token);
}

void Single(HttpApi &api, const std::string &token)
{
    auto body = Json::MakeObject();
    body["requestId"] = Json("shared-single");
    body["action"] = Json("single");
    Complete(api, Accepted(Request(api, "POST", "/api/v1/operations", body, token)), token);
}

std::string Clone(HttpApi &api, const std::string &name, const std::string &token)
{
    const auto status = Status(api);
    auto body = Json::MakeObject();
    body["requestId"] = Json("shared-clone");
    body["sourceName"] = Json(name);
    body["expectedRevision"] = Field(status, "revision");
    body["expectedDisplayRevision"] = Field(Field(status, "display"), "revision");
    Complete(api, Accepted(Request(api, "POST", "/api/v1/measurement-actions/clone", body, token)), token);
    return Field(Display(api), "selectedMeasurement").string;
}

void Shared(HttpApi &writer, HttpApi &observer)
{
    const auto expected = VnaWeb::StringifyJson(Display(writer));
    Require(VnaWeb::StringifyJson(Display(observer)) == expected, "Observer reads writer's authoritative display");
}

void ObserversShareAuthoritativeDisplay()
{
    Fixture fixture;
    HttpApi firstObserver(fixture.service);
    HttpApi secondObserver(fixture.service);
    Shared(firstObserver, secondObserver);
    HttpApi refreshedObserver(fixture.service);
    Shared(firstObserver, refreshedObserver);
}

void FormatAndScale(HttpApi &writer, HttpApi &observer, const std::string &name, const std::string &token)
{
    auto select = Action(writer, "select", name, "shared-select");
    Apply(writer, select, token);
    auto format = Action(writer, "format", name, "shared-format");
    format["format"] = Json("phase");
    Apply(writer, format, token);
    auto scale = Action(writer, "scale", name, "shared-scale");
    scale["perDivision"] = Json(5.0);
    scale["referenceLevel"] = Json(30.0);
    scale["referencePosition"] = Json(7.0);
    Apply(writer, scale, token);
    Shared(writer, observer);
    const auto display = Display(observer);
    const auto &presentation = Presentation(display, name);
    Require(Field(display, "selectedMeasurement").string == name, "Selection is shared");
    Require(Field(presentation, "format").string == "phase", "Format is shared");
    const auto &range = Field(presentation, "scale");
    Require(Field(range, "min").number == -5.0 && Field(range, "max").number == 45.0 &&
                Field(range, "referencePosition").number == 7.0,
        "Backend links per division, reference level and position into a saved range");
}

void MarkerAndLayout(HttpApi &writer, HttpApi &observer, const std::string &name, const std::string &token)
{
    auto marker = Action(writer, "marker", name, "shared-marker");
    marker["enabled"] = Json(true);
    marker["sampleIndex"] = Json(37U);
    marker["discrete"] = Json(true);
    marker["acquisitionRevision"] = Field(Status(writer), "acquisitionRevision");
    Apply(writer, marker, token);
    auto layout = Action(writer, "layout", name, "shared-layout");
    layout["layout"] = Json("grid");
    Apply(writer, layout, token);
    Shared(writer, observer);
    const auto display = Display(observer);
    Require(Field(display, "layout").string == "grid", "Layout is shared");
    const auto &saved = Field(Presentation(display, name), "marker");
    Require(Field(saved, "enabled").boolean && Field(saved, "sampleIndex").number == 37.0 &&
                Field(saved, "discrete").boolean,
        "Original marker index and mode are shared");
    const auto frame = Request(observer, "GET", "/api/v1/frame").json;
    Require(Field(frame, "displayRevision").string == Field(display, "revision").string,
        "Projection records its authoritative display revision");
    const auto &projection = Field(Field(frame, "traces").array.front(), "projection");
    Require(Field(projection, "format").string == "phase" && Field(Field(projection, "marker"), "available").boolean,
        "Observer receives backend format projection and marker readout");
}

void SharedActionsAndRefresh()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    const auto copy = Clone(writer, original, token);
    Require(copy != original, "Backend assigns the copied trace name");
    Single(writer, token);
    const auto physical = fixture.core.ReadView();
    const auto logical = fixture.service.Settings().revision;
    FormatAndScale(writer, observer, original, token);
    MarkerAndLayout(writer, observer, original, token);
    HttpApi refreshedObserver(fixture.service);
    Shared(writer, refreshedObserver);
    const auto unchanged = fixture.core.ReadView();
    Require(unchanged.configRevision == physical.configRevision && unchanged.runId == physical.runId &&
                unchanged.frame == physical.frame && fixture.service.Settings().revision == logical,
        "Display actions preserve acquisition identity, original immutable frame and catalog revision");
}

void ReadOnlyAndStaleRevision()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto name = Field(Display(writer), "selectedMeasurement").string;
    auto body = Action(writer, "format", name, "display-read-only");
    body["format"] = Json("real");
    const auto before = VnaWeb::StringifyJson(Display(writer));
    Require(Request(observer, "POST", "/api/v1/display", body).status == 403U, "Observer cannot alter display");
    Require(VnaWeb::StringifyJson(Display(writer)) == before, "Unauthorized writes leave display unchanged");
    body["requestId"] = Json("display-dedupe");
    const auto id = Accepted(Request(writer, "POST", "/api/v1/display", body, token));
    Complete(writer, id, token);
    Require(Accepted(Request(writer, "POST", "/api/v1/display", body, token)) == id,
        "Retry returns original operation before stale revision rejection");
    body["requestId"] = Json("display-stale");
    Require(
        Request(writer, "POST", "/api/v1/display", body, token).status == 409U, "New stale display CAS is rejected");
    Shared(writer, observer);
}

std::string Query(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Require(engine.SubmitLine(line), "SCPI observer accepts query");
    const auto deadline = HttpApi::Clock::now() + WAIT_TIMEOUT;
    do {
        engine.Poll();
        Require(HttpApi::Clock::now() < deadline, "SCPI observer query completes");
        std::this_thread::sleep_for(POLL_INTERVAL);
    } while (engine.HasPending());
    return engine.TakeOutput();
}

void ScpiObserverSeesWebWritesWhileLeaseHeld()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    const auto token = Claim(writer);
    const auto owner = fixture.service.ControlOwner();
    VnaScpi::ScpiEngine observer(fixture.service);
    Require(!observer.HasControl(), "SCPI observes while Web retains the control lease");
    const auto name = Field(Display(writer), "selectedMeasurement").string;
    auto body = Action(writer, "format", name, "web-visible-to-scpi");
    body["format"] = Json("real");
    Apply(writer, body, token);
    Require(Query(observer, "CALC1:FORM?") == "REAL\n", "SCPI sees Web format without taking control");
    Single(writer, token);
    auto marker = Action(writer, "marker", name, "web-marker-visible-to-scpi");
    marker["enabled"] = Json(true);
    Apply(writer, marker, token);
    Require(Query(observer, "CALC1:MARK1:STAT?") == "1\n", "SCPI sees Web marker state");
    Require(fixture.service.ControlOwner() == owner, "Queries retain the Web lease");
}

void NumberedMarkersAreSharedAndAtomic()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    Single(writer, token);
    const auto name = Field(Display(writer), "selectedMeasurement").string;
    for (unsigned id = 1U; id <= VnaDisplay::MAX_MARKERS; ++id) {
        auto add = Action(writer, "marker", name, "numbered-add-" + std::to_string(id));
        add["markerAction"] = Json("add");
        Apply(writer, add, token);
        const auto display = Display(observer);
        const auto &presentation = Presentation(display, name);
        const auto &markers = Field(presentation, "markers").array;
        Require(markers.size() == VnaDisplay::MAX_MARKERS && Field(presentation, "activeMarkerId").number == id,
            "观察器必须读到十个独立标记及本次新增的活动编号");
        Require(Field(markers[id - 1U], "id").number == id && Field(markers[id - 1U], "enabled").boolean,
            "新增不能反复覆盖 M1");
    }
    const auto full = VnaWeb::StringifyJson(Display(writer));
    auto extra = Action(writer, "marker", name, "numbered-full");
    extra["markerAction"] = Json("add");
    Complete(writer, Accepted(Request(writer, "POST", "/api/v1/display", extra, token)), token, false);
    Require(VnaWeb::StringifyJson(Display(writer)) == full, "满十个后新增失败不能部分提交");
    auto move = Action(writer, "marker", name, "numbered-move-m2");
    move["markerId"] = Json(2U);
    move["sampleIndex"] = Json(37U);
    move["discrete"] = Json(true);
    move["acquisitionRevision"] = Field(Status(writer), "acquisitionRevision");
    Apply(writer, move, token);
    Shared(writer, observer);
    const auto moved = Display(observer);
    const auto &presentation = Presentation(moved, name);
    const auto &markers = Field(presentation, "markers").array;
    Require(Field(presentation, "activeMarkerId").number == 2.0 && Field(markers[1U], "sampleIndex").number == 37.0 &&
                Field(markers[0U], "sampleIndex").number != 37.0,
        "M2 移动与激活不能改变 M1 位置");
    const auto frame = Request(observer, "GET", "/api/v1/frame").json;
    const auto &projection = Field(Field(frame, "traces").array.front(), "projection");
    Require(Field(projection, "activeMarkerId").number == 2.0 &&
                Field(Field(projection, "markers").array[1U], "sampleIndex").number == 37.0 &&
                Field(Field(projection, "marker"), "sampleIndex").number == 37.0,
        "帧的活动别名与编号读数必须来自同一完整帧");
    const auto beforeInvalid = VnaWeb::StringifyJson(Display(observer));
    move["requestId"] = Json("numbered-invalid-id");
    move["expectedDisplayRevision"] = Field(Display(writer), "revision");
    move["markerId"] = Json(VnaDisplay::MAX_MARKERS + 1U);
    Require(Request(writer, "POST", "/api/v1/display", move, token).status == 400U &&
                VnaWeb::StringifyJson(Display(observer)) == beforeInvalid,
        "协议拒绝非法编号，权威状态不改变");
    auto allOff = Action(writer, "marker", name, "numbered-all-off");
    allOff["markerAction"] = Json("allOff");
    Apply(writer, allOff, token);
    const auto disabled = Display(observer);
    const auto &saved = Field(Presentation(disabled, name), "markers").array;
    for (const auto &marker : saved) {
        Require(!Field(marker, "enabled").boolean, "All Off 关闭当前迹线全部标记");
    }
    Require(Field(saved[1U], "sampleIndex").number == 37.0, "关闭后保留原始位置供重新开启");
}

void InfoTableAndWindowAnnotationsAreShared()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    const auto copy = Clone(writer, original, token);
    Single(writer, token);
    auto split = Action(writer, "split", original, "info-table-split");
    Apply(writer, split, token);
    const auto physical = fixture.core.ReadView();
    const auto logical = fixture.service.Settings().revision;
    const auto selected = Field(Display(writer), "selectedMeasurement").string;
    auto hide = Action(writer, "markerInfo", "", "info-table-hide-window-one");
    hide["enabled"] = Json(false);
    hide["diagram"] = Json(1U);
    Apply(writer, hide, token);
    auto move = Action(writer, "markerInfoLocation", original, "info-table-move-original");
    move["location"] = Json("table");
    Apply(writer, move, token);
    auto display = Display(observer);
    Require(!Field(Field(display, "infoTable"), "visible").boolean &&
                Field(display, "selectedMeasurement").string == selected &&
                Field(Presentation(display, original), "markerInfoLocation").string == "table" &&
                Field(Presentation(display, copy), "markerInfoLocation").string == "diagram" &&
                !Field(Field(display, "diagrams").array[0U], "markerInfoVisible").boolean &&
                Field(Field(display, "diagrams").array[1U], "markerInfoVisible").boolean,
        "移动字段和显式图窗开关必须保持独立，不能改变其他窗口或活动迹线");
    auto table = Action(writer, "infoTable", "", "info-table-show-left");
    table["enabled"] = Json(true);
    table["position"] = Json("left");
    Apply(writer, table, token);
    auto annotations = Action(writer, "displayAnnotations", "", "info-table-annotation-options");
    annotations["activeTraceOnly"] = Json(true);
    annotations["stimulusInfoVisible"] = Json(false);
    annotations["fontScale"] = Json(137.5);
    Apply(writer, annotations, token);
    display = Display(observer);
    Require(Field(Field(display, "infoTable"), "visible").boolean &&
                Field(Field(display, "infoTable"), "position").string == "left" &&
                Field(Field(display, "annotations"), "activeTraceOnly").boolean &&
                !Field(Field(display, "annotations"), "stimulusInfoVisible").boolean &&
                Field(Field(display, "annotations"), "fontScale").number == 137.5,
        "观察器必须读到全局表格、活动迹线过滤和标记频率文字设置");
    table = Action(writer, "infoTable", "", "info-table-hide-right");
    table["enabled"] = Json(false);
    table["position"] = Json("right");
    Apply(writer, table, token);
    Shared(writer, observer);
    HttpApi refreshed(fixture.service);
    Shared(writer, refreshed);
    display = Display(refreshed);
    Require(Field(Presentation(display, original), "markerInfoLocation").string == "table",
        "隐藏表格与刷新浏览器都不能恢复字段归属");
    const auto revision = Field(display, "revision").string;
    table["requestId"] = Json("info-table-no-op");
    table["expectedDisplayRevision"] = Json(revision);
    Apply(writer, table, token);
    Require(Field(Display(observer), "revision").string == revision, "重复相同设置不能增加显示版本");
    const auto unchanged = fixture.core.ReadView();
    Require(unchanged.configRevision == physical.configRevision && unchanged.runId == physical.runId &&
                unchanged.frame == physical.frame && fixture.service.Settings().revision == logical,
        "信息表格与注释不得修改核心扫描、完整帧和测量目录");
}

void InfoTableStrictContractAndRevision()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    auto table = Action(writer, "infoTable", "", "info-table-read-only");
    table["enabled"] = Json(true);
    const auto original = VnaWeb::StringifyJson(Display(writer));
    Require(Request(observer, "POST", "/api/v1/display", table).status == 403U &&
                VnaWeb::StringifyJson(Display(writer)) == original,
        "观察器不得修改全局表格设置");
    table["requestId"] = Json("info-table-first-write");
    const auto id = Accepted(Request(writer, "POST", "/api/v1/display", table, token));
    Complete(writer, id, token);
    Require(Accepted(Request(writer, "POST", "/api/v1/display", table, token)) == id, "重试表格设置仍返回原操作");
    table["requestId"] = Json("info-table-stale-write");
    Require(Request(writer, "POST", "/api/v1/display", table, token).status == 409U,
        "新请求不得使用过期显示版本覆盖表格设置");
    const auto saved = VnaWeb::StringifyJson(Display(writer));
    for (const std::string action : {"infoTable", "displayAnnotations", "markerInfo"}) {
        auto scoped = Action(writer, action, "Trc1", "info-table-invalid-trace-scope-" + action);
        if (action == "displayAnnotations") {
            scoped["activeTraceOnly"] = Json(true);
        } else {
            scoped["enabled"] = Json(false);
        }
        Require(Request(writer, "POST", "/api/v1/display", scoped, token).status == 400U &&
                    VnaWeb::StringifyJson(Display(writer)) == saved,
            "全局或图窗设置不得静默忽略迹线范围: " + action);
    }
    auto invalid = Action(writer, "infoTable", "", "info-table-invalid-position");
    invalid["enabled"] = Json(false);
    invalid["position"] = Json("top");
    Require(
        Request(writer, "POST", "/api/v1/display", invalid, token).status == 400U, "表格只接受 bottom、left、right");
    invalid = Action(writer, "markerInfo", "", "info-table-missing-enabled");
    Require(Request(writer, "POST", "/api/v1/display", invalid, token).status == 400U, "图窗字段开关必须提供 enabled");
    invalid = Action(writer, "markerInfoLocation", "Trc1", "info-table-mixed-scope");
    invalid["location"] = Json("table");
    invalid["fontScale"] = Json(120.0);
    Require(Request(writer, "POST", "/api/v1/display", invalid, token).status == 400U, "字段迁移不能混入全局注释设置");
    invalid = Action(writer, "displayAnnotations", "", "info-table-invalid-font");
    invalid["activeTraceOnly"] = Json(true);
    invalid["fontScale"] = Json(170.1);
    Complete(writer, Accepted(Request(writer, "POST", "/api/v1/display", invalid, token)), token, false);
    Require(VnaWeb::StringifyJson(Display(writer)) == saved, "协议或领域校验失败时全部显示状态必须原子保持不变");
    Shared(writer, observer);
}

void MarkerInfoAreasAreSharedAndAtomic()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    Require(Field(Presentation(Display(observer), original), "markerInfoArea").string == "topRight",
        "新字段的出厂锚点必须由后端提供");
    auto move = Action(writer, "markerInfoLocation", original, "marker-info-before-clone");
    move["location"] = Json("diagram");
    move["area"] = Json("bottomLeft");
    Apply(writer, move, token);
    const auto copy = Clone(writer, original, token);
    Single(writer, token);
    const auto physical = fixture.core.ReadView();
    const auto logical = fixture.service.Settings().revision;
    const auto selected = Field(Display(writer), "selectedMeasurement").string;
    for (const std::string area : {"topLeft", "topCenter", "topRight", "centerLeft", "center", "centerRight",
             "bottomLeft", "bottomCenter", "bottomRight"}) {
        move = Action(writer, "markerInfoLocation", original, "marker-info-area-" + area);
        move["location"] = Json("diagram");
        move["area"] = Json(area);
        Apply(writer, move, token);
        const auto display = Display(observer);
        Require(Field(Presentation(display, original), "markerInfoArea").string == area &&
                    Field(Presentation(display, copy), "markerInfoArea").string == "bottomLeft" &&
                    Field(display, "selectedMeasurement").string == selected,
            "观察器必须读到独立迹线字段定位，复制字段随后独立且不能切换活动迹线");
    }
    const auto revision = Field(Display(writer), "revision").string;
    move["requestId"] = Json("marker-info-area-no-op");
    move["expectedDisplayRevision"] = Json(revision);
    Apply(writer, move, token);
    Require(Field(Display(observer), "revision").string == revision, "重复字段锚点不增加显示版本");
    move = Action(writer, "markerInfoLocation", original, "marker-info-area-to-table");
    move["location"] = Json("table");
    Apply(writer, move, token);
    move = Action(writer, "markerInfoLocation", original, "marker-info-area-back-diagram");
    move["location"] = Json("diagram");
    Apply(writer, move, token);
    Require(Field(Presentation(Display(observer), original), "markerInfoArea").string == "bottomRight",
        "表格归属变化与图内锚点必须独立保存");
    auto stale = Action(writer, "markerInfoLocation", original, "marker-info-area-stale");
    stale["location"] = Json("diagram");
    stale["area"] = Json("topLeft");
    move = Action(writer, "markerInfoLocation", original, "marker-info-area-fresh");
    move["location"] = Json("diagram");
    move["area"] = Json("topCenter");
    Apply(writer, move, token);
    const auto saved = VnaWeb::StringifyJson(Display(writer));
    Require(Request(writer, "POST", "/api/v1/display", stale, token).status == 409U &&
                VnaWeb::StringifyJson(Display(observer)) == saved,
        "过期显示版本不能覆盖已完成的字段定位");
    auto invalid = Action(writer, "markerInfoLocation", original, "marker-info-area-read-only");
    invalid["location"] = Json("diagram");
    invalid["area"] = Json("center");
    Require(Request(observer, "POST", "/api/v1/display", invalid).status == 403U &&
                VnaWeb::StringifyJson(Display(observer)) == saved,
        "观察器不得拖动字段位置");
    unsigned index = 0U;
    for (const auto area : {Json("unknown"), Json("TopLeft"), Json(1U), Json()}) {
        invalid["requestId"] = Json("marker-info-area-invalid-" + std::to_string(index++));
        invalid["area"] = area;
        Require(Request(writer, "POST", "/api/v1/display", invalid, token).status == 400U &&
                    VnaWeb::StringifyJson(Display(observer)) == saved,
            "非法位置名称或类型必须原子拒绝");
    }
    invalid["requestId"] = Json("marker-info-area-table-conflict");
    invalid["location"] = Json("table");
    invalid["area"] = Json("bottomLeft");
    Require(Request(writer, "POST", "/api/v1/display", invalid, token).status == 400U &&
                VnaWeb::StringifyJson(Display(observer)) == saved,
        "表格与图内定位的混合请求不能部分迁移字段");
    HttpApi refreshed(fixture.service);
    Shared(writer, refreshed);
    const auto unchanged = fixture.core.ReadView();
    Require(unchanged.configRevision == physical.configRevision && unchanged.runId == physical.runId &&
                unchanged.frame == physical.frame && fixture.service.Settings().revision == logical,
        "字段拖动不得修改扫描状态、完整帧或测量目录");
}

void MarkerSelectionNavigatesOwnerAcrossWindows()
{
    Fixture fixture;
    HttpApi writer(fixture.service);
    HttpApi observer(fixture.service);
    const auto token = Claim(writer);
    const auto original = Field(Display(writer), "selectedMeasurement").string;
    const auto copy = Clone(writer, original, token);
    Single(writer, token);
    auto select = Action(writer, "marker", original, "marker-select-owner-one");
    select["markerAction"] = Json("select");
    select["markerId"] = Json(5U);
    const auto revision = std::stoull(Field(Display(writer), "revision").string);
    Apply(writer, select, token);
    auto display = Display(observer);
    Require(Field(display, "selectedMeasurement").string == original &&
                Field(Field(display, "diagrams").array.front(), "primaryMeasurement").string == original &&
                Field(Presentation(display, original), "activeMarkerId").number == 5.0 &&
                std::stoull(Field(display, "revision").string) == revision + 1U,
        "点击别的迹线标记必须一次提交所属迹线、图窗主迹线和活动编号");
    Apply(writer, Action(writer, "split", "", "marker-select-split"), token);
    select = Action(writer, "marker", copy, "marker-select-owner-window-two");
    select["markerAction"] = Json("select");
    select["markerId"] = Json(3U);
    Apply(writer, select, token);
    display = Display(observer);
    Require(Field(display, "selectedMeasurement").string == copy && Field(display, "selectedDiagram").number == 2.0 &&
                Field(Field(display, "diagrams").array[1U], "primaryMeasurement").string == copy &&
                Field(Presentation(display, copy), "activeMarkerId").number == 3.0,
        "选择另一个图窗的标记必须共享所属图窗与迹线导航结果");
    HttpApi refreshed(fixture.service);
    Shared(writer, refreshed);
    const auto saved = VnaWeb::StringifyJson(Display(writer));
    select = Action(writer, "marker", original, "marker-select-invalid-owner-id");
    select["markerAction"] = Json("select");
    select["markerId"] = Json(VnaDisplay::MAX_MARKERS + 1U);
    Require(Request(writer, "POST", "/api/v1/display", select, token).status == 400U &&
                VnaWeb::StringifyJson(Display(observer)) == saved,
        "非法编号不能改变当前图窗、所属迹线或其活动标记");
}

}  // 匿名命名空间

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"shared-authoritative-display", ObserversShareAuthoritativeDisplay},
        {"shared-actions-and-refresh", SharedActionsAndRefresh},
        {"shared-display-read-only-and-cas", ReadOnlyAndStaleRevision},
        {"shared-scpi-observer-sees-web-writes", ScpiObserverSeesWebWritesWhileLeaseHeld},
        {"shared-numbered-markers", NumberedMarkersAreSharedAndAtomic},
        {"shared-info-table-and-window-annotations", InfoTableAndWindowAnnotationsAreShared},
        {"shared-info-table-strict-contract-and-cas", InfoTableStrictContractAndRevision},
        {"shared-marker-info-area-and-cas", MarkerInfoAreasAreSharedAndAtomic},
        {"shared-marker-selection-navigates-owner", MarkerSelectionNavigatesOwnerAcrossWindows}};
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
