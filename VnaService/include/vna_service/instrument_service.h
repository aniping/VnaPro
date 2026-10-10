/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H
#define VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "vna_core/acquisition_core.h"
#include "vna_service/data_result.h"
#include "vna_service/instrument_error.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/instrument_snapshot.h"
#include "vna_service/measurement.h"
#include "vna_service/operation_result.h"
#include "vna_service/service_task.h"

namespace vna_service {

// Thread confinement: all calls after startup belong to the service executor.
// The referenced core must outlive this service. Protocol state survives clients.
class InstrumentService final {
public:
    // Binds to a core that must outlive this service; subsequent calls share one service executor.
    explicit InstrumentService(vna_core::AcquisitionCore &core);
    // Destroy only after accepted operations and protocol users have finished.
    ~InstrumentService();
    // Executor confinement and core binding cannot be transferred to another instance.
    InstrumentService(InstrumentService &&) = delete;
    InstrumentService &operator=(InstrumentService &&) = delete;
    InstrumentService(const InstrumentService &) = delete;
    InstrumentService &operator=(const InstrumentService &) = delete;

    // Startup-only blocking call; accepts the verified NativeSimulator identity and reports failures in error.
    bool BootstrapMockProfile(std::string &error);
    // Advances one pending transition without blocking for device I/O.
    void Poll();
    // Reports whether the finite service FIFO still contains accepted operations.
    bool HasPending() const noexcept;
    // Copies futures for currently accepted work; a ready future means the operation has completed.
    std::vector<Operation> PendingOperations() const;
    // Acquires an unowned instrument for a nonempty owner token; returns false on contention.
    bool TryAcquireControl(const std::string &owner = "SCPI");
    // Stops accepting writes from the matching owner while already accepted work drains.
    void BeginReleaseControl(const std::string &owner) noexcept;
    // Clears the matching owner only when all accepted operations have finished.
    void ReleaseControl(const std::string &owner = "SCPI") noexcept;
    // Returns the active owner token, or an empty string when control is available.
    std::string ControlOwner() const;
    // Reports whether the active owner is draining its accepted operations.
    bool ControlReleasing() const noexcept;
    // Returns settings, core publication, and control state from this executor turn.
    InstrumentSnapshot Snapshot() const;

    // Returns committed settings; the reference is valid only while this service remains alive.
    const InstrumentSettings &Settings() const noexcept;
    // Reports the verified simulator identity, or an explicitly unverified backend identity.
    std::string Identity() const;
    // Queues a rounded 1 Hz..1 GHz endpoint; an endpoint crossing preserves the old span.
    Operation SetStartHz(double value, std::uint64_t expectedRevision = 0U);
    // Queues a rounded 1 Hz..1 GHz endpoint; coupled endpoints must both stay in range.
    Operation SetStopHz(double value, std::uint64_t expectedRevision = 0U);
    // Queues 2..10001 sweep points; expectedRevision zero skips the SCPI revision check.
    Operation SetPoints(unsigned value, std::uint64_t expectedRevision = 0U);
    // Queues a positive bandwidth, rounded upward to the supported simulator values.
    Operation SetIfBandwidthHz(double value, std::uint64_t expectedRevision = 0U);
    // Queues one atomic candidate; the owner and expected revision are rechecked before device mutation.
    Operation ApplyConfiguration(ConfigPatch patch, std::uint64_t expectedRevision, const std::string &owner);
    // Queues a unique 1..128 byte receiver name with physical source/receiver ports 1..4.
    Operation DefineMeasurement(
        Measurement measurement, std::uint64_t expectedRevision = 0U, const std::string &owner = "SCPI");
    // Queues removal of an existing exact-case name and updates the selection if necessary.
    Operation DeleteMeasurement(
        const std::string &name, std::uint64_t expectedRevision = 0U, const std::string &owner = "SCPI");
    // Queues a finite source transition; owner must hold active instrument control.
    Operation SetTriggerSource(TriggerSource source, const std::string &owner = "SCPI");
    // Queues HOLD, SINGLE, or CONTINUOUS and resolves only after its transition barriers.
    Operation SetSweepMode(SweepMode mode, const std::string &owner = "SCPI");
    // Queues one internally triggered frame followed by a Stop barrier and a return to HOLD.
    Operation RunSingle(const std::string &owner);
    // Queues continuous acquisition using the internal trigger source.
    Operation RunContinuous(const std::string &owner);
    // Queues a manual INIT for an armed current run; overlapping finite measurements are rejected.
    Operation Trigger(const std::string &owner = "SCPI");
    // Immediately selects an existing exact-case name; requires SCPI control and no pending configuration.
    bool SelectMeasurement(const std::string &name);
    // Updates the single-channel scope flag; requires SCPI control and no pending configuration.
    bool SetCurrentTriggerScope(bool current);
    // Returns the committed Hz axis; faults and uncommitted revisions produce an error.
    DataResult StimulusAxis() const;
    // Returns interleaved real/imaginary values only from the complete current run/revision frame.
    DataResult MeasurementData() const;

    // Appends an IEEE 488.2/SCPI error and updates the appropriate event-register bit.
    void RecordError(int code, const std::string &message);
    // Removes the oldest queued error; an empty FIFO returns code zero and No error.
    InstrumentError PopError();
    // Returns the bounded error FIFO size without consuming any entries.
    unsigned ErrorCount() const noexcept;
    // Clears error/event state and advances the generation used by pending status watches.
    void ClearStatus() noexcept;
    // Returns the generation advanced by ClearStatus.
    unsigned StatusGeneration() const noexcept;
    // Stores the low eight ESE bits.
    void SetEventEnable(unsigned value) noexcept;
    // Returns the current ESE mask.
    unsigned EventEnable() const noexcept;
    // Stores supported SRE bits while excluding the request-summary bit.
    void SetServiceRequestEnable(unsigned value) noexcept;
    // Returns the current SRE mask.
    unsigned ServiceRequestEnable() const noexcept;
    // Returns and clears ESR atomically within the service executor.
    unsigned ReadEventRegister() noexcept;
    // Sets the operation-complete ESR bit after the relevant barrier has finished.
    void MarkOperationComplete() noexcept;
    // Combines error FIFO, transport output, ESR/ESE, and SRE into an IEEE 488.2 status byte.
    unsigned StatusByte(bool outputAvailable) const noexcept;

private:
    // IEEE 488.2 sets this ESR bit when the instrument powers on.
    static constexpr unsigned POWER_ON_EVENT = 128U;

    void Error(int code, const std::string &text);
    Operation Rejected(int code, const std::string &message, const std::string &requester = "SCPI");
    Operation Enqueue(detail::ServiceTask task, const std::string &requester = "SCPI");
    Operation Configure(std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected,
        const std::string &requester = "SCPI");
    bool HasMeasurement() const noexcept;
    bool HasConfiguration() const noexcept;
    void Finish(OperationResult value);
    bool Consume(detail::ServiceTask &task);
    void CommitMode(detail::ServiceTask &task);
    void PollTask();
    void PollConfiguration(detail::ServiceTask &task);
    void PollTrigger(detail::ServiceTask &task);
    bool BeginMode(detail::ServiceTask &task);
    void PollMode(detail::ServiceTask &task);
    void PollStartedMode(detail::ServiceTask &task);

    vna_core::AcquisitionCore &core_;
    InstrumentSettings settings_;
    std::deque<detail::ServiceTask> tasks_;
    std::deque<InstrumentError> errors_;
    bool mockReady_ = false;
    std::string owner_;
    std::uint64_t controlGeneration_ = 0;
    bool releasing_ = false;
    unsigned ese_ = 0;
    unsigned sre_ = 0;
    unsigned esr_ = POWER_ON_EVENT;
    unsigned generation_ = 0;
    std::string observedFault_;
};

}  // namespace vna_service
#endif
