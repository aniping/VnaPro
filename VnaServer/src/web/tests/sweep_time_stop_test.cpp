/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "scpi/scpi_engine.h"
#include "vna_core/frame_layout.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using VnaWeb::Json;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const Json &Field(const Json &value, const std::string &key)
{
    const auto field = value.Find(key);
    Require(field != nullptr, "Missing field: " + key);
    return *field;
}

class TimedDevice final : public VnaCore::Detail::DeviceSession {
public:
    std::atomic<bool> entered{false};
    std::atomic<bool> interrupted{false};

    std::string Initialize() override
    {
        return "VnaPro,NativeSimulator,4PORT,1.0";
    }

    void ApplyPlan(const VnaCore::SweepPlan &) override
    {
    }

    void EnableRf() override
    {
    }

    void Shutdown() override
    {
    }

    bool ManualSweepTimeSupported() const noexcept override
    {
        return true;
    }

    std::vector<VnaCore::PortFrame> AcquireSweep(const VnaCore::SweepPlan &plan) override
    {
        return AcquireSweep(plan, {}, {});
    }

    std::vector<VnaCore::PortFrame> AcquireSweep(const VnaCore::SweepPlan &plan,
        const VnaCore::Detail::SweepProgressCallback &progress, const VnaCore::Detail::SweepReadGate &gate) override
    {
        std::vector<VnaCore::PortFrame> ports;
        for (const auto source : plan.sourcePorts) {
            VnaCore::PortFrame port;
            port.sourcePort = source;
            port.iq.assign(plan.frequencyRaw.size() * VnaCore::IQ_VALUES_PER_POINT, 1.0);
            ports.push_back(std::move(port));
        }
        if (!plan.sweepTimeAuto && gate) {
            entered.store(true);
            interrupted.store(!gate(plan.frequencyRaw.size()));
        }
        static_cast<void>(progress);
        return ports;
    }
};

VnaWeb::HttpResponse Request(
    VnaWeb::HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &owner = "")
{
    VnaWeb::HttpRequest request;
    request.path = path;
    request.method = body.type == Json::Type::NULL_VALUE ? "GET" : "POST";
    request.body = body.type == Json::Type::NULL_VALUE ? "" : VnaWeb::StringifyJson(body);
    if (!owner.empty()) {
        request.headers["x-vna-session"] = owner;
    }
    return api.Handle(request);
}

struct Fixture final {
    std::unique_ptr<TimedDevice> owned = std::make_unique<TimedDevice>();
    TimedDevice *device = owned.get();
    VnaCore::AcquisitionCore core{std::move(owned)};
    VnaService::InstrumentService service{core};
    VnaWeb::HttpApi api{service};
    std::string owner;

    explicit Fixture(bool claimWeb = true)
    {
        Require(core.Initialize().get().ok, "Initialize timed device");
        std::string error;
        Require(service.BootstrapProfile(error), error);
        if (claimWeb) {
            owner = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
        }
    }

    ~Fixture()
    {
        core.Close();
    }

    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    Fixture(Fixture &&) = delete;
    Fixture &operator=(Fixture &&) = delete;
};

Json Body(Fixture &fixture, const std::string &id, const std::string &action = "")
{
    auto body = Json::MakeObject();
    body["requestId"] = Json(id);
    if (action.empty() || action == "new") {
        body["expectedRevision"] = Json(std::to_string(fixture.service.Settings().revision));
    }
    if (!action.empty()) {
        body["action"] = Json(action);
    }
    return body;
}

std::string Accepted(const VnaWeb::HttpResponse &response)
{
    Require(response.status == 202U, "Expected accepted public operation: " + VnaWeb::StringifyJson(response.json));
    return Field(response.json, "operationId").string;
}

void Complete(Fixture &fixture, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        const auto result = Request(fixture.api, "/api/v1/operations/" + id, Json(), fixture.owner);
        const auto state = Field(result.json, "state").string;
        if (state != "pending") {
            Require(state == "completed", "Public operation failed: " + VnaWeb::StringifyJson(result.json));
            return;
        }
        Require(std::chrono::steady_clock::now() < deadline,
            "Accepted HTTP Stop remained behind the long Single instead of waking its software wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string StartLongSingle(Fixture &fixture)
{
    auto config = Body(fixture, "long-time");
    config["sweepTimeSeconds"] = Json(20.0);
    Complete(fixture, Accepted(Request(fixture.api, "/api/v1/config", config, fixture.owner)));
    return Accepted(
        Request(fixture.api, "/api/v1/sweep-control", Body(fixture, "long-single", "single"), fixture.owner));
}

void WaitForSoftwareGate(Fixture &fixture)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!fixture.device->entered.load()) {
        fixture.api.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Single did not enter its real Core software wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void AcceptedHttpStopWakesSingleAndPreservesQueuedConfiguration()
{
    Fixture fixture;
    const auto single = StartLongSingle(fixture);
    WaitForSoftwareGate(fixture);
    auto config = Body(fixture, "queued-points");
    config["points"] = Json(5U);
    const auto queued = Accepted(Request(fixture.api, "/api/v1/config", config, fixture.owner));
    const auto denied = Request(fixture.api, "/api/v1/sweep-control", Body(fixture, "denied-stop", "stop"), "intruder");
    Require(denied.status == 403U && !fixture.device->interrupted.load(), "Rejected Stop must not wake the Core gate");
    const auto stop =
        Accepted(Request(fixture.api, "/api/v1/sweep-control", Body(fixture, "real-stop", "stop"), fixture.owner));
    Complete(fixture, stop);
    Complete(fixture, single);
    Complete(fixture, queued);
    Require(fixture.device->interrupted.load(), "Accepted HTTP Stop must reach the active Core software wait");
    Require(
        fixture.service.Settings().points == 5U && fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "Stop cannot drop or reorder a previously queued configuration");
    const auto frame = fixture.core.ReadView().frame;
    Require(frame && frame->plan.frequencyRaw.size() == 201U && frame->ports.front().iq.size() == 201U * 16U,
        "Stopping software pacing must safely drain and retain a complete frame, never partial IQ");
}

void StopAcceptedBeforeSingleEntersWaitStillWorks()
{
    Fixture fixture;
    const auto single = StartLongSingle(fixture);
    const auto stop =
        Accepted(Request(fixture.api, "/api/v1/sweep-control", Body(fixture, "early-stop", "stop"), fixture.owner));
    Complete(fixture, stop);
    Complete(fixture, single);
    Require(fixture.device->interrupted.load(), "Queued Stop intent must survive the Single startup race");
}

void OtherChannelStopCannotWakeCurrentSingle()
{
    Fixture fixture;
    auto create = Body(fixture, "new-channel", "new");
    create["expectedDisplayRevision"] = Json(std::to_string(fixture.service.Display().revision));
    Complete(fixture, Accepted(Request(fixture.api, "/api/v1/channels", create, fixture.owner)));
    Require(fixture.service.ActiveChannelId() == 2U, "New channel must be explicitly selected for the isolation case");
    Complete(fixture,
        Accepted(Request(fixture.api, "/api/v1/sweep-control", Body(fixture, "new-hold", "stop"), fixture.owner)));
    const auto single = StartLongSingle(fixture);
    WaitForSoftwareGate(fixture);
    const auto retained = fixture.core.ReadView(2U).frame;
    auto other = Body(fixture, "other-stop", "stop");
    other["channelId"] = Json(1U);
    const auto otherStop = Accepted(Request(fixture.api, "/api/v1/sweep-control", other, fixture.owner));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    do {
        fixture.api.Poll();
        const auto state = Request(fixture.api, "/api/v1/operations/" + single, Json(), fixture.owner);
        Require(Field(state.json, "state").string == "pending" && !fixture.device->interrupted.load() &&
                    fixture.core.ReadView(2U).frame == retained,
            "Stopping another channel must not wake the current Single: state=" + Field(state.json, "state").string +
                ", interrupted=" + std::to_string(fixture.device->interrupted.load()) +
                ", retained=" + std::to_string(fixture.core.ReadView(2U).frame == retained));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    auto correct = Body(fixture, "target-stop", "stop");
    correct["channelId"] = Json(2U);
    Complete(fixture, Accepted(Request(fixture.api, "/api/v1/sweep-control", correct, fixture.owner)));
    Complete(fixture, single);
    Complete(fixture, otherStop);
}

void SameScpiConnectionCanHoldItsAsynchronousSingle()
{
    Fixture fixture(false);
    VnaScpi::ScpiEngine engine(fixture.service);
    Require(engine.SubmitLine("SENS:SWE:TIME 20;:SENS:SWE:MODE SING"), "Submit slow SCPI Single");
    const auto started = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!fixture.device->entered.load()) {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < started, "SCPI Single did not enter software wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(engine.SubmitLine("SENS:SWE:MODE HOLD"), "Same SCPI controller must accept HOLD without an OPC barrier");
    const auto stopped = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < stopped, "SCPI HOLD did not wake its asynchronous Single");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (engine.HasPending());
    Require(fixture.device->interrupted.load() && fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "SCPI HOLD must interrupt software pacing and retain normal Single completion");
}
}  // 匿名命名空间

int main()
{
    try {
        AcceptedHttpStopWakesSingleAndPreservesQueuedConfiguration();
        StopAcceptedBeforeSingleEntersWaitStillWorks();
        OtherChannelStopCannotWakeCurrentSingle();
        SameScpiConnectionCanHoldItsAsynchronousSingle();
        std::cout << "PASS public HTTP/SCPI Stop wakes long Single with ownership, channel and FIFO isolation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
