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

// 在所有驱动操作共用的互斥锁下复制的一致状态摘要。
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

// 使用同一互斥锁串行执行配置、帧复制、同步 IQ 生成及结果读取。
// 多次调用组成的工作流仍由调用者协调；没有后台采集线程。
class VNA_NATIVE_HIDDEN NativeSimulatorState final {
public:
    // 初始处于未初始化状态；测试可构造独立实例，无需使用单例。
    NativeSimulatorState() noexcept = default;
    NativeSimulatorState(const NativeSimulatorState &) = delete;
    NativeSimulatorState &operator=(const NativeSimulatorState &) = delete;
    NativeSimulatorState(NativeSimulatorState &&) = delete;
    NativeSimulatorState &operator=(NativeSimulatorState &&) = delete;

    // 返回九个 C ABI 函数共用的进程级状态。
    static NativeSimulatorState &Instance();

    // 保存环境指定的模式和种子，重置序列、帧、配置及待取结果，并关闭射频。
    // 模式或种子无效时，保留原状态并返回 INVALID_ARGUMENT。
    std::uint32_t HwInit() noexcept;
    // 可写缓冲区必须容纳 pointCount * IQ_VALUES_PER_POINT 个 double；ABI 不提供容量参数。
    // 匹配的待取结果仅能取走一次；失败时保持待取结果和调用者内存不变。
    std::uint32_t GetSweepData(std::uint32_t pointCount, double *iqData) noexcept;
    // 要求已配置射频、接收链路和普通模式，且存在与 sourcePort 匹配的帧；端口范围为 0～3。
    // 存在未取结果时拒绝再次启动；生成和提交均在状态互斥锁下执行。
    std::uint32_t StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort) noexcept;
    // 接受四个接收端口的非零位图；IQ 仍包含全部四个端口。
    std::uint32_t ActivateRxLink(std::uint32_t rxPortBitmap) noexcept;
    // 接受 0 或 1；关闭射频时取消待取结果，保留已下载帧及配置。
    std::uint32_t SetRfPowerOn(std::uint32_t enabled) noexcept;
    // 仅支持普通模式 0；即使使用默认值，也必须执行此调用。
    std::uint32_t SetFastSweepMode(std::uint32_t mode) noexcept;
    // 容量包含空字符终止符；截断时写入带终止符的前缀，并返回 BUFFER_TOO_SMALL。
    std::uint32_t GetIDN(char *buffer, int capacity) noexcept;
    // 未模拟功率检测；有效调用将输出置零，并返回 NOT_SUPPORTED。
    // indexCapacity 为零时允许索引缓冲区为空；其他输出存储必须可写。
    std::uint32_t CheckSweepPortPower(std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap,
        std::uint16_t *overFreqIndex, std::uint32_t indexCapacity) noexcept;
    // 复制借用的帧，并原子替换对应源端口槽位；不影响待取结果。
    // 长度为精确的元素数量；调用者保证实际容量，以及缓冲区在整个调用期间有效。
    std::uint32_t DataFrameDownload(std::uint32_t pointCount, const std::uint32_t *paramData,
        std::uint32_t paramDataLen, const double *tableData, std::uint32_t tableDataLen) noexcept;

    // 为单元测试返回一致的状态摘要；加锁失败向测试执行器传播。
    NativeSimulatorSnapshot SnapshotForTest() const;
    // 为测试复制已保存的帧；端口无效或槽位为空时，保持输出不变。
    std::uint32_t CopyFrameForTest(std::uint32_t sourcePort, NormalFrameSnapshot &output) const noexcept;
    // 借用注入器，直到被替换或通过 nullptr 清除；它必须在每次调用期间保持有效。
    // 调用者不得并发销毁注入器；此处不转移所有权，也不进行引用计数。
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

}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
