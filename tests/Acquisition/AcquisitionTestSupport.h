#ifndef VNA_TESTS_ACQUISITION_ACQUISITION_TEST_SUPPORT_H
#define VNA_TESTS_ACQUISITION_ACQUISITION_TEST_SUPPORT_H

#include <complex>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "VnaAcquisition.h"

namespace vna::test {

struct WaveSample {
    std::complex<double> value;
    std::uint32_t quality{0};
};

struct WaveBlockSpec {
    SweepId sweepId{0};
    std::uint64_t sequence{0};
    LayoutId layoutId{0};
    std::uint32_t firstPointIndex{0};
    std::vector<std::vector<WaveSample>> waves;
};

struct PreparedNotice {
    CommandId commandId{0};
    std::shared_ptr<const PreparedAcquisition> prepared;
    VnaError error;
};

class CallbackBarrier final {
public:
    void arriveAndWait();
    bool waitUntilArrived();
    void release();

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool arrived_{false};
    bool released_{false};
};

class RecordingAcquisitionSink final : public IVnaAcquisitionSink {
public:
    void onPrepared(CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome) override;
    void onSweepReady(std::shared_ptr<const RawSweepFrame> frame) override;
    void onRunCompleted(const AcquisitionRunCompletion &completion) override;

    bool waitForPrepared(std::size_t count);
    bool waitForFrames(std::size_t count);
    bool waitForCompletions(std::size_t count);
    std::size_t preparedCount() const;
    std::size_t frameCount() const;
    std::size_t completionCount() const;
    PreparedNotice prepared(std::size_t index) const;
    std::shared_ptr<const RawSweepFrame> frame(std::size_t index) const;
    AcquisitionRunCompletion completion(std::size_t index) const;
    void throwOnNextPrepared();
    void throwOnNextFrame();
    void throwOnNextCompletion();
    void setPreparedEntryHook(std::function<void()> hook);
    void setFrameEntryHook(std::function<void()> hook);
    void setCompletionEntryHook(std::function<void()> hook);

private:
    enum class NoticeKind : std::uint8_t { Prepared, Frame, Completion };
    bool waitForCount(NoticeKind kind, std::size_t count);

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<PreparedNotice> preparedNotices_;
    std::vector<std::shared_ptr<const RawSweepFrame>> frames_;
    std::vector<AcquisitionRunCompletion> completions_;
    bool throwPrepared_{false};
    bool throwFrame_{false};
    bool throwCompletion_{false};
    std::function<void()> preparedEntryHook_;
    std::function<void()> frameEntryHook_;
    std::function<void()> completionEntryHook_;
};

class FakeVnaHardware final : public IVnaHardware {
public:
    explicit FakeVnaHardware(bool supportsAbort);
    ~FakeVnaHardware() override;
    FakeVnaHardware(const FakeVnaHardware &) = delete;
    FakeVnaHardware &operator=(const FakeVnaHardware &) = delete;
    FakeVnaHardware(FakeVnaHardware &&) = delete;
    FakeVnaHardware &operator=(FakeVnaHardware &&) = delete;

    DeviceDescriptor descriptor() const override;
    VnaCapabilities capabilities() const override;
    DeviceState state() const override;
    ValidationReport validate(const VnaSweepPlan &plan) const override;
    Result<CommandId> connectAsync() override;
    Result<CommandId> prepareAsync(VnaSweepPlan plan) override;
    Result<CommandId> startAsync(ConfigurationId configurationId) override;
    Result<CommandId> abortAsync(SweepId sweepId) override;
    Result<CommandId> disconnectAsync() override;
    Subscription subscribe(IVnaHardwareEventSink &sink) override;

    // 命令与查询可跨测试线程调用；事件注入由单个测试控制线程串行执行。
    void rejectNextPrepare(VnaError error);
    void rejectNextStart(VnaError error);
    void rejectNextAbort(VnaError error);
    void publishState(DeviceState current);
    void publishPrepareSucceeded(CommandId commandId, PreparedSweep prepared);
    void publishPrepareSnapshot(CommandId commandId, std::shared_ptr<const PreparedSweep> prepared);
    void publishStartSucceeded(CommandId commandId, SweepId sweepId);
    void publishAbortSucceeded(CommandId commandId, SweepId sweepId);
    void publishCommandFailed(CommandId commandId, VnaError error);
    bool publishWaveBlock(const WaveBlockSpec &specification);
    void publishSweepFinished(SweepCompletion completion, DeviceState stateAfter);

    bool waitForStartCalls(std::size_t count);
    bool waitForAbortCalls(std::size_t count);
    std::size_t validationCallCount() const;
    std::size_t prepareCallCount() const;
    std::size_t startCallCount() const;
    std::size_t abortCallCount() const;
    CommandId lastStartCommand() const;
    CommandId lastAbortCommand() const;
    ConfigurationId lastStartConfiguration() const;

private:
    Result<CommandId> commandResultLocked(std::optional<VnaError> &rejection);

    mutable std::mutex mutex_;
    std::condition_variable commandChanged_;
    std::shared_ptr<detail::VnaEventDispatcher> dispatcher_;
    std::shared_ptr<detail::VnaBufferPool> pool_;
    VnaCapabilities capabilities_;
    DeviceState state_{DeviceState::Idle};
    CommandId nextCommandId_{1};
    mutable std::size_t validationCalls_{0};
    std::size_t prepareCalls_{0};
    std::size_t startCalls_{0};
    std::size_t abortCalls_{0};
    CommandId lastStartCommand_{0};
    CommandId lastAbortCommand_{0};
    ConfigurationId lastStartConfiguration_{0};
    std::optional<VnaError> nextPrepareRejection_;
    std::optional<VnaError> nextStartRejection_;
    std::optional<VnaError> nextAbortRejection_;
};

VnaSweepPlan makeSparsePlan();
PreparedSweep makeSparsePrepared(const VnaSweepPlan &plan, ConfigurationId configurationId);
AcquisitionLimits exactLimitsFor(const VnaSweepPlan &plan);
AcquisitionLimits spaciousLimits();
WaveSample expectedSample(ExcitationId excitationId, VnaWaveKey wave, PointId pointId, SweepId sweepId);
std::vector<WaveBlockSpec> makeSparseBlocks(const VnaSweepPlan &plan, SweepId sweepId);
bool completeSparseSweep(FakeVnaHardware &hardware, CommandId commandId, SweepId sweepId, const VnaSweepPlan &plan);
VnaError testError(VnaErrorCode code, const char *description);

class AcquisitionTest : public ::testing::Test {
protected:
    void SetUp() override;
    void resetEngine(bool supportsAbort, AcquisitionLimits limits);
    CommandId prepareSuccessfully(ConfigurationId configurationId);
    bool completeCurrentSweep(SweepId sweepId);

    RecordingAcquisitionSink sink_;
    std::shared_ptr<FakeVnaHardware> hardware_;
    std::unique_ptr<IVnaAcquisitionEngine> engine_;
    VnaSweepPlan plan_;
    PreparedSweep prepared_;
};

}  // namespace vna::test

#endif
