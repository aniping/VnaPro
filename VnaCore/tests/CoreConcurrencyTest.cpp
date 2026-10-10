#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
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

SweepPlan makePlan(std::uint32_t variant = 0U)
{
    SweepPlan plan;
    plan.frequencyRaw = {UINT64_C(1000000) + variant, UINT64_C(0x100000001) + variant};
    if (variant % 2U != 0U) {
        plan.frequencyRaw.push_back(UINT64_C(0x200000003) + variant);
    }
    for (const auto frequency : plan.frequencyRaw) {
        plan.frequencyHz.push_back(static_cast<double>(frequency));
    }
    plan.ifBandwidthRaw = 1000U + variant;
    plan.receiverMask = variant % 2U == 0U ? 0xFU : 0x5U;
    plan.sourcePorts = variant % 2U == 0U ? std::vector<std::uint32_t>{1U, 4U} :
                                           std::vector<std::uint32_t>{4U, 2U, 1U};
    plan.tableData.assign(9U * plan.frequencyRaw.size(), 0.5 + static_cast<double>(variant) / 1024.0);
    return plan;
}

bool samePlan(const SweepPlan& left, const SweepPlan& right)
{
    return left.frequencyRaw == right.frequencyRaw && left.frequencyHz == right.frequencyHz &&
           left.ifBandwidthRaw == right.ifBandwidthRaw && left.receiverMask == right.receiverMask &&
           left.sourcePorts == right.sourcePorts && left.tableData == right.tableData;
}

double sampleValue(const SweepPlan& plan, std::uint64_t frame, std::uint32_t source, std::size_t point,
    std::size_t receiver, std::size_t component)
{
    // Exact binary fields distinguish configuration, frame, source, point and all 16 IQ slots.
    return static_cast<double>(frame) * 68719476736.0 + static_cast<double>(plan.ifBandwidthRaw) * 1048576.0 +
           static_cast<double>(source) * 65536.0 + static_cast<double>(point) * 256.0 +
           static_cast<double>(receiver) * 16.0 + static_cast<double>(component) +
           static_cast<double>(plan.frequencyRaw[point] & UINT64_C(0x3FF)) / 1024.0;
}

void requireFrameContent(const MeasurementFrame& frame)
{
    require(frame.ports.size() == frame.plan.sourcePorts.size(), "published frame is missing a source");
    for (std::size_t source = 0U; source < frame.ports.size(); ++source) {
        const auto& port = frame.ports[source];
        require(port.sourcePort == frame.plan.sourcePorts[source], "published sources were mixed or reordered");
        require(port.iq.size() == 16U * frame.plan.frequencyRaw.size(), "published IQ shape is incomplete");
        for (std::size_t point = 0U; point < frame.plan.frequencyRaw.size(); ++point) {
            for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
                for (std::size_t component = 0U; component < 4U; ++component) {
                    require(port.iq[16U * point + 4U * receiver + component] ==
                                sampleValue(frame.plan, frame.frameSeq, port.sourcePort, point, receiver, component),
                        "published IQ mixes configurations, frames, points or receivers");
                }
            }
        }
    }
}

void requireUnchanged(const MeasurementFrame& frame, const MeasurementFrame& snapshot)
{
    require(frame.channelId == snapshot.channelId && frame.runId == snapshot.runId &&
                frame.configRevision == snapshot.configRevision && frame.frameSeq == snapshot.frameSeq &&
                samePlan(frame.plan, snapshot.plan) && frame.ports.size() == snapshot.ports.size(),
        "retained frame metadata or full plan was mutated");
    for (std::size_t source = 0U; source < frame.ports.size(); ++source) {
        require(frame.ports[source].sourcePort == snapshot.ports[source].sourcePort &&
                    frame.ports[source].iq == snapshot.ports[source].iq,
            "retained frame source or IQ buffer was mutated");
    }
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
    std::size_t shortAcquireAt = 0U;
    std::size_t acquisitions = 0U;
    std::uint64_t acquisitionFrame = 0U;
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
        acquisitionFrame = 0U;
    }
    std::vector<double> acquirePort(const SweepPlan& plan, std::uint32_t source) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        recordThread();
        require(samePlan(plan, applied), "device received a plan different from its applied configuration");
        if (source == plan.sourcePorts.front()) {
            ++acquisitionFrame;
        }
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
        std::vector<double> iq;
        iq.reserve(16U * plan.frequencyRaw.size());
        for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
            for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
                for (std::size_t component = 0U; component < 4U; ++component) {
                    iq.push_back(sampleValue(plan, acquisitionFrame, source, point, receiver, component));
                }
            }
        }
        if (acquisitions == shortAcquireAt) {
            iq.pop_back();
        }
        return iq;
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
    std::size_t successfulQueued = 0U;
    for (const auto& ticket : tickets) {
        successfulQueued += await(ticket).ok ? 1U : 0U;
    }
    require(successfulQueued == 64U, "Close abandoned an accepted queued trigger");
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

void testConfigurationSwitchAtFrameBoundary()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    const auto baseline = f.core->readView().frame;
    f.device->arm();
    ReleaseGateOnExit releaseOnExit{f.device};
    const auto inFlight = f.core->triggerOnce(started.runId, started.configRevision);
    f.device->waitEntered();
    const auto replacement = makePlan(1U);
    const auto configured = f.core->configure(replacement);
    require(configured.wait_for(milliseconds(80)) == std::future_status::timeout,
        "configuration crossed an unfinished multi-source frame");
    require(f.core->readView().frame == baseline, "blocked frame was published before all sources completed");
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        require(samePlan(f.device->applied, makePlan()), "configuration mutated hardware during an active frame");
    }
    f.device->releaseGate();
    require(await(inFlight).ok, "old configuration frame failed during replacement");
    const auto changed = await(configured);
    require(changed.ok && changed.configRevision == started.configRevision + 1U,
        "successful replacement was not committed exactly once");
    const auto oldFrame = f.core->readView().frame;
    require(oldFrame->frameSeq == 2U && oldFrame->configRevision == started.configRevision &&
                samePlan(oldFrame->plan, makePlan()),
        "old frame mixed replacement metadata or frequencies");
    requireFrameContent(*oldFrame);
    require(!await(f.core->triggerOnce(started.runId, started.configRevision)).ok,
        "old-revision trigger was accepted after successful replacement");
    require(await(f.core->triggerOnce(started.runId, changed.configRevision)).ok,
        "new configuration frame failed");
    const auto newFrame = f.core->readView().frame;
    require(newFrame->runId == started.runId && newFrame->frameSeq == 3U &&
                newFrame->configRevision == changed.configRevision && samePlan(newFrame->plan, replacement),
        "next frame did not fully adopt the new configuration");
    requireFrameContent(*newFrame);
}

void testRetainedFrameDeepImmutability()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "retained baseline failed");
    const auto retained = f.core->readView().frame;
    const MeasurementFrame snapshot = *retained;
    requireFrameContent(*retained);
    for (std::uint32_t variant = 1U; variant <= 5U; ++variant) {
        const auto plan = makePlan(variant);
        const auto configured = await(f.core->configure(plan));
        require(configured.ok && await(f.core->triggerOnce(started.runId, configured.configRevision)).ok,
            "replacement scan failed in immutability test");
        const auto fresh = f.core->readView().frame;
        require(fresh != retained && samePlan(fresh->plan, plan), "replacement reused the retained frame");
        requireFrameContent(*fresh);
        requireUnchanged(*retained, snapshot);
    }
    require(f.core->close().ok, "close failed with a retained frame");
    requireUnchanged(*retained, snapshot);
}

void testShortIqDoesNotPublish()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "short-IQ baseline failed");
    const auto retained = f.core->readView().frame;
    const MeasurementFrame snapshot = *retained;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        f.device->shortAcquireAt = f.device->acquisitions + 2U;
    }
    const auto failed = await(f.core->triggerOnce(started.runId, started.configRevision));
    const auto view = f.core->readView();
    require(!failed.ok && failed.message.find("incomplete IQ") != std::string::npos &&
                view.state == CoreState::Faulted && view.frame == retained && failed.frameSeq == snapshot.frameSeq,
        "short second-source IQ published a partial frame or escaped fault handling");
    requireUnchanged(*retained, snapshot);
}

void testConcurrentSubmittersAndReaders()
{
    Fixture f;
    const auto started = await(f.core->start(RunMode::Manual));
    require(await(f.core->triggerOnce(started.runId, started.configRevision)).ok, "stress baseline failed");
    const auto retained = f.core->readView().frame;
    const MeasurementFrame baseline = *retained;
    std::promise<void> startWorkers;
    const auto startSignal = startWorkers.get_future().share();
    std::atomic<bool> finished(false);
    std::atomic<std::size_t> readersReady(0U), checkedViews(0U), completedTickets(0U), successfulTriggers(0U),
        rejectedTriggers(0U);
    std::mutex plansMutex;
    std::vector<std::pair<std::uint64_t, SweepPlan>> committedPlans{{started.configRevision, makePlan()}};
    std::vector<std::future<void>> submitters;
    std::vector<std::future<void>> readers;
    struct WorkerStartOnExit {
        std::promise<void>& signal;
        std::atomic<bool>& finished;
        bool pending = true;
        ~WorkerStartOnExit()
        {
            if (pending) {
                finished.store(true);
                signal.set_value();
            }
        }
    } releaseWorkersOnExit{startWorkers, finished};
    for (std::size_t submitter = 0U; submitter < 3U; ++submitter) {
        submitters.push_back(std::async(std::launch::async, [&, submitter] {
            startSignal.wait();
            const auto readyDeadline = steady_clock::now() + seconds(3);
            while (readersReady.load() != 2U) {
                require(steady_clock::now() < readyDeadline, "stress readers never became ready");
                std::this_thread::yield();
            }
            for (std::size_t index = 0U; index < 48U; ++index) {
                if (index % 12U == submitter) {
                    const auto plan = makePlan(static_cast<std::uint32_t>(1U + 48U * submitter + index));
                    const auto changed = await(f.core->configure(plan));
                    ++completedTickets;
                    require(changed.ok, "concurrent valid configuration failed");
                    std::lock_guard<std::mutex> lock(plansMutex);
                    committedPlans.emplace_back(changed.configRevision, plan);
                } else {
                    const auto view = f.core->readView();
                    const auto triggered = await(f.core->triggerOnce(view.runId, view.configRevision));
                    ++completedTickets;
                    if (triggered.ok) {
                        ++successfulTriggers;
                    } else {
                        ++rejectedTriggers;
                        require(triggered.message == "trigger requires the current manual run and configuration",
                            "concurrent trigger failed for a reason other than a superseded revision");
                    }
                }
            }
        }));
    }
    for (std::size_t reader = 0U; reader < 2U; ++reader) {
        readers.push_back(std::async(std::launch::async, [&] {
            startSignal.wait();
            std::vector<std::pair<FramePtr, MeasurementFrame>> snapshots{{retained, baseline}};
            ++readersReady;
            std::size_t checks = 0U;
            std::uint64_t previousFrame = 0U, previousRevision = 0U;
            const auto deadline = steady_clock::now() + seconds(5);
            while (!finished.load() || checks < 256U) {
                require(steady_clock::now() < deadline, "concurrent read stress exceeded its bound");
                const auto view = f.core->readView();
                require(view.state == CoreState::Running && view.error.empty() && view.frame &&
                            view.runId == started.runId && view.frame->runId == view.runId &&
                            view.frame->configRevision <= view.configRevision &&
                            view.frame->frameSeq >= previousFrame && view.configRevision >= previousRevision,
                    "readView exposed inconsistent or regressing published metadata");
                requireFrameContent(*view.frame);
                requireUnchanged(*retained, baseline);
                if (snapshots.back().first != view.frame) {
                    snapshots.emplace_back(view.frame, *view.frame);
                }
                previousFrame = view.frame->frameSeq;
                previousRevision = view.configRevision;
                ++checks;
                ++checkedViews;
                std::this_thread::yield();
            }
            std::lock_guard<std::mutex> lock(plansMutex);
            for (const auto& snapshot : snapshots) {
                requireUnchanged(*snapshot.first, snapshot.second);
                bool matchedRevision = false;
                for (const auto& committed : committedPlans) {
                    if (snapshot.first->configRevision == committed.first) {
                        matchedRevision = samePlan(snapshot.first->plan, committed.second);
                        break;
                    }
                }
                require(matchedRevision, "frame configuration did not match its committed revision");
            }
        }));
    }
    startWorkers.set_value();
    releaseWorkersOnExit.pending = false;
    // async captures exceptions; drain every worker before rethrowing so no joinable thread escapes.
    std::exception_ptr workerError;
    const auto collect = [&workerError](std::vector<std::future<void>>& workers) {
        for (auto& worker : workers) {
            try {
                worker.get();
            } catch (...) {
                if (!workerError) {
                    workerError = std::current_exception();
                }
            }
        }
    };
    collect(submitters);
    finished.store(true);
    collect(readers);
    if (workerError) {
        std::rethrow_exception(workerError);
    }
    require(completedTickets.load() == 144U && committedPlans.size() == 13U && checkedViews.load() >= 512U,
        "stress did not complete every bounded submission and reader check");
    require(successfulTriggers.load() > 0U && successfulTriggers.load() + rejectedTriggers.load() == 132U &&
                f.core->readView().frame->frameSeq == 1U + successfulTriggers.load(),
        "stress lost or duplicated a successfully triggered frame");
    require(await(f.core->stop(started.runId)).ok && f.core->close().ok, "stress did not stop and close cleanly");
    requireUnchanged(*retained, baseline);
    {
        std::lock_guard<std::mutex> lock(f.device->mutex);
        require(!f.device->wrongThread && f.device->shutdownCalled &&
                    f.device->deviceThread != std::this_thread::get_id(),
            "concurrent clients caused device I/O to escape its single worker");
    }
    std::cout << "stress: 144 tickets, " << successfulTriggers.load() << " frames, " << rejectedTriggers.load()
              << " superseded triggers, " << checkedViews.load() << " consistent views\n";
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
        testConfigurationSwitchAtFrameBoundary();
        testRetainedFrameDeepImmutability();
        testShortIqDoesNotPublish();
        testConcurrentSubmittersAndReaders();
        std::cout << "core concurrency: 11 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
