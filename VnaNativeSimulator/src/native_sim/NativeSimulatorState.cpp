#include "NativeSimulatorState.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#include "SignalModel.h"

namespace vna_native {
namespace {

const char kSimulatorIdn[] = "VnaPro,NativeSimulator,4PORT,1.0";

std::uint32_t ValidateIqShape(std::uint32_t pointCount, std::size_t* valueCount)
{
    if (valueCount == NULL || pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    const std::uint64_t count = static_cast<std::uint64_t>(pointCount) * static_cast<std::uint64_t>(kIqValuesPerPoint);
    const std::uint64_t maxSize = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
    if (count > maxSize || count > maxSize / sizeof(double)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    *valueCount = static_cast<std::size_t>(count);
    return DRV_SIM_OK;
}

bool IndexBytesFit(std::uint32_t indexCapacity)
{
    const std::uint64_t bytes = static_cast<std::uint64_t>(indexCapacity) * sizeof(std::uint16_t);
    return bytes <= static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
}

}  // namespace

NativeSimulatorSnapshot::NativeSimulatorSnapshot()
    : initialized(false),
      rfEnabled(false),
      rxConfigured(false),
      rxPortBitmap(0U),
      fastModeConfigured(false),
      fastSweepMode(0U),
      pendingSweep(false),
      pendingPointCount(0U),
      pendingSourcePort(0U)
{
    frameValid.fill(false);
    framePointCount.fill(0U);
}

NativeSimulatorState::FrameSlot::FrameSlot() : valid(false) {}

void NativeSimulatorState::FrameSlot::Swap(FrameSlot& other) noexcept
{
    using std::swap;
    swap(valid, other.valid);
    frame.Swap(other.frame);
}

NativeSimulatorState::PendingSweep::PendingSweep() : valid(false), pointCount(0U), sourcePort(0U) {}

void NativeSimulatorState::PendingSweep::Swap(PendingSweep& other) noexcept
{
    using std::swap;
    swap(valid, other.valid);
    swap(pointCount, other.pointCount);
    swap(sourcePort, other.sourcePort);
    iqData.swap(other.iqData);
}

NativeSimulatorState::NativeSimulatorState()
    : initialized_(false),
      rfEnabled_(false),
      rxConfigured_(false),
      rxPortBitmap_(0U),
      fastModeConfigured_(false),
      fastSweepMode_(0U)
{
}

NativeSimulatorState& NativeSimulatorState::Instance()
{
    static NativeSimulatorState state;
    return state;
}

std::uint32_t NativeSimulatorState::HwInit()
{
    try {
        std::array<FrameSlot, kNativePortCount> emptyFrames;
        PendingSweep emptyPending;
        std::lock_guard<std::mutex> lock(mutex_);

        downloadedFrames_.swap(emptyFrames);
        pendingSweep_.Swap(emptyPending);
        initialized_ = true;
        rfEnabled_ = false;
        rxConfigured_ = false;
        rxPortBitmap_ = 0U;
        fastModeConfigured_ = false;
        fastSweepMode_ = 0U;
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::GetSweepData(std::uint32_t pointCount, double* iqData)
{
    if (iqData == NULL) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::size_t expectedValueCount = 0U;
    const std::uint32_t shapeStatus = ValidateIqShape(pointCount, &expectedValueCount);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (!pendingSweep_.valid || pendingSweep_.pointCount != pointCount) {
            return DRV_SIM_INVALID_STATE;
        }
        if (pendingSweep_.iqData.size() != expectedValueCount) {
            return DRV_SIM_INTERNAL_ERROR;
        }

        std::copy(pendingSweep_.iqData.begin(), pendingSweep_.iqData.end(), iqData);
        PendingSweep consumed;
        pendingSweep_.Swap(consumed);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort)
{
    std::size_t expectedValueCount = 0U;
    const std::uint32_t shapeStatus = ValidateIqShape(pointCount, &expectedValueCount);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (sourcePort >= kNativePortCount) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
        if (!rfEnabled_) {
            return DRV_SIM_RF_DISABLED;
        }
        if (!rxConfigured_ || !fastModeConfigured_) {
            return DRV_SIM_INVALID_STATE;
        }

        const FrameSlot& frameSlot = downloadedFrames_[static_cast<std::size_t>(sourcePort)];
        if (!frameSlot.valid || frameSlot.frame.points.size() != pointCount || pendingSweep_.valid) {
            return DRV_SIM_INVALID_STATE;
        }

        PendingSweep candidate;
        candidate.pointCount = pointCount;
        candidate.sourcePort = sourcePort;
        const std::uint32_t generateStatus = SignalModel::Generate(frameSlot.frame, &candidate.iqData);
        if (generateStatus != DRV_SIM_OK) {
            return generateStatus;
        }
        if (candidate.iqData.size() != expectedValueCount) {
            return DRV_SIM_INTERNAL_ERROR;
        }
        candidate.valid = true;

        ThrowIfRequested(FailurePoint::kBeforePendingCommit);
        pendingSweep_.Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::ActivateRxLink(std::uint32_t rxPortBitmap)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (rxPortBitmap == 0U || rxPortBitmap > 0x0fU) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        rxPortBitmap_ = rxPortBitmap;
        rxConfigured_ = true;
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::SetRfPowerOn(std::uint32_t enabled)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (enabled > 1U) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        rfEnabled_ = enabled == 1U;
        if (!rfEnabled_) {
            PendingSweep cleared;
            pendingSweep_.Swap(cleared);
        }
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::SetFastSweepMode(std::uint32_t mode)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (mode != 0U) {
            return DRV_SIM_NOT_SUPPORTED;
        }

        fastSweepMode_ = 0U;
        fastModeConfigured_ = true;
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::GetIDN(char* buffer, int capacity)
{
    if (buffer == NULL || capacity <= 0) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }

        const std::size_t idnLength = sizeof(kSimulatorIdn) - 1U;
        const std::size_t outputCapacity = static_cast<std::size_t>(capacity);
        if (outputCapacity > idnLength) {
            std::memcpy(buffer, kSimulatorIdn, idnLength + 1U);
            return DRV_SIM_OK;
        }

        const std::size_t copyLength = outputCapacity - 1U;
        if (copyLength > 0U) {
            std::memcpy(buffer, kSimulatorIdn, copyLength);
        }
        buffer[copyLength] = '\0';
        return DRV_SIM_BUFFER_TOO_SMALL;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::CheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap, std::uint16_t* overFreqIndex, std::uint32_t indexCapacity)
{
    if (checkBitmap == NULL || (indexCapacity > 0U && overFreqIndex == NULL)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (!IndexBytesFit(indexCapacity)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (rfPortBitmap == 0U || rfPortBitmap > 0x0fU) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        *checkBitmap = 0U;
        if (indexCapacity > 0U) {
            std::fill(
                overFreqIndex, overFreqIndex + static_cast<std::size_t>(indexCapacity), static_cast<std::uint16_t>(0U));
        }
        return DRV_SIM_NOT_SUPPORTED;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::DataFrameDownload(std::uint32_t pointCount, const std::uint32_t* paramData,
    std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen)
{
    const std::uint32_t shapeStatus =
        NormalFrameDecoder::ValidateInputShape(pointCount, paramData, paramDataLen, tableData, tableDataLen);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }

        NormalFrameSnapshot decodedFrame;
        const std::uint32_t decodeStatus =
            NormalFrameDecoder::Decode(pointCount, paramData, paramDataLen, tableData, tableDataLen, &decodedFrame);
        if (decodeStatus != DRV_SIM_OK) {
            return decodeStatus;
        }

        FrameSlot candidate;
        candidate.valid = true;
        candidate.frame.Swap(decodedFrame);
        ThrowIfRequested(FailurePoint::kBeforeFrameCommit);
        downloadedFrames_[static_cast<std::size_t>(candidate.frame.sourcePort)].Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

NativeSimulatorSnapshot NativeSimulatorState::SnapshotForTest() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    NativeSimulatorSnapshot snapshot;
    snapshot.initialized = initialized_;
    snapshot.rfEnabled = rfEnabled_;
    snapshot.rxConfigured = rxConfigured_;
    snapshot.rxPortBitmap = rxPortBitmap_;
    snapshot.fastModeConfigured = fastModeConfigured_;
    snapshot.fastSweepMode = fastSweepMode_;
    for (std::size_t port = 0U; port < downloadedFrames_.size(); ++port) {
        snapshot.frameValid[port] = downloadedFrames_[port].valid;
        snapshot.framePointCount[port] = static_cast<std::uint32_t>(downloadedFrames_[port].frame.points.size());
    }
    snapshot.pendingSweep = pendingSweep_.valid;
    snapshot.pendingPointCount = pendingSweep_.pointCount;
    snapshot.pendingSourcePort = pendingSweep_.sourcePort;
    return snapshot;
}

std::uint32_t NativeSimulatorState::CopyFrameForTest(std::uint32_t sourcePort, NormalFrameSnapshot* output) const
{
    if (output == NULL || sourcePort >= kNativePortCount) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        const FrameSlot& frameSlot = downloadedFrames_[static_cast<std::size_t>(sourcePort)];
        if (!frameSlot.valid) {
            return DRV_SIM_INVALID_STATE;
        }
        NormalFrameSnapshot candidate = frameSlot.frame;
        output->Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

void NativeSimulatorState::SetFailureInjectorForTest(std::shared_ptr<FailureInjector> failureInjector)
{
    std::lock_guard<std::mutex> lock(mutex_);
    failureInjector_.swap(failureInjector);
}

void NativeSimulatorState::ThrowIfRequested(FailurePoint point)
{
    if (failureInjector_) {
        failureInjector_->ThrowIfRequested(point);
    }
}

}  // namespace vna_native
