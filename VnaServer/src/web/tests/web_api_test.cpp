/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cfenv>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"
#include "web/http_api.h"
#include "web/json.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"
#include "web/web_endpoint.h"

namespace {
constexpr std::chrono::seconds TEST_WAIT_TIMEOUT(3);
constexpr std::chrono::milliseconds TEST_POLL_INTERVAL(1);
using VnaServiceTest::Fixture;
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &name)
{
    const Json *found = value.Find(name);
    if (!found) {
        throw std::runtime_error("Missing field: " + name);
    }
    return *found;
}

std::string StringField(const Json &value, const std::string &name)
{
    const Json &found = Field(value, name);
    Require(found.type == Json::Type::STRING, "String field " + name);
    return found.string;
}

HttpResponse Request(HttpApi &api, const std::string &method, const std::string &path, const std::string &body = "",
    const std::string &token = "")
{
    VnaWeb::HttpRequest value;
    value.method = method;
    value.path = path;
    value.body = body;
    if (!token.empty()) {
        value.headers["x-vna-session"] = token;
    }
    return api.Handle(value);
}

std::string Claim(HttpApi &api)
{
    const auto response = Request(api, "POST", "/api/v1/control/claim", "{}");
    Require(response.status == 200U, "Claim succeeds");
    return StringField(response.json, "sessionId");
}

void Until(const std::function<bool()> &done, const std::function<void()> &poll)
{
    const auto end = std::chrono::steady_clock::now() + TEST_WAIT_TIMEOUT;
    while (!done()) {
        Require(std::chrono::steady_clock::now() < end, "Timed out waiting for controlled task");
        poll();
        std::this_thread::sleep_for(TEST_POLL_INTERVAL);
    }
}

VnaService::OperationResult Finish(VnaService::InstrumentService &service, const VnaService::Operation &operation)
{
    Until(
        [&operation] {
            return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        },
        [&service] {
            service.Poll();
        });
    return operation.get();
}

HttpResponse Finish(HttpApi &api, const std::string &operation, const std::string &token)
{
    HttpResponse response;
    Until(
        [&response, &api, &operation, &token] {
            response = Request(api, "GET", "/api/v1/operations/" + operation, "", token);
            Require(response.status == 200U, "Read owned operation");
            return StringField(response.json, "state") != "pending";
        },
        [&api] {
            api.Poll();
        });
    return response;
}

std::string Accepted(const HttpResponse &response)
{
    Require(response.status == 202U, "Operation accepted, received " + std::to_string(response.status));
    return StringField(response.json, "operationId");
}

std::string ConfigBody(std::uint64_t revision, const std::string &id, unsigned points)
{
    return "{\"expectedRevision\":\"" + std::to_string(revision) + "\",\"requestId\":\"" + id +
           "\",\"points\":" + std::to_string(points) + "}";
}

std::string BeginGatedSingle(HttpApi &api, Fixture &fixture, const std::string &token, const std::string &requestId)
{
    fixture.device->Arm();
    const auto id = Accepted(Request(
        api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"" + requestId + "\"}", token));
    Until(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        [&api] {
            api.Poll();
        });
    return id;
}

void AtomicAndDedupe()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    fixture.service.RecordError(-113, "SCPI sentinel");
    const auto token = Claim(api);
    Require(!fixture.service.TryAcquireControl(), "Web excludes SCPI");
    const auto previous = fixture.service.Settings().revision;
    const std::string body =
        "{\"expectedRevision\":\"" + std::to_string(previous) +
        "\",\"requestId\":\"atomic\",\"startHz\":2000000,\"stopHz\":3000000,\"points\":19,\"ifBandwidthHz\":101}";
    const auto id = Accepted(Request(api, "POST", "/api/v1/config", body, token));
    const auto complete = Finish(api, id, token);
    Require(StringField(complete.json, "state") == "completed", "Atomic config completed");
    const auto &settings = fixture.service.Settings();
    Require(settings.revision == previous + 1U && settings.points == 19U && settings.startHz == 2000000.0 &&
                settings.stopHz == 3000000.0 && settings.ifBandwidthHz == 1000.0,
        "Exactly one committed configuration");
    Require(
        Accepted(Request(api, "POST", "/api/v1/config", body, token)) == id, "Dedupe precedes stale revision check");
    Require(Request(api, "POST", "/api/v1/config", ConfigBody(previous, "atomic", 21U), token).status == 409U,
        "Conflicting retry rejected");
    Require(Request(api, "POST", "/api/v1/config", ConfigBody(0U, "zero", 21U), token).status == 409U,
        "Revision zero is strict");
    Require(
        Request(api, "DELETE", "/api/v1/config", ConfigBody(settings.revision, "wrong-method", 21U), token).status ==
            405U,
        "DELETE config does not mutate");
    Require(settings.points == 19U && settings.revision == previous + 1U, "Wrong method leaves config unchanged");
    Require(Request(api, "GET", "/api/v1/operations/" + id).status == 403U, "Operation reads require owner token");
    Require(Request(api, "GET", "/api/v1/operations/" + id, "", "another").status == 403U,
        "Different token cannot read operation");
    Request(api, "GET", "/api/v1/status");
    Request(api, "GET", "/api/v1/frame");
    Require(fixture.service.ErrorCount() == 1U && fixture.service.PopError().code == -113,
        "Web leaves SCPI FIFO untouched");
}

void ConfigurationThenSingleFifo()
{
    Fixture fixture;
    const std::string owner = "web-fifo";
    Require(fixture.service.TryAcquireControl(owner), "Acquire typed owner");
    const auto revision = fixture.service.Settings().revision;
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 23U;
    const auto config = fixture.service.ApplyConfiguration(patch, revision, owner);
    const auto single = fixture.service.RunSingle(owner);
    Require(fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "Queued Single does not change committed mode");
    Require(Finish(fixture.service, config).ok, "Earlier configuration succeeds");
    Require(Finish(fixture.service, single).ok, "Later single succeeds");
    const auto view = fixture.core.ReadView();
    Require(view.frame && view.frame->configRevision == revision + 1U && view.frame->plan.frequencyHz.size() == 23U &&
                view.frame->runId == view.runId,
        "Single uses preceding committed configuration");
    Require(fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD, "Single completes in HOLD");
}

void ExecutionRevisionCheck()
{
    Fixture fixture;
    const std::string owner = "web-cas";
    Require(fixture.service.TryAcquireControl(owner), "Acquire typed owner");
    const auto revision = fixture.service.Settings().revision;
    VnaService::ConfigPatch first;
    first.hasPoints = true;
    first.points = 17U;
    VnaService::ConfigPatch second;
    second.hasPoints = true;
    second.points = 29U;
    const auto a = fixture.service.ApplyConfiguration(first, revision, owner);
    const auto b = fixture.service.ApplyConfiguration(second, revision, owner);
    Require(Finish(fixture.service, a).ok, "First configuration commits");
    Require(!Finish(fixture.service, b).ok, "Execution rechecks expected revision");
    Require(fixture.service.Settings().points == 17U && fixture.service.Settings().revision == revision + 1U,
        "CAS failure leaves previous commit");
    Require(!Finish(fixture.service, fixture.service.ApplyConfiguration(second, 0U, owner)).ok,
        "Typed zero revision cannot bypass CAS");
    Require(fixture.service.ErrorCount() == 0U, "Web CAS failures do not enter SCPI FIFO");
}

void VerifyReleasingAdmission(
    HttpApi &api, Fixture &fixture, const std::string &id, const std::string &token, bool shuttingDown)
{
    Require(fixture.service.ControlReleasing() && !fixture.service.TryAcquireControl(),
        "Pending controller retains releasing ownership");
    Require(!api.Drained(), "Shutdown drain waits accepted operation");
    Require(Request(api, "GET", "/api/v1/operations/" + id, "", token).status == 200U,
        "Released owner still reads accepted operation");
    const auto rejected =
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"hold\",\"requestId\":\"late\"}", token);
    Require(rejected.status == (shuttingDown ? 503U : 409U), "No new writes during release");
}

void VerifyReleaseScenario(unsigned scenario)
{
    Fixture fixture;
    auto time = HttpApi::Clock::time_point();
    HttpApi api(fixture.service, [clock = &time] {
        return *clock;
    });
    const auto token = Claim(api);
    const auto id = BeginGatedSingle(api, fixture, token, "gated");
    if (scenario == 0U) {
        Require(
            Request(api, "POST", "/api/v1/control/release", "{}", token).status == 200U, "Explicit release accepted");
    }
    if (scenario == 1U) {
        time += std::chrono::seconds(31);
        // 时钟跳变与申请之间不调用 Poll，验证准入时也会处理过期。
        Require(Request(api, "POST", "/api/v1/control/claim", "{\"sessionId\":\"" + token + "\"}").status == 409U,
            "Expired lease cannot renew past accepted operation drain");
    }
    if (scenario == 2U) {
        api.BeginShutdown();
    }
    VerifyReleasingAdmission(api, fixture, id, token, scenario == 2U);
    fixture.device->Release();
    Require(StringField(Finish(api, id, token).json, "state") == "completed", "Accepted operation survives release");
    api.Poll();
    Require(api.Drained() && fixture.service.TryAcquireControl(), "SCPI acquires after drain");
    Require(fixture.service.ErrorCount() == 0U, "Lease conflicts leave SCPI errors untouched");
}

void ReleasingAndLease()
{
    for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
        VerifyReleaseScenario(scenario);
    }
}

void SnapshotAndSampling()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    auto response = Request(api, "GET", "/api/v1/frame");
    Require(!Field(response.json, "available").boolean, "No fabricated initial frame");
    const auto config = Accepted(Request(
        api, "POST", "/api/v1/config", ConfigBody(fixture.service.Settings().revision, "large", 10001U), token));
    Require(StringField(Finish(api, config, token).json, "state") == "completed", "Large config completes");
    const auto single =
        Accepted(Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"first\"}", token));
    Require(StringField(Finish(api, single, token).json, "state") == "completed", "Large single completes");
    response = Request(api, "GET", "/api/v1/frame");
    Require(Field(response.json, "current").boolean && !Field(response.json, "decimated").boolean,
        "Completed single trace preserves the full sweep");
    Require(Field(response.json, "originalPointCount").number == 10001.0 &&
                Field(response.json, "displayPointCount").number == 10001.0,
        "Sampling metadata truthful");
    const auto &axis = Field(response.json, "frequencyHz").array;
    Require(axis.size() == 10001U && axis.front().number == 1000000.0 && axis.back().number == 100000000.0,
        "The complete axis includes every original sample");
    const auto &traces = Field(response.json, "traces").array;
    Require(traces.size() == 1U && Field(traces.front(), "real").array.size() == axis.size() &&
                Field(traces.front(), "valid").array.front().boolean,
        "Trace arrays share the frequency snapshot");
    fixture.device->Arm();
    Accepted(Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"second\"}", token));
    response = Request(api, "GET", "/api/v1/frame");
    Require(Field(response.json, "available").boolean && !Field(response.json, "current").boolean,
        "Pending finite operation invalidates prior frame immediately");
    fixture.device->Release();
    Until(
        [&fixture] {
            return !fixture.service.HasPending();
        },
        [&api] {
            api.Poll();
        });
}

void OperationRetentionAndBudgets()
{
    Fixture fixture;
    auto time = HttpApi::Clock::time_point();
    HttpApi api(fixture.service, [clock = &time] {
        return *clock;
    });
    const auto token = Claim(api);
    std::string first;
    for (unsigned index = 0; index < 128U; ++index) {
        const auto id = Accepted(Request(api, "POST", "/api/v1/operations",
            "{\"action\":\"hold\",\"requestId\":\"hold-" + std::to_string(index) + "\"}", token));
        Require(StringField(Finish(api, id, token).json, "state") == "completed", "Bounded record completes");
        if (index == 0U) {
            first = id;
        }
    }
    Require(
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"hold\",\"requestId\":\"overflow\"}", token).status ==
            429U,
        "The 129th retained operation is rejected");
    time += std::chrono::seconds(59);
    Require(Request(api, "GET", "/api/v1/operations/" + first, "", token).status == 200U,
        "Released token reads terminal result for 59 seconds");
    time += std::chrono::seconds(1);
    Require(Request(api, "GET", "/api/v1/operations/" + first, "", token).status == 404U,
        "Terminal record expires after 60 seconds");
}

void LongOperationRetention()
{
    // 长时间原生调用期间保留待完成操作；保留期限从操作完成时开始计算。
    Fixture fixture;
    auto time = HttpApi::Clock::time_point();
    HttpApi api(fixture.service, [clock = &time] {
        return *clock;
    });
    const auto secondToken = Claim(api);
    const auto gated = BeginGatedSingle(api, fixture, secondToken, "long-native");
    time += std::chrono::seconds(100);
    const auto pending = Request(api, "GET", "/api/v1/operations/" + gated, "", secondToken);
    Require(pending.status == 200U && StringField(pending.json, "state") == "pending",
        "Pending result survives record TTL and expired lease");
    Require(fixture.service.ControlReleasing(), "Long native call keeps expired controller releasing");
    fixture.device->Release();
    Require(
        StringField(Finish(api, gated, secondToken).json, "state") == "completed", "Long accepted operation completes");
    time += std::chrono::seconds(59);
    Require(
        Request(api, "GET", "/api/v1/operations/" + gated, "", secondToken).status == 200U, "TTL starts at completion");
    time += std::chrono::seconds(1);
    Require(Request(api, "GET", "/api/v1/operations/" + gated, "", secondToken).status == 404U,
        "Completed long operation eventually expires");
}

void VerifyInvalidTrace(const Json &trace)
{
    const auto &real = Field(trace, "real").array;
    const auto &imaginary = Field(trace, "imag").array;
    const auto &validity = Field(trace, "valid").array;
    Require(real.size() == 201U && imaginary.size() == 201U && validity.size() == 201U,
        "Invalid points retain their axis positions");
    for (std::size_t point = 0U; point < validity.size(); ++point) {
        const bool invalid = point == 1U || point == 2U;
        Require(validity[point].boolean == !invalid, "Only the affected IQ points are invalid");
        if (invalid) {
            Require(real[point].type == Json::Type::NULL_VALUE && imaginary[point].type == Json::Type::NULL_VALUE,
                "Either invalid component makes both public components null");
        } else {
            Require(real[point].type == Json::Type::NUMBER && imaginary[point].type == Json::Type::NUMBER &&
                        real[point].number == 1.11 && imaginary[point].number == 1.11,
                "Valid neighboring samples remain exact");
        }
    }
}

void InvalidPointsRemainExplicit()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    // 此测试验证原始接收机样本，不依赖仪器上电时默认的 S21 比值测量。
    const auto rawBody = "{\"requestId\":\"raw-invalid-test\",\"expectedRevision\":\"" +
                         std::to_string(fixture.service.Settings().revision) +
                         "\",\"kind\":\"receiver\",\"receiverKind\":\"a\",\"receiverPort\":1,\"sourcePort\":1}";
    const auto raw = Accepted(Request(
        api, "PUT", "/api/v1/measurements/" + fixture.service.Settings().measurements.front().name, rawBody, token));
    Require(StringField(Finish(api, raw, token).json, "state") == "completed", "Explicit raw measurement completes");
    fixture.device->SetInvalidSamples(true);
    const auto id = Accepted(
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"invalid-samples\"}", token));
    Require(StringField(Finish(api, id, token).json, "state") == "completed", "Full invalid-data frame completes");
    const auto held = fixture.core.ReadView().frame;
    Require(held && std::isnan(held->ports.front().iq[16U]) && std::isinf(held->ports.front().iq[33U]),
        "Core preserves received invalid samples for diagnostics");
    // 同时验证序列化后的公开响应，确保 NaN 和 Inf 不会进入 JSON。
    const auto frame = VnaWeb::ParseJson(VnaWeb::StringifyJson(Request(api, "GET", "/api/v1/frame").json));
    Require(Field(frame, "available").boolean && Field(frame, "current").boolean, "Frame identity remains current");
    const auto &trace = Field(frame, "traces").array.front();
    VerifyInvalidTrace(trace);
    Require(fixture.service.ErrorCount() == 0U, "HTTP diagnostics do not consume or add SCPI errors");
    fixture.device->SetInvalidSamples(false);
    const auto recovered = Accepted(
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"valid-again\"}", token));
    Require(StringField(Finish(api, recovered, token).json, "state") == "completed", "Next valid scan completes");
    const auto next = Request(api, "GET", "/api/v1/frame");
    for (const auto &valid : Field(Field(next.json, "traces").array.front(), "valid").array) {
        Require(valid.boolean, "Next valid scan restores every public sample");
    }
    Require(std::isnan(held->ports.front().iq[16U]) && std::isinf(held->ports.front().iq[33U]),
        "Retained invalid-data snapshot is unchanged by the next scan");
}

class CommaDecimal final : public std::numpunct<char> {
protected:
    char do_decimal_point() const override
    {
        return ',';
    }
};

class ScopedCommaLocale final {
public:
    ScopedCommaLocale() : previous_(std::locale::global(std::locale(std::locale::classic(), new CommaDecimal)))
    {
    }

    ~ScopedCommaLocale()
    {
        std::locale::global(previous_);
    }

private:
    std::locale previous_;
};

void JsonNumberStreamReuse()
{
    ScopedCommaLocale locale;
    std::string input = "[1.25,-0,-0.0,-0e+10,6.022e23,1e-30,7]";
    VnaWeb::JsonParser parser(input);
    for (unsigned pass = 0U; pass < 3U; ++pass) {
        const auto value = parser.Parse();
        Require(value.array.size() == 7U && value.array[0U].number == 1.25 && value.array[6U].number == 7.0,
            "Reused number stream resets its buffer and EOF state within and between documents");
        for (unsigned index = 1U; index <= 3U; ++index) {
            Require(value.array[index].number == 0.0 && std::signbit(value.array[index].number),
                "JSON negative zero retains its sign for integer, fraction and exponent spelling");
        }
        Require(value.array[4U].number == 6.022e23 && value.array[5U].number == 1e-30,
            "JSON dot decimal and exponents ignore the global comma locale");
    }
    for (const std::string bad : {"+1", "01", "-01", ".1", "1.", "1e", "1e+", "1e-", "--1", "0x10", "NaN", "Infinity",
             "-Infinity", "1,5", "[1,,2]", "[1,1e400]"}) {
        input = bad;
        bool rejected = false;
        try {
            parser.Parse();
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        Require(rejected, "Reused parser rejects invalid JSON number: " + bad);
        input = "-2.5";
        Require(parser.Parse().number == -2.5, "A failed parse does not poison the next number conversion");
    }
}

void CheckNumberCompatibility(const std::string &token)
{
    std::istringstream baseline(token);
    baseline.imbue(std::locale::classic());
    double expected = 0.0;
    baseline >> expected;
    const bool expectedAccepted = !baseline.fail() && baseline.eof() && std::isfinite(expected);
    Json actual;
    bool accepted = false;
    try {
        actual = VnaWeb::ParseJson(token);
        accepted = true;
    } catch (const std::runtime_error &) {
    }
    Require(accepted == expectedAccepted, "JSON boundary acceptance preserves stream semantics: " + token);
    if (accepted) {
        Require(actual.type == Json::Type::NUMBER && actual.number == expected &&
                    std::signbit(actual.number) == std::signbit(expected),
            "JSON boundary value and zero sign preserve stream semantics: " + token);
    }
}

void JsonNumberBoundaryCompatibility()
{
    ScopedCommaLocale locale;
    for (const std::string token : {"1.7976931348623157e308", "-1.7976931348623157e308", "1.7976931348623159e308",
             "2.2250738585072014e-308", "2.225073858507201e-308", "4.9406564584124654e-324", "-4.9406564584124654e-324",
             "1e-400", "-1e-400", "1e400", "-1e400", "0e999999999", "1", "-0",
             "3.166338562921711625857999285172825103424373299966255096425790206770411e-310",
             "-2.29652892184641754489269627148249438342304762894026871e-323"}) {
        CheckNumberCompatibility(token);
    }
}

void JsonNumberRoundingCompatibility()
{
    struct RoundingGuard final {
        int previous = std::fegetround();

        ~RoundingGuard()
        {
            std::fesetround(previous);
        }
    } guard;

    ScopedCommaLocale locale;
    for (const int rounding : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        if (std::fesetround(rounding) != 0) {
            continue;
        }
        std::mt19937_64 random(20261016U);
        for (unsigned index = 0U; index < 1000U; ++index) {
            const std::string token = (index % 2U == 0U ? "-" : "") + std::to_string(1U + random() % 9U) + "." +
                                      std::to_string(random()) + std::to_string(random()) + "e" +
                                      std::to_string(static_cast<int>(random() % 850U) - 425);
            CheckNumberCompatibility(token);
        }
        for (const std::string boundary :
            {"1.7976931348623157e308", "1.7976931348623159e308", "2.2250738585072014e-308", "2.225073858507201e-308",
                "4.9406564584124654e-324", "-4.9406564584124654e-324", "1e-400", "-1e-400", "0e999999999", "-0"}) {
            CheckNumberCompatibility(boundary);
        }
        CheckNumberCompatibility("1.00000000000000011102230246251565404236316680908203125");
    }
}

void JsonAndEndpoint()
{
    const auto value = VnaWeb::ParseJson("{\"escaped\":\"quote\\\";\\n\\u4e2d\",\"array\":[true,null,1.25]}");
    Require(StringField(VnaWeb::ParseJson(VnaWeb::StringifyJson(value)), "escaped") == StringField(value, "escaped"),
        "JSON round trip");
    for (const std::string bad : {"{\"x\":1,\"x\":2}", "{\"x\":NaN}", "{\"x\":01}", "[true,]"}) {
        bool rejected = false;
        try {
            VnaWeb::ParseJson(bad);
        } catch (const std::exception &) {
            rejected = true;
        }
        Require(rejected, "Malformed JSON rejected");
    }
    Fixture fixture;
    VnaWeb::WebOptions options;
    options.port = 0U;
    VnaWeb::WebEndpoint endpoint(fixture.service, options);
    Require(endpoint.Port() != 0U, "Ephemeral Web listener exposes port");
    endpoint.BeginShutdown();
    endpoint.Poll();
    Require(endpoint.Drained(), "Empty-root API endpoint shuts down");
}

std::string RatioBody(std::uint64_t revision, const std::string &kind, const std::string &name, unsigned port)
{
    return "{\"expectedRevision\":\"" + std::to_string(revision) + "\",\"requestId\":\"" + name + "\",\"name\":\"" +
           name + "\",\"kind\":\"" + kind + "\",\"receiverKind\":\"b\",\"receiverPort\":" + std::to_string(port) +
           ",\"sourcePort\":1}";
}

void RatioMetadataAndInvalidReference()
{
    Fixture fixture;
    HttpApi api(fixture.service);
    const auto token = Claim(api);
    const auto revision = fixture.service.Settings().revision;
    const auto bad = Request(api, "POST", "/api/v1/measurements", RatioBody(revision, "unknown", "bad", 1U), token);
    Require(bad.status == 400U && fixture.service.Settings().revision == revision, "Unknown kind cannot mutate");
    const auto definition =
        Accepted(Request(api, "POST", "/api/v1/measurements", RatioBody(revision, "sParameter", "reflect", 1U), token));
    Require(StringField(Finish(api, definition, token).json, "state") == "completed", "Define S11 via HTTP");
    const auto select = Accepted(Request(api, "POST", "/api/v1/display",
        "{\"requestId\":\"select-reflect\","
        "\"expectedDisplayRevision\":\"" +
            std::to_string(fixture.service.Display().revision) + "\",\"action\":\"select\",\"traceName\":\"reflect\"}",
        token));
    Require(StringField(Finish(api, select, token).json, "state") == "completed", "Display selection is explicit");
    std::vector<double> samples(16U, 0.0);
    samples[0] = 2.0;
    samples[2] = 0.5;
    fixture.device->SetSamples(samples);
    const auto single = Accepted(
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"ratio-valid\"}", token));
    Finish(api, single, token);
    auto trace = Field(Request(api, "GET", "/api/v1/frame").json, "traces").array.at(1U);
    Require(StringField(trace, "parameter") == "S11" && StringField(trace, "kind") == "sParameter" &&
                StringField(trace, "unit") == "1" && StringField(trace, "calibration") == "uncalibrated",
        "Ratio metadata does not imply calibration or receiver amplitude");
    Require(Field(trace, "real").array.front().number == 0.25 && Field(trace, "valid").array.front().boolean,
        "HTTP exports shared complex ratio");
    samples[0] = 0.0;
    fixture.device->SetSamples(samples);
    const auto invalid = Accepted(
        Request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"ratio-invalid\"}", token));
    Finish(api, invalid, token);
    trace = Field(Request(api, "GET", "/api/v1/frame").json, "traces").array.at(1U);
    Require(Field(trace, "available").boolean && !Field(trace, "valid").array.front().boolean &&
                Field(trace, "real").array.front().type == Json::Type::NULL_VALUE &&
                Field(trace, "imag").array.front().type == Json::Type::NULL_VALUE,
        "Zero reference stays explicit invalid instead of a fabricated zero S11");
    Require(fixture.service.ErrorCount() == 0U, "HTTP invalid reference leaves SCPI errors alone");
}
}  // 匿名命名空间

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {{"atomic-config-dedupe", AtomicAndDedupe},
        {"config-single-fifo", ConfigurationThenSingleFifo}, {"execution-revision-check", ExecutionRevisionCheck},
        {"release-expiry-shutdown", ReleasingAndLease}, {"snapshot-and-sampling", SnapshotAndSampling},
        {"operation-retention-budgets", OperationRetentionAndBudgets},
        {"long-operation-retention", LongOperationRetention},
        {"invalid-points-remain-explicit", InvalidPointsRemainExplicit},
        {"json-number-stream-reuse", JsonNumberStreamReuse},
        {"json-number-boundary-compatibility", JsonNumberBoundaryCompatibility},
        {"json-number-rounding-compatibility", JsonNumberRoundingCompatibility}, {"json-and-endpoint", JsonAndEndpoint},
        {"ratio-metadata-invalid-reference", RatioMetadataAndInvalidReference}};
    unsigned failures = 0;
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
