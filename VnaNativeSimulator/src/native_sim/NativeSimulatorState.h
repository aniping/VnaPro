#ifndef VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
#define VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "NormalFrameDecoder.h"

namespace vna_native {

enum class FailurePoint { kBeforeFrameCommit, kBeforePendingCommit };

class VNA_NATIVE_HIDDEN FailureInjector {
public:
    virtual ~FailureInjector() {}
    virtual void ThrowIfRequested(FailurePoint point) = 0;
};

struct VNA_NATIVE_HIDDEN NativeSimulatorSnapshot {
    bool initialized;
    bool rfEnabled;
    bool rxConfigured;
    std::uint32_t rxPortBitmap;
    bool fastModeConfigured;
    std::uint32_t fastSweepMode;
    std::array<bool, kNativePortCount> frameValid;
    std::array<std::uint32_t, kNativePortCount> framePointCount;
    bool pendingSweep;
    std::uint32_t pendingPointCount;
    std::uint32_t pendingSourcePort;

    NativeSimulatorSnapshot();
};

class VNA_NATIVE_HIDDEN NativeSimulatorState {
public:
    NativeSimulatorState();

    static NativeSimulatorState& Instance();

    std::uint32_t HwInit();
    std::uint32_t GetSweepData(std::uint32_t pointCount, double* iqData);
    std::uint32_t StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
    std::uint32_t ActivateRxLink(std::uint32_t rxPortBitmap);
    std::uint32_t SetRfPowerOn(std::uint32_t enabled);
    std::uint32_t SetFastSweepMode(std::uint32_t mode);
    std::uint32_t GetIDN(char* buffer, int capacity);
    std::uint32_t CheckSweepPortPower(std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap,
        std::uint16_t* overFreqIndex, std::uint32_t indexCapacity);
    std::uint32_t DataFrameDownload(std::uint32_t pointCount, const std::uint32_t* paramData,
        std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen);

    // 以下接口只供直接链接内部对象的单元测试使用，不属于 C ABI。
    NativeSimulatorSnapshot SnapshotForTest() const;
    std::uint32_t CopyFrameForTest(std::uint32_t sourcePort, NormalFrameSnapshot* output) const;
    void SetFailureInjectorForTest(std::shared_ptr<FailureInjector> failureInjector);

private:
    struct FrameSlot {
        bool valid;
        NormalFrameSnapshot frame;

        FrameSlot();
        void Swap(FrameSlot& other) noexcept;
    };

    struct PendingSweep {
        bool valid;
        std::uint32_t pointCount;
        std::uint32_t sourcePort;
        std::vector<double> iqData;

        PendingSweep();
        void Swap(PendingSweep& other) noexcept;
    };

    void ThrowIfRequested(FailurePoint point);

    mutable std::mutex mutex_;
    bool initialized_;
    bool rfEnabled_;
    bool rxConfigured_;
    std::uint32_t rxPortBitmap_;
    bool fastModeConfigured_;
    std::uint32_t fastSweepMode_;
    std::array<FrameSlot, kNativePortCount> downloadedFrames_;
    PendingSweep pendingSweep_;
    std::shared_ptr<FailureInjector> failureInjector_;
};

}  // namespace vna_native

#endif  // VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
