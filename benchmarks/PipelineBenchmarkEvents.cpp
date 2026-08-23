#include "PipelineBenchmarkEvents.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace vna::benchmark::detail {
namespace {

    constexpr auto controlTimeout = std::chrono::seconds(15);

}  // namespace

[[noreturn]] void throwVnaError(const char *operation, const VnaError &error)
{
    throw std::runtime_error(std::string{operation} + ": " + error.description);
}

DiscoverySink::DiscoverySink() = default;

DiscoverySink::~DiscoverySink() = default;

void DiscoverySink::onDeviceFound(DiscoveryId, const DeviceDescriptor &descriptor)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if ((descriptor.vendor == "VnaPro") && (descriptor.model == "Open-circuit four-port simulator")) {
        device_ = descriptor;
    }
}

LifecycleSink::LifecycleSink() = default;

LifecycleSink::~LifecycleSink() = default;

void DiscoverySink::onDiscoveryCompleted(DiscoveryId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completed_ = true;
    changed_.notify_all();
}

void DiscoverySink::onDiscoveryFailed(DiscoveryId, const VnaError &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = error;
    completed_ = true;
    changed_.notify_all();
}

DeviceDescriptor DiscoverySink::waitForSimulator()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!changed_.wait_for(lock, controlTimeout, [this] { return completed_; })) {
        throw std::runtime_error("simulator discovery timed out");
    }
    if (error_.code != VnaErrorCode::None) {
        throwVnaError("simulator discovery failed", error_);
    }
    if (!device_) {
        throw std::runtime_error("simulator was not discovered");
    }
    return *device_;
}

void LifecycleSink::onStateChanged(const StateChangedEvent &)
{
}

void LifecycleSink::onConnectCompleted(const ConnectCompletedEvent &event)
{
    recordCompletion(event.commandId);
}

void LifecycleSink::onPrepareCompleted(const PrepareCompletedEvent &)
{
}

void LifecycleSink::onStartCompleted(const StartCompletedEvent &)
{
}

void LifecycleSink::onAbortCompleted(const AbortCompletedEvent &)
{
}

void LifecycleSink::onDisconnectCompleted(const DisconnectCompletedEvent &event)
{
    recordCompletion(event.commandId);
}

void LifecycleSink::onWaveBlock(VnaWaveBlock)
{
}

void LifecycleSink::onSweepCompleted(const SweepCompletion &)
{
}

void LifecycleSink::onCommandFailed(const CommandFailedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    failures_.push_back(event);
    changed_.notify_all();
}

void LifecycleSink::waitForCommand(CommandId commandId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const auto finished = [this, commandId] {
        return contains(completions_, commandId) || findFailure(commandId) != failures_.end();
    };
    if (!changed_.wait_for(lock, controlTimeout, finished)) {
        throw std::runtime_error("hardware lifecycle command timed out");
    }
    const auto failure = findFailure(commandId);
    if (failure != failures_.end()) {
        throwVnaError("hardware lifecycle command failed", failure->error);
    }
}

bool LifecycleSink::contains(const std::vector<CommandId> &commands, CommandId commandId)
{
    return std::find(commands.begin(), commands.end(), commandId) != commands.end();
}

std::vector<CommandFailedEvent>::const_iterator LifecycleSink::findFailure(CommandId commandId) const
{
    return std::find_if(failures_.begin(), failures_.end(), [commandId](const auto &failure) {
        return failure.commandId == commandId;
    });
}

void LifecycleSink::recordCompletion(CommandId commandId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completions_.push_back(commandId);
    changed_.notify_all();
}

AcquisitionSink::AcquisitionSink()
{
    frames_.reserve(64);
    completions_.reserve(64);
}

AcquisitionSink::~AcquisitionSink() = default;

void AcquisitionSink::onPrepared(CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome)
{
    PreparedNotice notice;
    notice.commandId = commandId;
    if (outcome.hasValue()) {
        notice.prepared = outcome.takeValue();
    } else {
        notice.error = outcome.error();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    prepared_ = std::move(notice);
    changed_.notify_all();
}

void AcquisitionSink::onSweepReady(std::shared_ptr<const RawSweepFrame> frame)
{
    const auto arrivedAt = BenchmarkClock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.push_back(FrameNotice{frame ? frame->runId() : 0, arrivedAt});
    changed_.notify_all();
}

void AcquisitionSink::onRunCompleted(const AcquisitionRunCompletion &completion)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completions_.push_back(completion);
    changed_.notify_all();
}

void AcquisitionSink::waitForPrepared(CommandId commandId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = changed_.wait_for(
        lock, controlTimeout, [this, commandId] { return prepared_ && (prepared_->commandId == commandId); });
    if (!ready) {
        throw std::runtime_error("acquisition prepare timed out");
    }
    if (!prepared_->prepared) {
        throwVnaError("acquisition prepare failed", prepared_->error);
    }
}

bool AcquisitionSink::waitForFrameOrCompletion(AcquisitionRunId runId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const auto terminal = [this, runId] { return findFrame(runId) != frames_.end() || hasCompletion(runId); };
    return changed_.wait_for(lock, controlTimeout, terminal) && (findFrame(runId) != frames_.end());
}

BenchmarkClock::time_point AcquisitionSink::frameArrival(AcquisitionRunId runId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = findFrame(runId);
    if (found == frames_.end()) {
        throw std::runtime_error("acquisition frame timestamp is missing");
    }
    return found->arrivedAt;
}

bool AcquisitionSink::waitForCompletion(AcquisitionRunId runId, std::chrono::seconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, runId] { return hasCompletion(runId); });
}

AcquisitionRunCompletion AcquisitionSink::completion(AcquisitionRunId runId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = findCompletion(runId);
    if (found == completions_.end()) {
        throw std::runtime_error("acquisition completion is missing");
    }
    return *found;
}

std::vector<FrameNotice>::const_iterator AcquisitionSink::findFrame(AcquisitionRunId runId) const
{
    return std::find_if(frames_.begin(), frames_.end(), [runId](const auto &notice) { return notice.runId == runId; });
}

std::vector<AcquisitionRunCompletion>::const_iterator AcquisitionSink::findCompletion(AcquisitionRunId runId) const
{
    return std::find_if(
        completions_.begin(), completions_.end(), [runId](const auto &notice) { return notice.runId == runId; });
}

bool AcquisitionSink::hasCompletion(AcquisitionRunId runId) const
{
    return findCompletion(runId) != completions_.end();
}

}  // namespace vna::benchmark::detail
