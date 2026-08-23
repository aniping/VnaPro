#ifndef VNA_BENCHMARKS_PIPELINE_BENCHMARK_EVENTS_H
#define VNA_BENCHMARKS_PIPELINE_BENCHMARK_EVENTS_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "BenchmarkModel.h"
#include "VnaHardwareManager.h"

namespace vna::benchmark::detail {

using BenchmarkClock = std::chrono::steady_clock;

[[noreturn]] void throwVnaError(const char *operation, const VnaError &error);

class DiscoverySink final : public IVnaDiscoverySink {
public:
    DiscoverySink();
    ~DiscoverySink() override;
    DiscoverySink(const DiscoverySink &) = delete;
    DiscoverySink &operator=(const DiscoverySink &) = delete;
    DiscoverySink(DiscoverySink &&) = delete;
    DiscoverySink &operator=(DiscoverySink &&) = delete;

    void onDeviceFound(DiscoveryId discoveryId, const DeviceDescriptor &descriptor) override;
    void onDiscoveryCompleted(DiscoveryId discoveryId) override;
    void onDiscoveryFailed(DiscoveryId discoveryId, const VnaError &error) override;
    DeviceDescriptor waitForSimulator();

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<DeviceDescriptor> device_;
    VnaError error_;
    bool completed_{false};
};

class LifecycleSink final : public IVnaHardwareEventSink {
public:
    LifecycleSink();
    ~LifecycleSink() override;
    LifecycleSink(const LifecycleSink &) = delete;
    LifecycleSink &operator=(const LifecycleSink &) = delete;
    LifecycleSink(LifecycleSink &&) = delete;
    LifecycleSink &operator=(LifecycleSink &&) = delete;

    void onStateChanged(const StateChangedEvent &event) override;
    void onConnectCompleted(const ConnectCompletedEvent &event) override;
    void onPrepareCompleted(const PrepareCompletedEvent &event) override;
    void onStartCompleted(const StartCompletedEvent &event) override;
    void onAbortCompleted(const AbortCompletedEvent &event) override;
    void onDisconnectCompleted(const DisconnectCompletedEvent &event) override;
    void onWaveBlock(VnaWaveBlock block) override;
    void onSweepCompleted(const SweepCompletion &event) override;
    void onCommandFailed(const CommandFailedEvent &event) override;
    void waitForCommand(CommandId commandId);

private:
    static bool contains(const std::vector<CommandId> &commands, CommandId commandId);
    std::vector<CommandFailedEvent>::const_iterator findFailure(CommandId commandId) const;
    void recordCompletion(CommandId commandId);

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<CommandId> completions_;
    std::vector<CommandFailedEvent> failures_;
};

struct PreparedNotice {
    CommandId commandId{0};
    std::shared_ptr<const PreparedAcquisition> prepared;
    VnaError error;
};

struct FrameNotice {
    AcquisitionRunId runId{0};
    BenchmarkClock::time_point arrivedAt;
};

class AcquisitionSink final : public IVnaAcquisitionSink {
public:
    AcquisitionSink();
    ~AcquisitionSink() override;
    AcquisitionSink(const AcquisitionSink &) = delete;
    AcquisitionSink &operator=(const AcquisitionSink &) = delete;
    AcquisitionSink(AcquisitionSink &&) = delete;
    AcquisitionSink &operator=(AcquisitionSink &&) = delete;

    void onPrepared(CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome) override;
    void onSweepReady(std::shared_ptr<const RawSweepFrame> frame) override;
    void onRunCompleted(const AcquisitionRunCompletion &completion) override;
    void waitForPrepared(CommandId commandId);
    bool waitForFrameOrCompletion(AcquisitionRunId runId);
    BenchmarkClock::time_point frameArrival(AcquisitionRunId runId) const;
    bool waitForCompletion(AcquisitionRunId runId, std::chrono::seconds timeout);
    AcquisitionRunCompletion completion(AcquisitionRunId runId) const;

private:
    std::vector<FrameNotice>::const_iterator findFrame(AcquisitionRunId runId) const;
    std::vector<AcquisitionRunCompletion>::const_iterator findCompletion(AcquisitionRunId runId) const;
    bool hasCompletion(AcquisitionRunId runId) const;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<PreparedNotice> prepared_;
    std::vector<FrameNotice> frames_;
    std::vector<AcquisitionRunCompletion> completions_;
};

}  // namespace vna::benchmark::detail

#endif
