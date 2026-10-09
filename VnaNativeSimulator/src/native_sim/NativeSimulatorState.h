#ifndef VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H
#define VNAPRO_NATIVE_SIM_NATIVE_SIMULATOR_STATE_H

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "NormalFrameDecoder.h"

namespace vna_native {

// 在完整候选对象构造后、替换共享状态前注入异常，用于验证失败时旧状态仍可用。
enum class FailurePoint { kBeforeFrameCommit, kBeforePendingCommit };

// 注入器在持有状态锁时调用；实现不得回调本状态对象，以免重复加锁。
class VNA_NATIVE_HIDDEN FailureInjector {
public:
    virtual ~FailureInjector() {}
    virtual void ThrowIfRequested(FailurePoint point) = 0;
};

// 仅暴露状态摘要；完整下载帧通过 CopyFrameForTest 另行复制。
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

// 管理普通模式的配置、各源端口下载帧及唯一一份待读取的扫描结果。
// Instance 为导出 API 提供共享状态；直接构造实例可让单元测试互不干扰。
// 扫描数据在 StartSweep 内同步生成，不存在后台采集线程或结果队列。
class VNA_NATIVE_HIDDEN NativeSimulatorState {
public:
    NativeSimulatorState();

    static NativeSimulatorState& Instance();

    // 重置配置、下载帧及 pending，随后保持已初始化、RF 关闭的状态。
    std::uint32_t HwInit();
    // 调用方须提供 pointCount * kIqValuesPerPoint 个 double 的可写空间；接口无容量参数。
    // 只有点数与 pending 一致时才复制并消费结果；错误返回不会消费 pending。
    std::uint32_t GetSweepData(std::uint32_t pointCount, double* iqData);
    // 前置条件：RF 已开、接收链路与普通模式已配置、该源端口存在同点数的帧。
    // sourcePort 为 0..3；有未消费的 pending 时不能开始下一次扫描。
    std::uint32_t StartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
    // 位图低四位对应四个接收端口，要求至少选中一个端口。
    std::uint32_t ActivateRxLink(std::uint32_t rxPortBitmap);
    // enabled 仅接受 0/1；关闭 RF 会丢弃 pending，但保留配置及下载帧。
    std::uint32_t SetRfPowerOn(std::uint32_t enabled);
    // 当前只支持 mode=0；即使值默认为 0，也必须显式调用以完成配置。
    std::uint32_t SetFastSweepMode(std::uint32_t mode);
    // capacity 包含末尾 NUL；容量不足时写入带 NUL 的截断字符串并返回错误。
    std::uint32_t GetIDN(char* buffer, int capacity);
    // 尚未模拟功率检测；合法调用会清零输出并返回 NOT_SUPPORTED。
    std::uint32_t CheckSweepPortPower(std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap,
        std::uint16_t* overFreqIndex, std::uint32_t indexCapacity);
    // 解码成功后替换帧内 sourcePort 对应的槽位；数组内容会复制为模块自有数据。
    // 下载本身不启动扫描，也不修改已生成的 pending。
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

    // valid 为 false 时无待读取结果；消费或取消时与默认构造的空对象交换。
    // 下载帧可继续更新，但这里保存的是 StartSweep 当次生成的数据快照。
    struct PendingSweep {
        bool valid;
        std::uint32_t pointCount;
        std::uint32_t sourcePort;
        std::vector<double> iqData;

        PendingSweep();
        void Swap(PendingSweep& other) noexcept;
    };

    void ThrowIfRequested(FailurePoint point);

    // 一把锁保护下方全部状态；解码、生成与结果复制也在锁内完成，避免跨调用混用配置。
    // 锁保证单次调用的状态一致性；调用方仍需协调多步配置/扫描流程的调用顺序。
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
