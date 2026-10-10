#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Fixture.h"
#include "web/HttpApi.h"
#include "web/Json.h"
#include "web/JsonEncoder.h"
#include "web/JsonParser.h"
#include "web/WebEndpoint.h"

namespace {
using vna_service_test::Fixture;
using vna_web::HttpApi;
using vna_web::HttpResponse;
using vna_web::Json;

void require(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}

const Json& field(const Json& value, const std::string& name)
{
    const Json* found = value.find(name);
    if (!found)
        throw std::runtime_error("Missing field: " + name);
    return *found;
}

std::string stringField(const Json& value, const std::string& name)
{
    const Json& found = field(value, name);
    require(found.type == Json::Type::String, "String field " + name);
    return found.string;
}

HttpResponse request(HttpApi& api, const std::string& method, const std::string& path, const std::string& body = "",
    const std::string& token = "")
{
    vna_web::HttpRequest value;
    value.method = method;
    value.path = path;
    value.body = body;
    if (!token.empty())
        value.headers["x-vna-session"] = token;
    return api.handle(value);
}

std::string claim(HttpApi& api)
{
    const auto response = request(api, "POST", "/api/v1/control/claim", "{}");
    require(response.status == 200U, "Claim succeeds");
    return stringField(response.json, "sessionId");
}

void until(const std::function<bool()>& done, const std::function<void()>& poll)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done()) {
        require(std::chrono::steady_clock::now() < end, "Timed out waiting for controlled task");
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

vna_service::OperationResult finish(vna_service::InstrumentService& service, const vna_service::Operation& operation)
{
    until(
        [&] {
            return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        },
        [&] {
            service.poll();
        });
    return operation.get();
}

HttpResponse finish(HttpApi& api, const std::string& operation, const std::string& token)
{
    HttpResponse response;
    until(
        [&] {
            response = request(api, "GET", "/api/v1/operations/" + operation, "", token);
            require(response.status == 200U, "Read owned operation");
            return stringField(response.json, "state") != "pending";
        },
        [&] {
            api.poll();
        });
    return response;
}

std::string accepted(const HttpResponse& response)
{
    require(response.status == 202U, "Operation accepted, received " + std::to_string(response.status));
    return stringField(response.json, "operationId");
}

std::string configBody(std::uint64_t revision, const std::string& id, unsigned points)
{
    return "{\"expectedRevision\":\"" + std::to_string(revision) + "\",\"requestId\":\"" + id +
           "\",\"points\":" + std::to_string(points) + "}";
}

void atomicAndDedupe()
{
    Fixture fixture;
    HttpApi api(*fixture.service);
    fixture.service->recordError(-113, "SCPI sentinel");
    const auto token = claim(api);
    require(!fixture.service->tryAcquireControl(), "Web excludes SCPI");
    const auto previous = fixture.service->settings().revision;
    const std::string body =
        "{\"expectedRevision\":\"" + std::to_string(previous) +
        "\",\"requestId\":\"atomic\",\"startHz\":2000000,\"stopHz\":3000000,\"points\":19,\"ifBandwidthHz\":101}";
    const auto id = accepted(request(api, "POST", "/api/v1/config", body, token));
    const auto complete = finish(api, id, token);
    require(stringField(complete.json, "state") == "completed", "Atomic config completed");
    const auto& settings = fixture.service->settings();
    require(settings.revision == previous + 1U && settings.points == 19U && settings.startHz == 2000000.0 &&
                settings.stopHz == 3000000.0 && settings.ifBandwidthHz == 1000.0,
        "Exactly one committed configuration");
    require(
        accepted(request(api, "POST", "/api/v1/config", body, token)) == id, "Dedupe precedes stale revision check");
    require(request(api, "POST", "/api/v1/config", configBody(previous, "atomic", 21U), token).status == 409U,
        "Conflicting retry rejected");
    require(request(api, "POST", "/api/v1/config", configBody(0U, "zero", 21U), token).status == 409U,
        "Revision zero is strict");
    require(
        request(api, "DELETE", "/api/v1/config", configBody(settings.revision, "wrong-method", 21U), token).status ==
            405U,
        "DELETE config does not mutate");
    require(settings.points == 19U && settings.revision == previous + 1U, "Wrong method leaves config unchanged");
    require(request(api, "GET", "/api/v1/operations/" + id).status == 403U, "Operation reads require owner token");
    require(request(api, "GET", "/api/v1/operations/" + id, "", "another").status == 403U,
        "Different token cannot read operation");
    request(api, "GET", "/api/v1/status");
    request(api, "GET", "/api/v1/frame");
    require(fixture.service->errorCount() == 1U && fixture.service->popError().code == -113,
        "Web leaves SCPI FIFO untouched");
}

void configurationThenSingleFifo()
{
    Fixture fixture;
    const std::string owner = "web-fifo";
    require(fixture.service->tryAcquireControl(owner), "Acquire typed owner");
    const auto revision = fixture.service->settings().revision;
    vna_service::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 23U;
    const auto config = fixture.service->applyConfiguration(patch, revision, owner);
    const auto single = fixture.service->runSingle(owner);
    require(fixture.service->settings().sweepMode == vna_service::SweepMode::Hold,
        "Queued Single does not change committed mode");
    require(finish(*fixture.service, config).ok, "Earlier configuration succeeds");
    require(finish(*fixture.service, single).ok, "Later single succeeds");
    const auto view = fixture.core->readView();
    require(view.frame && view.frame->configRevision == revision + 1U && view.frame->plan.frequencyHz.size() == 23U &&
                view.frame->runId == view.runId,
        "Single uses preceding committed configuration");
    require(fixture.service->settings().sweepMode == vna_service::SweepMode::Hold, "Single completes in HOLD");
}

void executionRevisionCheck()
{
    Fixture fixture;
    const std::string owner = "web-cas";
    require(fixture.service->tryAcquireControl(owner), "Acquire typed owner");
    const auto revision = fixture.service->settings().revision;
    vna_service::ConfigPatch first;
    first.hasPoints = true;
    first.points = 17U;
    vna_service::ConfigPatch second;
    second.hasPoints = true;
    second.points = 29U;
    const auto a = fixture.service->applyConfiguration(first, revision, owner);
    const auto b = fixture.service->applyConfiguration(second, revision, owner);
    require(finish(*fixture.service, a).ok, "First configuration commits");
    require(!finish(*fixture.service, b).ok, "Execution rechecks expected revision");
    require(fixture.service->settings().points == 17U && fixture.service->settings().revision == revision + 1U,
        "CAS failure leaves previous commit");
    require(!finish(*fixture.service, fixture.service->applyConfiguration(second, 0U, owner)).ok,
        "Typed zero revision cannot bypass CAS");
    require(fixture.service->errorCount() == 0U, "Web CAS failures do not enter SCPI FIFO");
}

void releasingAndLease()
{
    for (unsigned scenario = 0; scenario < 3U; ++scenario) {
        Fixture fixture;
        auto time = HttpApi::Clock::time_point();
        HttpApi api(*fixture.service, [&] {
            return time;
        });
        const auto token = claim(api);
        fixture.device->arm();
        const auto id = accepted(
            request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"gated\"}", token));
        until(
            [&] {
                return fixture.device->hasEntered();
            },
            [&] {
                api.poll();
            });
        if (scenario == 0U)
            require(request(api, "POST", "/api/v1/control/release", "{}", token).status == 200U,
                "Explicit release accepted");
        if (scenario == 1U) {
            time += std::chrono::seconds(31);
            // No poll between clock jump and claim: admission must enforce expiry.
            require(request(api, "POST", "/api/v1/control/claim", "{\"sessionId\":\"" + token + "\"}").status == 409U,
                "Expired lease cannot renew past accepted operation drain");
        }
        if (scenario == 2U)
            api.beginShutdown();
        require(fixture.service->controlReleasing() && !fixture.service->tryAcquireControl(),
            "Pending controller retains releasing ownership");
        require(!api.drained(), "Shutdown drain waits accepted operation");
        require(request(api, "GET", "/api/v1/operations/" + id, "", token).status == 200U,
            "Released owner still reads accepted operation");
        const auto rejected =
            request(api, "POST", "/api/v1/operations", "{\"action\":\"hold\",\"requestId\":\"late\"}", token);
        require(rejected.status == (scenario == 2U ? 503U : 409U), "No new writes during release");
        fixture.device->release();
        require(
            stringField(finish(api, id, token).json, "state") == "completed", "Accepted operation survives release");
        api.poll();
        require(api.drained() && fixture.service->tryAcquireControl(), "SCPI acquires after drain");
        require(fixture.service->errorCount() == 0U, "Lease conflicts leave SCPI errors untouched");
    }
}

void snapshotAndSampling()
{
    Fixture fixture;
    HttpApi api(*fixture.service);
    const auto token = claim(api);
    auto response = request(api, "GET", "/api/v1/frame");
    require(!field(response.json, "available").boolean, "No fabricated initial frame");
    const auto config = accepted(request(
        api, "POST", "/api/v1/config", configBody(fixture.service->settings().revision, "large", 10001U), token));
    require(stringField(finish(api, config, token).json, "state") == "completed", "Large config completes");
    const auto single =
        accepted(request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"first\"}", token));
    require(stringField(finish(api, single, token).json, "state") == "completed", "Large single completes");
    response = request(api, "GET", "/api/v1/frame");
    require(field(response.json, "current").boolean && field(response.json, "decimated").boolean,
        "Completed frame current and sampled");
    require(field(response.json, "originalPointCount").number == 10001.0 &&
                field(response.json, "displayPointCount").number == 2001.0,
        "Sampling metadata truthful");
    const auto& axis = field(response.json, "frequencyHz").array;
    require(axis.size() == 2001U && axis.front().number == 1000000.0 && axis.back().number == 100000000.0,
        "Sampling includes endpoints");
    const auto& traces = field(response.json, "traces").array;
    require(traces.size() == 1U && field(traces.front(), "real").array.size() == axis.size() &&
                field(traces.front(), "valid").array.front().boolean,
        "Trace arrays share the frequency snapshot");
    fixture.device->arm();
    accepted(request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"second\"}", token));
    response = request(api, "GET", "/api/v1/frame");
    require(field(response.json, "available").boolean && !field(response.json, "current").boolean,
        "Pending finite operation invalidates prior frame immediately");
    fixture.device->release();
    until(
        [&] {
            return !fixture.service->hasPending();
        },
        [&] {
            api.poll();
        });
}

void operationRetentionAndBudgets()
{
    Fixture fixture;
    auto time = HttpApi::Clock::time_point();
    HttpApi api(*fixture.service, [&] {
        return time;
    });
    const auto token = claim(api);
    std::string first;
    for (unsigned index = 0; index < 128U; ++index) {
        const auto id = accepted(request(api, "POST", "/api/v1/operations",
            "{\"action\":\"hold\",\"requestId\":\"hold-" + std::to_string(index) + "\"}", token));
        require(stringField(finish(api, id, token).json, "state") == "completed", "Bounded record completes");
        if (index == 0U)
            first = id;
    }
    require(
        request(api, "POST", "/api/v1/operations", "{\"action\":\"hold\",\"requestId\":\"overflow\"}", token).status ==
            429U,
        "The 129th retained operation is rejected");
    time += std::chrono::seconds(59);
    require(request(api, "GET", "/api/v1/operations/" + first, "", token).status == 200U,
        "Released token reads terminal result for 59 seconds");
    time += std::chrono::seconds(1);
    require(request(api, "GET", "/api/v1/operations/" + first, "", token).status == 404U,
        "Terminal record expires after 60 seconds");

    // Pending operations survive a long native call. Retention starts when the
    // operation completes, rather than when it enters the device queue.
    const auto secondToken = claim(api);
    fixture.device->arm();
    const auto gated = accepted(request(
        api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"long-native\"}", secondToken));
    until(
        [&] {
            return fixture.device->hasEntered();
        },
        [&] {
            api.poll();
        });
    time += std::chrono::seconds(100);
    const auto pending = request(api, "GET", "/api/v1/operations/" + gated, "", secondToken);
    require(pending.status == 200U && stringField(pending.json, "state") == "pending",
        "Pending result survives record TTL and expired lease");
    require(fixture.service->controlReleasing(), "Long native call keeps expired controller releasing");
    fixture.device->release();
    require(
        stringField(finish(api, gated, secondToken).json, "state") == "completed", "Long accepted operation completes");
    time += std::chrono::seconds(59);
    require(
        request(api, "GET", "/api/v1/operations/" + gated, "", secondToken).status == 200U, "TTL starts at completion");
    time += std::chrono::seconds(1);
    require(request(api, "GET", "/api/v1/operations/" + gated, "", secondToken).status == 404U,
        "Completed long operation eventually expires");
}

void invalidPointsRemainExplicit()
{
    Fixture fixture;
    HttpApi api(*fixture.service);
    const auto token = claim(api);
    fixture.device->setInvalidSamples(true);
    const auto id = accepted(
        request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"invalid-samples\"}", token));
    require(stringField(finish(api, id, token).json, "state") == "completed", "Full invalid-data frame completes");
    const auto held = fixture.core->readView().frame;
    require(held && std::isnan(held->ports.front().iq[16U]) && std::isinf(held->ports.front().iq[33U]),
        "Core preserves received invalid samples for diagnostics");
    // Verify the serialized public response too: NaN/Inf must never enter JSON.
    const auto frame = vna_web::parseJson(vna_web::stringifyJson(request(api, "GET", "/api/v1/frame").json));
    require(field(frame, "available").boolean && field(frame, "current").boolean, "Frame identity remains current");
    const auto& trace = field(frame, "traces").array.front();
    const auto& real = field(trace, "real").array;
    const auto& imaginary = field(trace, "imag").array;
    const auto& validity = field(trace, "valid").array;
    require(real.size() == 201U && imaginary.size() == 201U && validity.size() == 201U,
        "Invalid points retain their axis positions");
    for (std::size_t point = 0U; point < validity.size(); ++point) {
        const bool invalid = point == 1U || point == 2U;
        require(validity[point].boolean == !invalid, "Only the affected IQ points are invalid");
        if (invalid) {
            require(real[point].type == Json::Type::Null && imaginary[point].type == Json::Type::Null,
                "Either invalid component makes both public components null");
        } else {
            require(real[point].type == Json::Type::Number && imaginary[point].type == Json::Type::Number &&
                        real[point].number == 1.11 && imaginary[point].number == 1.11,
                "Valid neighboring samples remain exact");
        }
    }
    require(fixture.service->errorCount() == 0U, "HTTP diagnostics do not consume or add SCPI errors");
    fixture.device->setInvalidSamples(false);
    const auto recovered = accepted(
        request(api, "POST", "/api/v1/operations", "{\"action\":\"single\",\"requestId\":\"valid-again\"}", token));
    require(stringField(finish(api, recovered, token).json, "state") == "completed", "Next valid scan completes");
    const auto next = request(api, "GET", "/api/v1/frame");
    for (const auto& valid : field(field(next.json, "traces").array.front(), "valid").array)
        require(valid.boolean, "Next valid scan restores every public sample");
    require(std::isnan(held->ports.front().iq[16U]) && std::isinf(held->ports.front().iq[33U]),
        "Retained invalid-data snapshot is unchanged by the next scan");
}

void jsonAndEndpoint()
{
    const auto value = vna_web::parseJson("{\"escaped\":\"quote\\\";\\n\\u4e2d\",\"array\":[true,null,1.25]}");
    require(stringField(vna_web::parseJson(vna_web::stringifyJson(value)), "escaped") == stringField(value, "escaped"),
        "JSON round trip");
    for (const std::string bad : {"{\"x\":1,\"x\":2}", "{\"x\":NaN}", "{\"x\":01}", "[true,]"}) {
        bool rejected = false;
        try {
            vna_web::parseJson(bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Malformed JSON rejected");
    }
    Fixture fixture;
    vna_web::WebOptions options;
    options.port = 0U;
    vna_web::WebEndpoint endpoint(*fixture.service, options);
    require(endpoint.port() != 0U, "Ephemeral Web listener exposes port");
    endpoint.beginShutdown();
    endpoint.poll();
    require(endpoint.drained(), "Empty-root API endpoint shuts down");
}
}  // namespace

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {{"atomic-config-dedupe", atomicAndDedupe},
        {"config-single-fifo", configurationThenSingleFifo}, {"execution-revision-check", executionRevisionCheck},
        {"release-expiry-shutdown", releasingAndLease}, {"snapshot-and-sampling", snapshotAndSampling},
        {"operation-retention-budgets", operationRetentionAndBudgets},
        {"invalid-points-remain-explicit", invalidPointsRemainExplicit}, {"json-and-endpoint", jsonAndEndpoint}};
    unsigned failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures == 0U ? 0 : 1;
}
