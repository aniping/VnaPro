/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_simulator_state.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

#include "signal_model.h"

namespace VnaNative {
namespace {

constexpr std::uint32_t ALL_PORTS_BITMAP = (1U << NATIVE_PORT_COUNT) - 1U;
constexpr std::uint64_t DECIMAL_RADIX = 10U;

const char g_simulatorIdn[] = "VnaPro,NativeSimulator,4PORT,1.0";

bool ReadNoiseAmplitude(double &amplitude) noexcept
{
    amplitude = 0.002;
    const char *text = std::getenv("VNAPRO_SIM_NOISE_AMPLITUDE");
    if (text == nullptr || text[0] == '\0') {
        return true;
    }
    try {
        std::istringstream input(text);
        input.imbue(std::locale::classic());
        double parsed = 0.0;
        input >> std::noskipws >> parsed;
        if (input.fail() || input.peek() != std::char_traits<char>::eof() || !std::isfinite(parsed) || parsed < 0.0 ||
            parsed > 1.0) {
            return false;
        }
        if (parsed == 0.0) {
            // 某些运行库会把过小的非零数静默转为零，必须区分真实零与下溢。
            const std::string value(text);
            const std::size_t nonZeroDigit = value.find_first_of("123456789");
            const std::size_t exponent = value.find_first_of("eE");
            if (nonZeroDigit != std::string::npos && (exponent == std::string::npos || nonZeroDigit < exponent)) {
                return false;
            }
        }
        amplitude = parsed;
        return true;
    } catch (...) {
        // 环境数字解析异常不能穿过驱动的 noexcept 边界。
        return false;
    }
}

bool ReadNoiseConfiguration(bool &enabled, std::uint64_t &seed, double &amplitude) noexcept
{
    enabled = false;
    seed = 1U;
    amplitude = 0.002;
    const char *mode = std::getenv("VNAPRO_SIM_MODE");
    if (mode == nullptr || mode[0] == '\0' || std::strcmp(mode, "golden") == 0) {
        return true;
    }
    if (std::strcmp(mode, "noise") != 0) {
        return false;
    }
    enabled = true;
    if (!ReadNoiseAmplitude(amplitude)) {
        return false;
    }
    const char *text = std::getenv("VNAPRO_SIM_SEED");
    if (text == nullptr || text[0] == '\0') {
        return true;
    }
    std::uint64_t parsed = 0U;
    for (const char *digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        const std::uint64_t value = static_cast<std::uint64_t>(*digit - '0');
        if (parsed > ((std::numeric_limits<std::uint64_t>::max)() - value) / DECIMAL_RADIX) {
            return false;
        }
        parsed = (parsed * DECIMAL_RADIX) + value;
    }
    seed = parsed;
    return true;
}

std::uint32_t ValidateIqShape(std::uint32_t pointCount, std::size_t &valueCount) noexcept
{
    if (pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    const std::uint64_t count =
        static_cast<std::uint64_t>(pointCount) * static_cast<std::uint64_t>(IQ_VALUES_PER_POINT);
    const std::uint64_t maxSize = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
    if (count > maxSize || count > maxSize / sizeof(double)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    valueCount = static_cast<std::size_t>(count);
    return DRV_SIM_OK;
}

bool IndexBytesFit(std::uint32_t indexCapacity) noexcept
{
    const std::uint64_t bytes = static_cast<std::uint64_t>(indexCapacity) * sizeof(std::uint16_t);
    return bytes <= static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
}

}  // 匿名命名空间

NativeSimulatorState &NativeSimulatorState::Instance()
{
    static NativeSimulatorState state;
    return state;
}

std::uint32_t NativeSimulatorState::HwInit() noexcept
{
    try {
        std::array<NativeFrameSlot, NATIVE_PORT_COUNT> emptyFrames;
        NativePendingSweep emptyPending;
        std::lock_guard<std::mutex> lock(mutex_);

        bool noiseEnabled = false;
        std::uint64_t noiseSeed = 1U;
        double noiseAmplitude = 0.002;
        if (!ReadNoiseConfiguration(noiseEnabled, noiseSeed, noiseAmplitude)) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        downloadedFrames_.swap(emptyFrames);
        pendingSweep_.Swap(emptyPending);
        initialized_ = true;
        rfEnabled_ = false;
        rxConfigured_ = false;
        rxPortBitmap_ = 0U;
        fastModeConfigured_ = false;
        fastSweepMode_ = 0U;
        noiseEnabled_ = noiseEnabled;
        noiseSeed_ = noiseSeed;
        noiseAmplitude_ = noiseAmplitude;
        successfulSweepIndex_ = 0U;
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::GetSweepData(std::uint32_t pointCount, double *iqData) noexcept
{
    if (iqData == nullptr) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::size_t expectedValueCount = 0U;
    const std::uint32_t shapeStatus = ValidateIqShape(pointCount, expectedValueCount);
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
        NativePendingSweep consumed;
        pendingSweep_.Swap(consumed);
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::ValidateSweepState(
    std::uint32_t pointCount, std::uint32_t sourcePort) const noexcept
{
    if (!initialized_) {
        return DRV_SIM_NOT_INITIALIZED;
    }
    if (sourcePort >= NATIVE_PORT_COUNT) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (!rfEnabled_) {
        return DRV_SIM_RF_DISABLED;
    }
    if (!rxConfigured_ || !fastModeConfigured_) {
        return DRV_SIM_INVALID_STATE;
    }
    const NativeFrameSlot &frameSlot = downloadedFrames_[sourcePort];
    if (!frameSlot.valid || frameSlot.frame.points.size() != pointCount || pendingSweep_.valid) {
        return DRV_SIM_INVALID_STATE;
    }
    return DRV_SIM_OK;
}

std::uint32_t NativeSimulatorState::GeneratePending(
    std::uint32_t pointCount, std::uint32_t sourcePort, std::size_t expectedValueCount)
{
    // 调用者在生成、故障注入和唯一一次提交期间始终持有互斥锁。
    const NativeFrameSlot &frameSlot = downloadedFrames_[sourcePort];
    NativePendingSweep candidate;
    candidate.pointCount = pointCount;
    candidate.sourcePort = sourcePort;
    const std::uint32_t generateStatus = noiseEnabled_ ? SignalModel::GenerateNoisy(frameSlot.frame, noiseSeed_,
                                                             successfulSweepIndex_, candidate.iqData, noiseAmplitude_)
                                                       : SignalModel::Generate(frameSlot.frame, candidate.iqData);
    if (generateStatus != DRV_SIM_OK) {
        return generateStatus;
    }
    if (candidate.iqData.size() != expectedValueCount) {
        return DRV_SIM_INTERNAL_ERROR;
    }
    candidate.valid = true;
    ThrowIfRequested(FailurePoint::BEFORE_PENDING_COMMIT);
    pendingSweep_.Swap(candidate);
    if (noiseEnabled_) {
        ++successfulSweepIndex_;
    }
    return DRV_SIM_OK;
}

std::uint32_t NativeSimulatorState::StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort) noexcept
{
    std::size_t expectedValueCount = 0U;
    const std::uint32_t shapeStatus = ValidateIqShape(pointCount, expectedValueCount);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint32_t stateStatus = ValidateSweepState(pointCount, sourcePort);
        if (stateStatus != DRV_SIM_OK) {
            return stateStatus;
        }
        return GeneratePending(pointCount, sourcePort, expectedValueCount);
    } catch (...) {
        // 生成失败或注入的故障不会发布不完整的待取结果。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::ActivateRxLink(std::uint32_t rxPortBitmap) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        if (rxPortBitmap == 0U || rxPortBitmap > ALL_PORTS_BITMAP) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        rxPortBitmap_ = rxPortBitmap;
        rxConfigured_ = true;
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::SetRfPowerOn(std::uint32_t enabled) noexcept
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
            NativePendingSweep cleared;
            pendingSweep_.Swap(cleared);
        }
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::SetFastSweepMode(std::uint32_t mode) noexcept
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
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::GetIDN(char *buffer, int capacity) noexcept
{
    if (buffer == nullptr || capacity <= 0) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }

        const std::size_t idnLength = sizeof(g_simulatorIdn) - 1U;
        const std::size_t outputCapacity = static_cast<std::size_t>(capacity);
        if (outputCapacity > idnLength) {
            std::memcpy(buffer, g_simulatorIdn, idnLength + 1U);
            return DRV_SIM_OK;
        }

        const std::size_t copyLength = outputCapacity - 1U;
        if (copyLength > 0U) {
            std::memcpy(buffer, g_simulatorIdn, copyLength);
        }
        buffer[copyLength] = '\0';
        return DRV_SIM_BUFFER_TOO_SMALL;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::CheckSweepPortPower(std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap,
    std::uint16_t *overFreqIndex, std::uint32_t indexCapacity) noexcept
{
    if (checkBitmap == nullptr || (indexCapacity > 0U && overFreqIndex == nullptr)) {
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
        if (rfPortBitmap == 0U || rfPortBitmap > ALL_PORTS_BITMAP) {
            return DRV_SIM_INVALID_ARGUMENT;
        }

        *checkBitmap = 0U;
        if (indexCapacity > 0U) {
            std::fill(
                overFreqIndex, overFreqIndex + static_cast<std::size_t>(indexCapacity), static_cast<std::uint16_t>(0U));
        }
        return DRV_SIM_NOT_SUPPORTED;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::DataFrameDownload(std::uint32_t pointCount, const std::uint32_t *paramData,
    std::uint32_t paramDataLen, const double *tableData, std::uint32_t tableDataLen) noexcept
{
    const NormalFrameInput input{pointCount, paramData, paramDataLen, tableData, tableDataLen};
    const std::uint32_t shapeStatus = NormalFrameDecoder::ValidateInputShape(input);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }

        NormalFrameSnapshot decodedFrame;
        const std::uint32_t decodeStatus = NormalFrameDecoder::Decode(input, decodedFrame);
        if (decodeStatus != DRV_SIM_OK) {
            return decodeStatus;
        }

        NativeFrameSlot candidate;
        candidate.valid = true;
        candidate.frame.Swap(decodedFrame);

        ThrowIfRequested(FailurePoint::BEFORE_FRAME_COMMIT);
        downloadedFrames_[static_cast<std::size_t>(candidate.frame.sourcePort)].Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
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

std::uint32_t NativeSimulatorState::CopyFrameForTest(
    std::uint32_t sourcePort, NormalFrameSnapshot &output) const noexcept
{
    if (sourcePort >= NATIVE_PORT_COUNT) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        const NativeFrameSlot &frameSlot = downloadedFrames_[static_cast<std::size_t>(sourcePort)];
        if (!frameSlot.valid) {
            return DRV_SIM_INVALID_STATE;
        }

        NormalFrameSnapshot candidate = frameSlot.frame;
        output.Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        // 保持驱动状态码语义：C++ 异常不得传播到采集操作之外。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

void NativeSimulatorState::SetFailureInjectorForTest(FailureInjector *failureInjector)
{
    std::lock_guard<std::mutex> lock(mutex_);
    failureInjector_ = failureInjector;
}

void NativeSimulatorState::ThrowIfRequested(FailurePoint point) const
{
    if (failureInjector_) {
        failureInjector_->ThrowIfRequested(point);
    }
}

}  // 命名空间 VnaNative
