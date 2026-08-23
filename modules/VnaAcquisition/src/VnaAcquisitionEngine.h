#ifndef VNA_ACQUISITION_VNA_ACQUISITION_ENGINE_H
#define VNA_ACQUISITION_VNA_ACQUISITION_ENGINE_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "VnaAcquisition.h"
#include "PreparedMapping.h"

namespace vna::detail {

enum class AcquisitionAbortKind : std::uint8_t { None, User, Internal };

struct AcquisitionAbortLatch {
    AcquisitionAbortKind kind{AcquisitionAbortKind::None};
    CommandId commandId{0};
    bool commandFinished{false};
    bool commandSucceeded{false};
    VnaError commandError;
};

struct AcquisitionActiveSweep {
    std::uint64_t sweepIndex{0};
    CommandId startCommandId{0};
    std::optional<SweepId> sweepId;
    std::unique_ptr<RawFrameAssembly> storage;
    std::optional<SweepCompletion> preStartCompletion;
    std::optional<SweepCompletion> completion;
    AcquisitionAbortLatch abort;
};

struct AcquisitionActiveRun {
    AcquisitionRunId runId{0};
    AcquisitionRunRequest request;
    std::uint64_t completedSweepCount{0};
    bool stopPending{false};
    bool abortPending{false};
    bool abortAfterSweep{false};
    bool closing{false};
    VnaError startTerminalFallback;
    std::shared_ptr<const PreparedAcquisition> prepared;
    std::unique_ptr<AcquisitionActiveSweep> sweep;
    std::optional<VnaError> primaryFailure;
};

struct AcquisitionPendingPrepare {
    CommandId commandId{0};
    AcquisitionState previousState{AcquisitionState::Unprepared};
    std::shared_ptr<const PreparedAcquisition> previousPrepared;
};

class VnaAcquisitionEngine final : public IVnaAcquisitionEngine, private IVnaHardwareEventSink {
public:
    VnaAcquisitionEngine(
        std::shared_ptr<IVnaHardware> hardware, IVnaAcquisitionSink &sink, AcquisitionLimits limits,
        bool supportsAbort);
    ~VnaAcquisitionEngine() override;
    VnaAcquisitionEngine(const VnaAcquisitionEngine &) = delete;
    VnaAcquisitionEngine &operator=(const VnaAcquisitionEngine &) = delete;
    VnaAcquisitionEngine(VnaAcquisitionEngine &&) = delete;
    VnaAcquisitionEngine &operator=(VnaAcquisitionEngine &&) = delete;

    void subscribe();
    Result<CommandId> prepareAsync(VnaSweepPlan plan) override;
    Result<AcquisitionRunId> start(AcquisitionRunRequest request) override;
    VnaError requestStop(AcquisitionRunId runId) override;
    VnaError requestAbort(AcquisitionRunId runId) override;
    AcquisitionState state() const override;

private:
    class TerminalCallbackTicket final {
    public:
        TerminalCallbackTicket() noexcept;
        ~TerminalCallbackTicket() noexcept;
        TerminalCallbackTicket(const TerminalCallbackTicket &) = delete;
        TerminalCallbackTicket &operator=(const TerminalCallbackTicket &) = delete;
        TerminalCallbackTicket(TerminalCallbackTicket &&other) noexcept;
        TerminalCallbackTicket &operator=(TerminalCallbackTicket &&) = delete;

    private:
        friend class VnaAcquisitionEngine;

        VnaAcquisitionEngine *engine_{nullptr};
    };

    void onStateChanged(const StateChangedEvent &event) override;
    void onConnectCompleted(const ConnectCompletedEvent &event) override;
    void onPrepareCompleted(const PrepareCompletedEvent &event) override;
    void onStartCompleted(const StartCompletedEvent &event) override;
    void onAbortCompleted(const AbortCompletedEvent &event) override;
    void onDisconnectCompleted(const DisconnectCompletedEvent &event) override;
    void onWaveBlock(VnaWaveBlock block) override;
    void onSweepCompleted(const SweepCompletion &event) override;
    void onCommandFailed(const CommandFailedEvent &event) override;

    Result<AcquisitionRunId> startLocked(AcquisitionRunRequest request);
    VnaError requestAbortLocked(AcquisitionActiveRun &run);
    VnaError submitUserAbortLocked(AcquisitionActiveRun &run);
    void submitDeferredAbortLocked(AcquisitionActiveRun &run);
    bool submitInternalAbortLocked(AcquisitionActiveRun &run);
    bool submitNextSweepLocked(AcquisitionActiveRun &run, VnaError &error);
    Result<std::unique_ptr<AcquisitionActiveSweep>> makeSweepLocked(
        std::uint64_t sweepIndex, const std::shared_ptr<const PreparedAcquisition> &prepared) const;

    void handlePrepareCompleted(const PrepareCompletedEvent &event);
    void handleStartCompleted(const StartCompletedEvent &event);
    void handleAbortCompleted(const AbortCompletedEvent &event);
    void handleWaveBlock(VnaWaveBlock block);
    void handleSweepCompleted(const SweepCompletion &event);
    void handleCommandFailed(const CommandFailedEvent &event);
    void handlePrepareFailed(const CommandFailedEvent &event);
    void handleStartFailed(const CommandFailedEvent &event);
    void handleAbortFailed(const CommandFailedEvent &event);
    void settleFailedPrepareLocked();
    VnaError startCommandFailureLocked(AcquisitionActiveRun &run, const VnaError &reportedError) noexcept;

    void processKnownSweepCompletion(AcquisitionRunId runId);
    void processAbortTerminals(AcquisitionRunId runId);
    AcquisitionRunCompletion finishAbortTerminalsLocked(AcquisitionActiveRun &run);
    void publishCompletedSweep(AcquisitionRunId runId);
    void afterSweepPublished(AcquisitionRunId runId, bool finiteLast);
    bool completedFrameIsValid(const AcquisitionActiveRun &run) const;
    void recordLocalFailureLocked(AcquisitionActiveRun &run, VnaError error);
    void recordProtocolFaultLocked(VnaError error);
    bool activeSweepDrainedLocked() const;
    void handleProtocolFault(const char *description) noexcept;
    void handleUnexpectedException(const char *context) noexcept;

    AcquisitionRunCompletion finishRunLocked(AcquisitionRunTermination termination, std::optional<VnaError> error);
    AcquisitionRunCompletion finishFailedRunLocked(VnaError error);
    AcquisitionState stableStateLocked() const;
    void clearPreparedForFaultLocked();
    void waitForTerminalCallbackDeliveryLocked(std::unique_lock<std::mutex> &lock) const;
    void armTerminalCallbackLocked(TerminalCallbackTicket &ticket) noexcept;
    void beginTerminalCallbackDelivery();
    void releaseTerminalCallbackDelivery() noexcept;

    void notifyPrepared(
        TerminalCallbackTicket ticket, CommandId commandId,
        Result<std::shared_ptr<const PreparedAcquisition>> outcome) noexcept;
    void notifySweepReady(std::shared_ptr<const RawSweepFrame> frame) noexcept;
    void notifyRunCompleted(TerminalCallbackTicket ticket, const AcquisitionRunCompletion &completion) noexcept;
    void logCallbackException(const char *callbackName) const noexcept;

    static VnaError makeError(VnaErrorCode code, const char *description);
    static VnaError invalidStateError(const char *description);

    std::shared_ptr<IVnaHardware> hardware_;
    Subscription subscription_;
    IVnaAcquisitionSink *sink_{nullptr};
    AcquisitionLimits limits_;
    bool supportsAbort_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable terminalCallbackCondition_;
    std::thread::id terminalCallbackThread_;
    std::size_t pendingTerminalCallbackCount_{0};
    AcquisitionState state_{AcquisitionState::Unprepared};
    DeviceState hardwareState_{DeviceState::Idle};
    std::shared_ptr<const PreparedAcquisition> prepared_;
    std::optional<AcquisitionPendingPrepare> pendingPrepare_;
    std::unique_ptr<AcquisitionActiveRun> activeRun_;
    AcquisitionRunId nextRunId_{1};
};

}  // namespace vna::detail

#endif
