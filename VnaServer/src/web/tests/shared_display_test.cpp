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
    result["traceName"] = Json(name);
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

}  // 匿名命名空间

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"shared-authoritative-display", ObserversShareAuthoritativeDisplay},
        {"shared-actions-and-refresh", SharedActionsAndRefresh},
        {"shared-display-read-only-and-cas", ReadOnlyAndStaleRevision},
        {"shared-scpi-observer-sees-web-writes", ScpiObserverSeesWebWritesWhileLeaseHeld},
        {"shared-numbered-markers", NumberedMarkersAreSharedAndAtomic}};
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
