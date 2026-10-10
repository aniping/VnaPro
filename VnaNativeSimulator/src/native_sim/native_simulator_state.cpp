#include "native_simulator_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "signal_model.h"

namespace vna_native {
namespace {

const char kSimulatorIdn[] = "VnaPro,NativeSimulator,4PORT,1.0";

bool ReadNoiseConfiguration(bool* enabled, std::uint64_t* seed)
{
    *enabled = false;
    *seed = 1U;
    const char* mode = std::getenv("VNAPRO_SIM_MODE");
    if (mode == NULL || mode[0] == '\0' || std::strcmp(mode, "golden") == 0) {
        // 默认路径不读取种子；残留的噪声种子配置不会改变黄金向量。
        return true;
    }
    if (std::strcmp(mode, "noise") != 0) {
        return false;
    }
    *enabled = true;
    const char* text = std::getenv("VNAPRO_SIM_SEED");
    if (text == NULL || text[0] == '\0') {
        return true;
    }
    std::uint64_t parsed = 0U;
    for (const char* digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        const std::uint64_t value = static_cast<std::uint64_t>(*digit - '0');
        if (parsed > ((std::numeric_limits<std::uint64_t>::max)() - value) / 10U) {
            return false;
        }
        parsed = parsed * 10U + value;
    }
    *seed = parsed;
    return true;
}

std::uint32_t ValidateIqShape(std::uint32_t pointCount, std::size_t* valueCount)
{
    if (valueCount == NULL || pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    // 先用 64 位计算元素数，再校验字节数，避免在 32 位进程中分配大小溢出。
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
      fastSweepMode_(0U),
      noiseEnabled_(false),
      noiseSeed_(1U),
      successfulSweepIndex_(0U)
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
        // 先构造空对象再交换；初始化可重复调用，旧帧和未读取结果都会被清除。
        std::array<FrameSlot, kNativePortCount> emptyFrames;
        PendingSweep emptyPending;
        std::lock_guard<std::mutex> lock(mutex_);

        bool noiseEnabled = false;
        std::uint64_t noiseSeed = 1U;
        if (!ReadNoiseConfiguration(&noiseEnabled, &noiseSeed)) {
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
        successfulSweepIndex_ = 0U;
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t NativeSimulatorState::GetSweepData(std::uint32_t pointCount, double* iqData)
{
    // 指针与形状错误优先于状态错误，尚未初始化也能返回具体的入参错误。
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

        // 先完成复制再清空 pending：点数不匹配等失败不会丢掉结果，成功读取只能发生一次。
        // 无容量参数，因此只能校验声明点数；实际缓冲区大小由调用方保证。
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
    // 点数形状先校验；锁内依次检查初始化、端口、RF、配置及帧/pending 状态。
    // 这些检查的先后决定同时存在多项错误时返回哪个错误码。
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

        // 生成期间始终持锁：帧不会被另一线程替换，RF 关闭或重初始化也不能穿插提交。
        PendingSweep candidate;
        candidate.pointCount = pointCount;
        candidate.sourcePort = sourcePort;
        const std::uint32_t generateStatus = noiseEnabled_ ? SignalModel::GenerateNoisy(frameSlot.frame, noiseSeed_,
                                                                 successfulSweepIndex_, &candidate.iqData)
                                                           : SignalModel::Generate(frameSlot.frame, &candidate.iqData);
        if (generateStatus != DRV_SIM_OK) {
            return generateStatus;
        }
        if (candidate.iqData.size() != expectedValueCount) {
            return DRV_SIM_INTERNAL_ERROR;
        }
        candidate.valid = true;

        // 分配、计算和测试异常均发生在提交前；Swap 不抛异常，失败时共享状态保持原样。
        ThrowIfRequested(FailurePoint::kBeforePendingCommit);
        pendingSweep_.Swap(candidate);
        if (noiseEnabled_) {
            ++successfulSweepIndex_;
        }
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
            // RF 关闭视为取消尚未读取的扫描；再次打开后可复用现有配置和下载帧。
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

        // 只给出可预测的空输出；不能将清零结果解释为真实功率检查已通过。
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
    // 形状检查不读取数组内容；初始化检查通过后才在锁内解码、复制调用方数据。
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

        // 先解码至临时帧，任何字段错误或分配异常都不会破坏已下载的旧帧。
        NormalFrameSnapshot decodedFrame;
        const std::uint32_t decodeStatus =
            NormalFrameDecoder::Decode(pointCount, paramData, paramDataLen, tableData, tableDataLen, &decodedFrame);
        if (decodeStatus != DRV_SIM_OK) {
            return decodeStatus;
        }

        FrameSlot candidate;
        candidate.valid = true;
        candidate.frame.Swap(decodedFrame);
        // Decode 已验证源端口范围；此处通过不抛异常的 Swap 一次性提交该端口的新帧。
        ThrowIfRequested(FailurePoint::kBeforeFrameCommit);
        downloadedFrames_[static_cast<std::size_t>(candidate.frame.sourcePort)].Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

NativeSimulatorSnapshot NativeSimulatorState::SnapshotForTest() const
{
    // 在同一临界区复制摘要，测试不会看到不同调用时刻拼接出的状态。
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
        // 先完成深拷贝再替换 output，内存分配失败时保留测试调用方原有输出。
        NormalFrameSnapshot candidate = frameSlot.frame;
        output->Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

void NativeSimulatorState::SetFailureInjectorForTest(std::shared_ptr<FailureInjector> failureInjector)
{
    // 传入空 shared_ptr 可禁用注入；本状态持有的旧引用在退出临界区后释放。
    std::lock_guard<std::mutex> lock(mutex_);
    failureInjector_.swap(failureInjector);
}

void NativeSimulatorState::ThrowIfRequested(FailurePoint point)
{
    // 仅由持锁的提交路径调用；异常由外层入口转换为 INTERNAL_ERROR。
    if (failureInjector_) {
        failureInjector_->ThrowIfRequested(point);
    }
}

}  // namespace vna_native
