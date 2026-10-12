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
#include "vna_core/sweep_plan.h"
#include "vna_display/display_workspace.h"
#include "vna_display/trace_projection.h"
#include "vna_service/data_result.h"
#include "vna_service/instrument_error.h"
#include "vna_service/instrument_profile.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/instrument_snapshot.h"
#include "vna_service/measurement.h"
#include "vna_service/operation_result.h"
#include "vna_service/service_task.h"

namespace VnaService {

// 新增或复制的显示目标，与目录及显示版本共同构成一个事务。
struct TraceCreationOptions final {
    unsigned diagram = 0U;
    bool newDiagram = false;
};

// 线程约束：启动后的所有调用均由服务执行线程完成。
// 引用的核心必须比服务存活更久；协议状态不随客户端断开而销毁。
class InstrumentService final {
public:
    // 绑定生命周期覆盖本服务的核心；后续调用统一由服务执行线程完成。
    explicit InstrumentService(VnaCore::AcquisitionCore &core, InstrumentProfile profile = {});
    // 已接收的操作及协议使用者全部结束后，才能销毁本服务。
    ~InstrumentService();
    // 不能将执行线程约束及核心绑定关系转移给另一实例。
    InstrumentService(InstrumentService &&) = delete;
    InstrumentService &operator=(InstrumentService &&) = delete;
    InstrumentService(const InstrumentService &) = delete;
    InstrumentService &operator=(const InstrumentService &) = delete;

    // 仅在启动时调用的阻塞操作；接受已验证的 NativeSimulator 标识，并通过 error 返回失败信息。
    bool BootstrapMockProfile(std::string &error);
    // 推进一个待完成的状态迁移，不阻塞等待设备 I/O。
    void Poll();
    // 报告有限服务操作的先进先出队列中是否仍有已接收操作。
    bool HasPending() const noexcept;
    // 复制当前已接收操作的 future；future 就绪表示对应操作已经完成。
    std::vector<Operation> PendingOperations() const;
    // 使用非空所有者令牌获取空闲仪器的控制权；发生竞争时返回假。
    bool TryAcquireControl(const std::string &owner = "SCPI");
    // 停止接收匹配所有者的写操作，并继续完成此前已接收的操作。
    void BeginReleaseControl(const std::string &owner) noexcept;
    // 仅在所有已接收操作完成后，清除匹配的所有者。
    void ReleaseControl(const std::string &owner = "SCPI") noexcept;
    // 返回当前所有者令牌；控制权空闲时返回空字符串。
    std::string ControlOwner() const;
    // 报告当前所有者是否正等待已接收操作全部完成。
    bool ControlReleasing() const noexcept;
    // 返回当前执行轮次的设置、核心发布数据和控制状态。
    InstrumentSnapshot Snapshot() const;

    // 返回已提交的设置；返回的引用仅在本服务存活期间有效。
    const InstrumentSettings &Settings() const noexcept;
    // 所有协议读取同一份显示域状态，返回引用仅在服务存活期间有效。
    const VnaDisplay::DisplayState &Display() const noexcept;
    // 显示操作复用控制权与有限 FIFO；不改变采集计划或扫描模式。
    Operation ApplyDisplay(VnaDisplay::DisplayChange change, std::uint64_t expectedDisplayRevision = 0U,
        const std::string &owner = "SCPI");
    // 从当前完整帧计算格式值及独立 Marker 读数；名称为空时读取当前选中测量。
    VnaDisplay::ProjectedTrace DisplayData(const std::string &name = std::string()) const;
    // 后端分配新名称并原子添加、选择副本，前端无需复制目录控制规则。
    Operation CloneMeasurement(const std::string &sourceName, std::uint64_t expectedRevision,
        std::uint64_t expectedDisplayRevision, const std::string &owner, TraceCreationOptions options = {});
    // 原生图窗菜单新增默认 S21，与图窗创建、选择一并原子提交。
    Operation AddMeasurement(std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner, TraceCreationOptions options = {});
    // 原生图窗删除连同所属测量定义一起提交，并保护最后图窗和最后迹线。
    Operation DeleteDiagram(unsigned diagram, std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner);
    // 返回启动时已验证的仪器能力，引用在本服务存活期间有效。
    const InstrumentProfile &Profile() const noexcept;
    // 返回已验证的模拟设备标识，或明确标注为未验证的后端标识。
    std::string Identity() const;
    // 将仪器支持频段内的端点加入队列；端点交叉时保持原有频率跨度。
    Operation SetStartHz(double value, std::uint64_t expectedRevision = 0U);
    // 将端点加入队列；取整及联动调整后的两个端点都必须在仪器支持频段内。
    Operation SetStopHz(double value, std::uint64_t expectedRevision = 0U);
    // 将 2～10001 个扫描点的配置加入队列；expectedRevision 为零时跳过 SCPI 版本检查。
    Operation SetPoints(unsigned value, std::uint64_t expectedRevision = 0U);
    // 将正带宽加入队列，并向上取整为模拟设备支持的带宽值。
    Operation SetIfBandwidthHz(double value, std::uint64_t expectedRevision = 0U);
    // 在 READY 或 RUNNING 下排入原子配置；完整帧边界提交，保留运行模式、控制权与版本校验。
    Operation ApplyConfiguration(ConfigPatch patch, std::uint64_t expectedRevision, const std::string &owner);
    // 添加唯一的 1～128 字节测量名称；采集中允许更新目录，新增硬件需求在完整帧边界提交。
    // 端口必须在仪器能力内；未校准 S 参数目前支持 S11、S21、S12、S22。
    Operation DefineMeasurement(
        Measurement measurement, std::uint64_t expectedRevision = 0U, const std::string &owner = "SCPI");
    // 原位更新同名测量并保留顺序和选择；已有采集需求即时生效，缺少的源或接收机在帧边界提交。
    Operation UpdateMeasurement(
        Measurement measurement, std::uint64_t expectedRevision = 0U, const std::string &owner = "SCPI");
    // 删除区分大小写的已有测量项并按需更新选择；保留已下发的采集需求，不中断当前运行。
    Operation DeleteMeasurement(
        const std::string &name, std::uint64_t expectedRevision = 0U, const std::string &owner = "SCPI");
    // 将有限的触发源迁移操作加入队列；所有者必须持有有效的仪器控制权。
    Operation SetTriggerSource(TriggerSource source, const std::string &owner = "SCPI");
    // 将 HOLD、SINGLE 或 CONTINUOUS 模式切换加入队列；迁移屏障完成后才返回操作结果。
    Operation SetSweepMode(SweepMode mode, const std::string &owner = "SCPI");
    // 将内部触发的一帧采集加入队列，随后经过 Stop 屏障并返回 HOLD 模式。
    Operation RunSingle(const std::string &owner);
    // 将使用内部触发源的连续采集操作加入队列。
    Operation RunContinuous(const std::string &owner);
    // 为当前已准备的扫描轮次排入手动 INIT；拒绝与现有有限测量重叠的请求。
    Operation Trigger(const std::string &owner = "SCPI");
    // 原生同步入口仅在持有 SCPI 控制权且 FIFO 为空时可用；协议写调用 ApplyDisplay。
    bool SelectMeasurement(const std::string &name);
    // 更新单通道作用域标志；要求持有 SCPI 控制权且没有待处理配置。
    bool SetCurrentTriggerScope(bool current);
    // 返回已提交的 Hz 频率轴；核心故障或版本尚未提交时返回错误。
    DataResult StimulusAxis() const;
    // 仅从当前扫描轮次和版本的完整帧计算交错复数数据；任一点无效时整条读取失败。
    DataResult MeasurementData() const;

    // 追加 IEEE 488.2/SCPI 错误，并更新对应的事件寄存器位。
    void RecordError(int code, const std::string &message);
    // 移出队列中最早的错误；队列为空时返回错误码零及 No error。
    InstrumentError PopError();
    // 返回有容量上限的错误队列长度，不移除任何条目。
    unsigned ErrorCount() const noexcept;
    // 清除错误和事件状态，并递增待处理状态监视使用的代次。
    void ClearStatus() noexcept;
    // 返回由 ClearStatus 递增的状态代次。
    unsigned StatusGeneration() const noexcept;
    // 保存 ESE 的低八位。
    void SetEventEnable(unsigned value) noexcept;
    // 返回当前 ESE 掩码。
    unsigned EventEnable() const noexcept;
    // 保存支持的 SRE 位，并排除请求汇总位。
    void SetServiceRequestEnable(unsigned value) noexcept;
    // 返回当前 SRE 掩码。
    unsigned ServiceRequestEnable() const noexcept;
    // 在服务执行线程内原子地读取并清零 ESR。
    unsigned ReadEventRegister() noexcept;
    // 相关完成屏障结束后，设置 ESR 的操作完成位。
    void MarkOperationComplete() noexcept;
    // 综合错误队列、传输输出、ESR/ESE 和 SRE，生成 IEEE 488.2 状态字节。
    unsigned StatusByte(bool outputAvailable) const noexcept;

private:
    // IEEE 488.2 规定仪器上电时设置此 ESR 位。
    static constexpr unsigned POWER_ON_EVENT = 128U;

    void Error(int code, const std::string &text);
    Operation Rejected(int code, const std::string &message, const std::string &requester = "SCPI");
    Operation Enqueue(Detail::ServiceTask task, const std::string &requester = "SCPI");
    Operation Configure(std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected,
        const std::string &requester = "SCPI");
    Operation ConfigureMeasurements(std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected,
        const std::string &requester);
    bool HasMeasurement() const noexcept;
    bool HasConfiguration() const noexcept;
    void Finish(OperationResult value);
    bool Consume(Detail::ServiceTask &task);
    void CommitMode(Detail::ServiceTask &task);
    void PollTask();
    void PollConfiguration(Detail::ServiceTask &task);
    void PollDisplay(Detail::ServiceTask &task);
    VnaDisplay::FrameContext DisplayFrame(bool reachedBarrier = false) const;
    bool PrepareDisplayConfiguration(Detail::ServiceTask &task);
    Operation CreateMeasurement(const std::string &sourceName, std::uint64_t expectedRevision,
        std::uint64_t expectedDisplayRevision, const std::string &owner, TraceCreationOptions options);
    bool PrepareConfiguration(Detail::ServiceTask &task);
    void CommitConfiguration(Detail::ServiceTask &task);
    void PollTrigger(Detail::ServiceTask &task);
    bool BeginMode(Detail::ServiceTask &task);
    void PollMode(Detail::ServiceTask &task);
    void PollStartedMode(Detail::ServiceTask &task);

    VnaCore::AcquisitionCore &core_;
    const InstrumentProfile profile_;
    InstrumentSettings settings_;
    VnaDisplay::DisplayWorkspace display_;
    // 缓存核心最后成功下发的真实计划，其源和接收需求可以覆盖当前目录的子集。
    VnaCore::SweepPlan activePlan_;
    std::deque<Detail::ServiceTask> tasks_;
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

}  // 命名空间 VnaService
#endif
