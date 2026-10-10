/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
#define VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "failure_injector.h"
#include "native_frame_slot.h"
#include "native_pending_sweep.h"
#include "normal_frame_decoder.h"

namespace VnaNative {

// A coherent state summary, copied under the same mutex used by all driver operations.
struct VNA_NATIVE_HIDDEN NativeSimulatorSnapshot final {
    bool initialized = false;
    bool rfEnabled = false;
    bool rxConfigured = false;
    std::uint32_t rxPortBitmap = 0U;
    bool fastModeConfigured = false;
    std::uint32_t fastSweepMode = 0U;
    std::array<bool, NATIVE_PORT_COUNT> frameValid{};
    std::array<std::uint32_t, NATIVE_PORT_COUNT> framePointCount{};
    bool pendingSweep = false;
    std::uint32_t pendingPointCount = 0U;
    std::uint32_t pendingSourcePort = 0U;
};

// One mutex serializes configuration, copied frames, synchronous IQ generation, and consumption.
// Callers still coordinate multi-call workflows; there is no background acquisition thread.
class VNA_NATIVE_HIDDEN NativeSimulatorState final {
public:
    // Starts uninitialized; tests may construct independent instances instead of using the singleton.
    NativeSimulatorState() noexcept = default;
    NativeSimulatorState(const NativeSimulatorState &) = delete;
    NativeSimulatorState &operator=(const NativeSimulatorState &) = delete;
    NativeSimulatorState(NativeSimulatorState &&) = delete;
    NativeSimulatorState &operator=(NativeSimulatorState &&) = delete;

    // Returns the process-wide state used by all nine C ABI functions.
    static NativeSimulatorState &Instance();

    // Latches environment mode/seed, resets sequence, frames, configuration and pending, and disables RF.
    // Invalid mode/seed preserves the previous state and returns INVALID_ARGUMENT.
    std::uint32_t HwInit() noexcept;
    // The writable buffer must hold pointCount * IQ_VALUES_PER_POINT doubles; no ABI capacity is available.
    // A matching pending result is consumed once; failures leave pending and caller memory unchanged.
    std::uint32_t GetSweepData(std::uint32_t pointCount, double *iqData) noexcept;
    // Requires RF, RX, normal-mode configuration, and a matching frame for sourcePort in 0..3.
    // Rejects a second Start while pending is unread; generation and commit occur under the state mutex.
    std::uint32_t StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort) noexcept;
    // Accepts a nonzero bitmap of the four receiver ports; IQ still contains all four ports.
    std::uint32_t ActivateRxLink(std::uint32_t rxPortBitmap) noexcept;
    // Accepts 0/1. Disabling RF cancels pending while retaining downloaded frames and configuration.
    std::uint32_t SetRfPowerOn(std::uint32_t enabled) noexcept;
    // Only normal mode 0 is supported; this call is required even for the default value.
    std::uint32_t SetFastSweepMode(std::uint32_t mode) noexcept;
    // Capacity includes NUL. Truncation writes a terminated prefix and returns BUFFER_TOO_SMALL.
    std::uint32_t GetIDN(char *buffer, int capacity) noexcept;
    // Power checking is not modeled. A valid call zeros outputs and returns NOT_SUPPORTED.
    // An indexCapacity of zero allows a null index buffer; other output storage must be writable.
    std::uint32_t CheckSweepPortPower(std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap,
        std::uint16_t *overFreqIndex, std::uint32_t indexCapacity) noexcept;
    // Copies the borrowed frame and atomically replaces its source slot; pending is unaffected.
    // Counts are exact element counts; callers guarantee actual capacity and lifetime through the call.
    std::uint32_t DataFrameDownload(std::uint32_t pointCount, const std::uint32_t *paramData,
        std::uint32_t paramDataLen, const double *tableData, std::uint32_t tableDataLen) noexcept;

    // Returns a coherent summary for unit tests; locking failure propagates to the test runner.
    NativeSimulatorSnapshot SnapshotForTest() const;
    // Copies a stored frame for tests; invalid ports/empty slots leave output unchanged.
    std::uint32_t CopyFrameForTest(std::uint32_t sourcePort, NormalFrameSnapshot &output) const noexcept;
    // Borrows an injector until replaced or cleared with nullptr; it must outlive every invoking call.
    // The caller must not destroy it concurrently. No ownership transfer or reference counting occurs.
    void SetFailureInjectorForTest(FailureInjector *failureInjector);

private:
    std::uint32_t ValidateSweepState(std::uint32_t pointCount, std::uint32_t sourcePort) const noexcept;
    std::uint32_t GeneratePending(std::uint32_t pointCount, std::uint32_t sourcePort, std::size_t expectedValueCount);
    void ThrowIfRequested(FailurePoint point) const;

    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool rfEnabled_ = false;
    bool rxConfigured_ = false;
    std::uint32_t rxPortBitmap_ = 0U;
    bool fastModeConfigured_ = false;
    std::uint32_t fastSweepMode_ = 0U;
    bool noiseEnabled_ = false;
    std::uint64_t noiseSeed_ = 1U;
    std::uint64_t successfulSweepIndex_ = 0U;
    std::array<NativeFrameSlot, NATIVE_PORT_COUNT> downloadedFrames_;
    NativePendingSweep pendingSweep_;
    FailureInjector *failureInjector_ = nullptr;
};

}  // namespace VnaNative
#endif  // VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
