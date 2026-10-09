#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "device/DeviceSession.h"
#include "vna_core/AcquisitionCore.h"

namespace vna_core {
namespace detail {

class CoreTestAccess {
public:
    static std::unique_ptr<AcquisitionCore> create(std::unique_ptr<DeviceSession> device)
    {
        return std::unique_ptr<AcquisitionCore>(new AcquisitionCore(std::move(device)));
    }
};

}  // namespace detail
}  // namespace vna_core

namespace {
using namespace vna_core;
using namespace std::chrono;

void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

CommandResult await(const Ticket& ticket)
{
    require(ticket.wait_for(seconds(3)) == std::future_status::ready, "ticket did not terminate");
    return ticket.get();
}

SweepPlan makePlan()
{
    SweepPlan plan;
    plan.frequencyRaw = {UINT64_C(1000000), UINT64_C(0x100000001)};
    plan.frequencyHz = {1000000.0, 4294967297.0};
    plan.ifBandwidthRaw = 1000U;
    plan.receiverMask = 0xFU;
    plan.sourcePorts = {1U, 4U};
    plan.tableData.assign(18U, 0.5);
    return plan;
}

class ControlledDevice : public detail::DeviceSession {
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool blockNext = false;
    bool entered = false;
    bool release = false;
    bool failApply = false;
    bool failPrepare = false;
    bool failShutdown = false;
    std::size_t failAcquireAt = 0U;
    std::size_t acquisitions = 0U;
    std::thread::id deviceThread;
    bool wrongThread = false;
    bool shutdownCalled = false;
    SweepPlan applied;

    std::string initialize() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        return "TestDevice";
    }
    void applyPlan(const SweepPlan& plan) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        if (failPrepare) {
            failPrepare = false;
            throw detail::PlanPreparationError();
        }
        applied = plan;  // Simulate a partial mutation before reporting failure.
        if (failApply) {
            failApply = false;
            throw std::runtime_error("controlled partial configuration failure");
        }
    }
    void enableRf() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
    }
    std::vector<double> acquirePort(const SweepPlan& plan, std::uint32_t source) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        recordThread();
        ++acquisitions;
        cv.notify_all();
        if (blockNext) {
            blockNext = false;
            entered = true;
            cv.notify_all();
            cv.wait(lock, [this] { return release; });
        }
        if (acquisitions == failAcquireAt) {
            throw std::runtime_error("controlled second-port acquisition failure");
        }
        return std::vector<double>(16U * plan.frequencyRaw.size(), static_cast<double>(source));
    }
    void shutdown() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        shutdownCalled = true;
        if (failShutdown) {
            throw std::runtime_error("controlled RF-off failure");
        }
    }
    void arm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blockNext = true;
        entered = release = false;
    }
    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(cv.wait_for(lock, seconds(3), [this] { return entered; }), "gate was never entered");
    }
    void releaseGate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        cv.notify_all();
    }
    void recordThread()
    {
        if (deviceThread == std::thread::id()) {
            deviceThread = std::this_thread::get_id();
        }
        wrongThread = wrongThread || deviceThread != std::this_thread::get_id();
    }
};

struct Fixture {
    ControlledDevice* device;
    std::unique_ptr<AcquisitionCore> core;
    Fixture()
    {
        auto owned = std::unique_ptr<ControlledDevice>(new ControlledDevice);
        device = owned.get();
        core = detail::CoreTestAccess::create(std::move(owned));
        require(await(core->initialize()).ok, "initialize failed");
        require(core->readView().deviceIdn == "TestDevice", "identity was not published");
        require(await(core->configure(makePlan())).ok, "configure failed");
    }
    ~Fixture()
    {
        device->releaseGate();
        core->close();
    }
};

void testStopFifo()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(started.ok, "manual start failed");
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "initial frame failed");
    const auto old = f.core->readView().frame;
    f.device->arm();
    const auto inFlight = f.core->triggerOnce(started.runId, started.configRevision);
    f.device->waitEntered();
    const auto priorTrigger = f.core->triggerOnce(started.runId, started.configRevision);
    const auto stop = f.core->stop(started.runId);
    const auto laterTrigger = f.core->triggerOnce(started.runId, started.configRevision);
    require(stop.wait_for(milliseconds(80)) == std::future_status::timeout, "Stop passed an unfinished frame");
    require(f.core->readView().frame == old, "partial frame became visible");
    f.device->releaseGate();
    require(await(inFlight).ok && await(priorTrigger).ok, "prior FIFO triggers did not execute");
    const auto stopped = await(stop);
    require(stopped.ok && stopped.frameSeq == 3U, "Stop did not cover preceding frames");
    require(!await(laterTrigger).ok, "trigger behind Stop succeeded");
    require(f.core->readView().state == CoreState::Ready, "Stop did not publish Ready");
    require(
        f.core->readView().frame->ports.size() == 2U && old->frameSeq == 1U, "complete immutable frame was modified");
    require(await(f.core->stop(started.runId)).ok, "matching idle Stop was not idempotent");
    const auto restarted = await(f.core->start(RunMode::Manual));
    require(restarted.runId == started.runId + 1U, "run identity did not advance");
    require(!await(f.core->stop(started.runId)).ok, "stale Stop affected a new run");
}

void testContinuousStop()
{
    Fixture f;
    f.device->arm();
    const auto started = await(f.core->start(RunMode::Continuous));
    f.device->waitEntered();
    const auto stop = f.core->stop(started.runId);
    require(stop.wait_for(milliseconds(80)) == std::future_status::timeout, "continuous Stop passed device I/O");
    f.device->releaseGate();
    require(await(stop).ok, "continuous Stop failed");
    const auto view = f.core->readView();
    require(view.state == CoreState::Ready && view.frame && view.frame->ports.size() == 2U,
        "continuous frame was not fully committed before Stop");
    std::unique_lock<std::mutex> lock(f.device->mutex);
    const auto acquisitions = f.device->acquisitions;
    require(!f.device->cv.wait_for(
                lock, milliseconds(80), [&f, acquisitions] { return f.device->acquisitions != acquisitions; }),
        "old run acquired another port after Stop confirmation");
}

void testConfigurationFailureAndRecover()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    require(await(f.core->stop(started.runId)).ok, "baseline stop failed");
    const auto old = f.core->readView();
    auto replacement = makePlan();
    replacement.frequencyRaw[0] = 77U;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        f.device->failApply = true;
    }
    require(!await(f.core->configure(replacement)).ok, "partial configuration failure was hidden");
    const auto failed = f.core->readView();
    require(
        failed.state == CoreState::Faulted && failed.configRevision == old.configRevision && failed.frame == old.frame,
        "failed configuration committed a revision or replaced the frame");
    require(await(f.core->stop(started.runId)).ok && f.core->readView().state == CoreState::Faulted,
        "matching Stop erased a persistent fault");
    require(!await(f.core->start(RunMode::Manual)).ok, "Faulted core restarted without recovery");
    const auto recovered = await(f.core->recover());
    require(recovered.ok && recovered.configRevision == old.configRevision + 1U,
        "recover did not completely restore and version the last successful plan");
    require(f.device->applied.frequencyRaw == makePlan().frequencyRaw,
        "recover restored the failed candidate instead of the last successful plan");
    require(f.core->readView().state == CoreState::Ready && f.core->readView().error.empty(),
        "recover resumed automatically or retained the old fault");
}

void testAcquisitionFailureDoesNotPublish()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    const auto old = f.core->readView().frame;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        f.device->failAcquireAt = f.device->acquisitions + 2U;
    }
    require(!await(f.core->triggerOnce(started.runId, started.configRevision)).ok,
        "second-port failure did not fail its trigger ticket");
    require(f.core->readView().frame == old && f.core->readView().state == CoreState::Faulted,
        "second-port failure published a partial frame");
}

void testPreparationFailurePreservesConfiguration()
{
    Fixture f;
    const auto old = f.core->readView();
    auto candidate = makePlan();
    candidate.frequencyRaw[0] = 77U;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        f.device->failPrepare = true;
    }
    require(!await(f.core->configure(candidate)).ok, "preparation failure was hidden");
    require(f.core->readView().state == CoreState::Ready && f.core->readView().configRevision == old.configRevision &&
                f.core->readView().error.empty(),
        "preparation failure invalidated a previously valid configuration");
    require(f.device->applied.frequencyRaw == makePlan().frequencyRaw, "preparation failure mutated the device");
    const auto started = await(f.core->start(RunMode::Manual));
    require(started.ok && await(f.core->triggerOnce(started.runId, started.configRevision)).ok,
        "previous valid configuration could not be used after preparation failure");
    require(!await(f.core->stop(0U)).ok, "zero run identifier was accepted");
}

struct ReleaseGateOnExit {
    ControlledDevice* device;
    ~ReleaseGateOnExit() { device->releaseGate(); }
};

void testCloseReservedAndTicketsTerminate()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    f.device->arm();
    const auto inFlight = f.core->triggerOnce(started.runId, started.configRevision);
    f.device->waitEntered();
    std::vector<Ticket> tickets;
    std::size_t rejected = 0U;
    for (std::size_t index = 0U; index < 80U; ++index) {
        const auto ticket = f.core->triggerOnce(started.runId, started.configRevision);
        if (ticket.wait_for(milliseconds(0)) == std::future_status::ready) {
            require(!ticket.get().ok, "full queue rejection pretended success");
            ++rejected;
        }
        tickets.push_back(ticket);
    }
    require(rejected == 16U, "normal queue capacity was not enforced");
    auto closing = std::async(std::launch::async, [&f] { return f.core->close(); });
    ReleaseGateOnExit releaseOnExit{f.device};
    bool admissionClosed = false;
    const auto deadline = steady_clock::now() + seconds(3);
    while (!admissionClosed && steady_clock::now() < deadline) {
        const auto answer = await(f.core->initialize());
        admissionClosed = answer.message.find("closing") != std::string::npos;
        std::this_thread::yield();
    }
    require(admissionClosed, "Close did not shut admission while the queue was full");
    require(closing.wait_for(milliseconds(80)) == std::future_status::timeout,
        "Close returned without waiting for in-flight device I/O");
    f.device->releaseGate();
    require(await(inFlight).ok, "in-flight ticket was abandoned by Close");
    for (const auto& ticket : tickets) {
        await(ticket);
    }
    require(closing.get().ok && f.core->readView().state == CoreState::Closed,
        "reserved Close entry failed to drain and join");
    require(f.device->shutdownCalled && !f.device->wrongThread && f.device->deviceThread != std::this_thread::get_id(),
        "device I/O escaped its single worker");
    require(!await(f.core->configure(makePlan())).ok, "closed core accepted new work");
}

void testOwnedInputAndShutdownError()
{
    Fixture f;
    auto input = makePlan();
    const auto configured = f.core->configure(input);
    input.frequencyRaw[0] = 42U;
    input.tableData.clear();
    require(await(configured).ok, "owned input failed to apply");
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "owned input scan failed");
    require(
        f.core->readView().frame->plan.frequencyRaw[0] == UINT64_C(1000000), "caller mutation changed the frozen plan");
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        f.device->failShutdown = true;
    }
    require(!f.core->close().ok && f.core->readView().state == CoreState::Closed,
        "RF shutdown error prevented terminal Closed state");
}

}  // namespace

int main()
{
    try {
        testStopFifo();
        testContinuousStop();
        testConfigurationFailureAndRecover();
        testAcquisitionFailureDoesNotPublish();
        testPreparationFailurePreservesConfiguration();
        testCloseReservedAndTicketsTerminate();
        testOwnedInputAndShutdownError();
        std::cout << "core concurrency: 7 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
