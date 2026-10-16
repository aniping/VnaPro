/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H
#define VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "vna_core/acquisition_core.h"
#include "vna_core/sweep_plan.h"
#include "vna_display/display_workspace.h"
#include "vna_display/trace_projection.h"
#include "vna_service/calibration_workspace.h"
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
    VnaDisplay::TraceDropPosition position = VnaDisplay::TraceDropPosition::CENTER;
    // 仅新增可指定测量量；为空时使用仪器默认量，复制始终继承源测量量。
    std::string parameter;
};

struct ChannelCreationOptions final {
    unsigned sourceChannelId = 0U;
    bool copyChannel = false;
    bool newDiagram = true;
};

// 线程约束：启动后的所有调用均由服务执行线程完成。
// 引用的核心必须比服务存活更久；协议状态不随客户端断开而销毁。
class InstrumentService final {
public:
    using CalibrationSaveHandler = std::function<Operation(unsigned, std::vector<CalibrationSetPtr>, std::uint64_t)>;
    // 绑定生命周期覆盖本服务的核心；后续调用统一由服务执行线程完成。
    explicit InstrumentService(
        VnaCore::AcquisitionCore &core, InstrumentProfile profile = InstrumentProfile{}, bool simulator = true);
    // 已接收的操作及协议使用者全部结束后，才能销毁本服务。
    ~InstrumentService();
    // 不能将执行线程约束及核心绑定关系转移给另一实例。
    InstrumentService(InstrumentService &&) = delete;
    InstrumentService &operator=(InstrumentService &&) = delete;
    InstrumentService(const InstrumentService &) = delete;
    InstrumentService &operator=(const InstrumentService &) = delete;

    // 仅在启动时调用的阻塞操作；核心就绪后准备仪器配置，通过 error 返回失败信息。
    bool BootstrapProfile(std::string &error);
    // 只在监听前、无控制者和待处理操作时恢复已验证的池；恢复后不自动启用。
    bool RestoreCalibration(
        std::vector<CalibrationSetPtr> sets, std::uint64_t selectedSetId, std::string &error, unsigned channelId = 1U);
    // 保存由外部适配器异步执行；future 成功前不提交校准候选或完成 OPC。
    void SetCalibrationSaveHandler(CalibrationSaveHandler handler);
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
    InstrumentSnapshot Snapshot(unsigned channelId = 0U) const;
    // 活动迹线所属通道；无迹线通道由显式 Channel Manager 选择维护。
    unsigned ActiveChannelId() const noexcept;
    bool HasChannel(unsigned channelId) const noexcept;
    unsigned MeasurementChannel(const std::string &name) const noexcept;
    std::string SelectedMeasurement(unsigned channelId = 0U) const;
    InstrumentChannelSnapshot ChannelSnapshot(unsigned channelId) const;
    Operation CreateChannel(ChannelCreationOptions options, std::uint64_t expectedRevision,
        std::uint64_t expectedDisplayRevision, const std::string &owner);
    Operation SelectChannel(unsigned channelId, std::uint64_t expectedDisplayRevision, const std::string &owner);
    Operation DeleteChannel(unsigned channelId, std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner);

    // 返回已提交的设置；返回的引用仅在本服务存活期间有效。
    const InstrumentSettings &Settings(unsigned channelId = 0U) const;
    // 所有协议读取同一份显示域状态，返回引用仅在服务存活期间有效。
    const VnaDisplay::DisplayState &Display() const noexcept;
    // 显示操作复用控制权与有限 FIFO；不改变采集计划或扫描模式。
    Operation ApplyDisplay(VnaDisplay::DisplayChange change, std::uint64_t expectedDisplayRevision = 0U,
        const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 从当前完整帧计算格式值及独立 Marker 读数；名称为空时读取当前选中测量。
    VnaDisplay::ProjectedTrace DisplayData(const std::string &name = std::string(), unsigned channelId = 0U) const;
    // 后端分配新名称并原子添加、选择副本，前端无需复制目录控制规则。
    Operation CloneMeasurement(const std::string &sourceName, std::uint64_t expectedRevision,
        std::uint64_t expectedDisplayRevision, const std::string &owner, TraceCreationOptions options = {},
        unsigned channelId = 0U);
    // 原子新增指定测量量或默认 S21（单端口 S11），同时提交命名、图窗归属与选择。
    Operation AddMeasurement(std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner, TraceCreationOptions options = {}, unsigned channelId = 0U);
    // 整组操作只替换目标通道旧定义，一次提交 n² 参数与独立图窗，保留其他通道。
    Operation CreateAllSParameters(std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner, unsigned channelId = 0U);
    // 原生图窗删除连同所属测量定义一起提交，并保护最后图窗和最后迹线。
    Operation DeleteDiagram(unsigned diagram, std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
        const std::string &owner, unsigned channelId = 0U);
    // 返回启动时已验证的仪器能力，引用在本服务存活期间有效。
    const InstrumentProfile &Profile() const noexcept;
    // 返回初始化时确定的驱动类别，运行期间不改变。
    bool IsSimulator() const noexcept;
    // 返回模拟仪器能力标识或核心缓存的真实设备标识；配置未就绪时返回未验证标识。
    std::string Identity() const;
    // 将仪器支持频段内的端点加入队列；端点交叉时保持原有频率跨度。
    Operation SetStartHz(double value, std::uint64_t expectedRevision = 0U, unsigned channelId = 0U);
    // 将端点加入队列；取整及联动调整后的两个端点都必须在仪器支持频段内。
    Operation SetStopHz(double value, std::uint64_t expectedRevision = 0U, unsigned channelId = 0U);
    // 将 2～10001 个扫描点的配置加入队列；expectedRevision 为零时跳过 SCPI 版本检查。
    Operation SetPoints(unsigned value, std::uint64_t expectedRevision = 0U, unsigned channelId = 0U);
    // 将正带宽加入队列，并向上取整为模拟设备支持的带宽值。
    Operation SetIfBandwidthHz(double value, std::uint64_t expectedRevision = 0U, unsigned channelId = 0U);
    // 在 READY 或 RUNNING 下排入原子配置；完整帧边界提交，保留运行模式、控制权与版本校验。
    Operation ApplyConfiguration(
        ConfigPatch patch, std::uint64_t expectedRevision, const std::string &owner, unsigned channelId = 0U);
    // 将有限的 [-120, 100] dBm 写入目标通道真实采集计划。
    Operation SetPowerDbm(double value, const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 框选原子修改真实频率范围和活动迹线纵轴；撤回只恢复该次 Zoom 的字段，双版本必须匹配。
    Operation ApplyStimulusZoom(StimulusZoomChange change, std::uint64_t expectedRevision,
        std::uint64_t expectedDisplayRevision, const std::string &owner, unsigned channelId = 0U);
    // 返回当前活动迹线的框选能力，以及最近一次 Zoom 是否仍能安全撤回。
    StimulusZoomStatus StimulusZoom(unsigned channelId = 0U) const;
    // 添加唯一的 1～128 字节测量名称；采集中允许更新目录，新增硬件需求在完整帧边界提交。
    // 未校准 Sij 的接收端口 i 和源端口 j 都必须在启动配置的仪器端口能力内。
    Operation DefineMeasurement(Measurement measurement, std::uint64_t expectedRevision = 0U,
        const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 原位更新同名测量并保留顺序和选择；已有采集需求即时生效，缺少的源或接收机在帧边界提交。
    Operation UpdateMeasurement(Measurement measurement, std::uint64_t expectedRevision = 0U,
        const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 删除区分大小写的已有测量项并按需更新选择；保留已下发的采集需求，不中断当前运行。
    Operation DeleteMeasurement(const std::string &name, std::uint64_t expectedRevision = 0U,
        const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 一次事务移除目标通道的全部测量及显示引用，不触碰其他通道的目录和采集。
    Operation DeleteAllMeasurements(const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 等待真实停止及默认物理计划下发后，提交默认测量、显示与 HOLD/IMMEDIATE；保留校准集存储。
    // 仪器级复位的最后一个通道可同时重建全局默认显示；普通通道复位保留其他显示设置。
    Operation ResetMeasurements(
        const std::string &owner = "SCPI", unsigned channelId = 0U, bool resetInstrumentDisplay = false);
    // 将有限的触发源迁移操作加入队列；所有者必须持有有效的仪器控制权。
    Operation SetTriggerSource(TriggerSource source, const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 将 HOLD、SINGLE 或 CONTINUOUS 模式切换加入队列；迁移屏障完成后才返回操作结果。
    Operation SetSweepMode(SweepMode mode, const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 改选 Continuous 即按当前触发源运行；改选 Single 经完整帧停止屏障后等待 StartSweep。
    Operation SelectSweepMode(SweepMode mode, const std::string &owner, unsigned channelId = 0U);
    // SCPI INIT:CONT OFF：即使已选 Single，也等待真实停止后保持 HOLD，不改变触发源。
    Operation SelectSingleAndHold(const std::string &owner, unsigned channelId = 0U);
    // 选择 Free Run 或 Manual，保留运行或停止意图；运行中经 Stop 屏障重启，Manual 只布防。
    Operation SelectTriggerSource(TriggerSource source, const std::string &owner, unsigned channelId = 0U);
    // 使用当前所选扫描模式和触发源启动；Manual 只布防，单次完成后保留选择。
    Operation StartSweep(const std::string &owner, unsigned channelId = 0U);
    // 保留触发源的兼容单次快捷动作；原子选择 Single 并启动。
    Operation RunSelectedSingle(const std::string &owner, unsigned channelId = 0U);
    // 等待采集停止屏障，不改变所选扫描模式或触发源。
    Operation StopSweep(const std::string &owner, unsigned channelId = 0U);
    // 将内部触发的一帧采集加入队列，随后经过 Stop 屏障并返回 HOLD 模式。
    Operation RunSingle(const std::string &owner, unsigned channelId = 0U);
    // 将使用内部触发源的连续采集操作加入队列。
    Operation RunContinuous(const std::string &owner, unsigned channelId = 0U);
    // 为当前已准备的扫描轮次排入手动 INIT；拒绝与现有有限测量重叠的请求。
    Operation Trigger(const std::string &owner = "SCPI", unsigned channelId = 0U);
    // 原生同步入口仅在持有 SCPI 控制权且 FIFO 为空时可用；协议写调用 ApplyDisplay。
    bool SelectMeasurement(const std::string &name, unsigned channelId = 0U);
    // 更新单通道作用域标志；要求持有 SCPI 控制权且没有待处理配置。
    bool SetCurrentTriggerScope(bool current, unsigned channelId = 0U);
    // 返回已提交的 Hz 频率轴；核心故障或版本尚未提交时返回错误。
    DataResult StimulusAxis(unsigned channelId = 0U) const;
    // 仅从当前扫描轮次和版本的完整帧计算交错复数数据；任一点无效时整条读取失败。
    DataResult MeasurementData(bool calibrated = true, unsigned channelId = 0U) const;
    // 序号按目标通道目录顺序从 1 开始；同一帧逐迹线返回交错复数，校正读取须覆盖全部 S 参数。
    DataResult MeasurementData(const std::vector<unsigned> &selection, bool calibrated, unsigned channelId = 0U) const;
    // 校准事务复用仪器控制权和有限 FIFO；原始标准每次启动全新完整扫描。
    Operation ApplyCalibration(CalibrationChange change, std::uint64_t expectedCalibrationRevision = 0U,
        const std::string &owner = "SCPI", unsigned channelId = 0U);
    CalibrationSnapshot Calibration(unsigned channelId = 0U) const;
    CalibrationSetPtr FindCalibrationSet(std::uint64_t id, unsigned channelId = 0U) const;
    CalibrationPreviewResult CalibrationPreview(
        std::uint64_t sessionId, const std::string &stepId, unsigned channelId = 0U) const;

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
    struct CalibrationRunState final {
        std::uint64_t sessionId = 0U;
        TriggerSource source = TriggerSource::IMMEDIATE;
        SweepMode mode = SweepMode::HOLD;
    };

    struct ChannelState final {
        bool committed = true;
        InstrumentSettings settings;
        CalibrationWorkspace calibration;
        CalibrationRunState calibrationRun;
        VnaCore::SweepPlan activePlan;
        std::deque<Detail::StimulusZoomHistoryEntry> zoomHistory;
        std::string selectedMeasurement;
    };

    // 仅交换单线程执行上下文；全局目录、显示、控制权和 FIFO 不随通道复制。
    class ContextScope final {
    public:
        ContextScope(
            InstrumentService &service, unsigned channelId, const std::string &traceName = {}, bool executing = false);
        ~ContextScope();
        bool Valid() const noexcept;

    private:
        InstrumentService &service_;
        unsigned previous_;
        bool previousExecuting_;
        bool valid_ = false;
    };

    bool ActivateContext(unsigned channelId);
    void CommitLogicalRevision(std::uint64_t revision) noexcept;
    void RememberSelection();
    void PollChannel(Detail::ServiceTask &task);
    bool PrepareChannelCreation(Detail::ServiceTask &task);
    bool PrepareChannelDeletion(Detail::ServiceTask &task);
    VnaCore::SweepPlan MakeChannelPlan(const InstrumentSettings &settings, unsigned channelId) const;
    std::vector<std::string> ChannelTraceNames(unsigned channelId) const;
    bool CalibrationCollecting() const noexcept;

    // IEEE 488.2 规定仪器上电时设置此 ESR 位。
    static constexpr unsigned POWER_ON_EVENT = 128U;

    void Error(int code, const std::string &text);
    Operation Rejected(int code, const std::string &message, const std::string &requester = "SCPI");
    Operation Enqueue(Detail::ServiceTask task, const std::string &requester = "SCPI");
    Operation Configure(std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected,
        const std::string &requester = "SCPI");
    Operation ConfigureMeasurements(std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected,
        const std::string &requester);
    static bool ValidMeasurement(const Measurement &measurement, const InstrumentProfile &profile) noexcept;
    bool HasMeasurement() const noexcept;
    bool HasConfiguration() const noexcept;
    void Finish(OperationResult value);
    bool Consume(Detail::ServiceTask &task);
    void CommitMode(Detail::ServiceTask &task);
    void PollTask();
    void PollConfiguration(Detail::ServiceTask &task);
    void PollReset(Detail::ServiceTask &task);
    DataResult ReadMeasurementData(
        const std::vector<Measurement> &measurements, bool calibrated, bool requireCalibration = false) const;
    void PollDisplay(Detail::ServiceTask &task);
    bool PrepareStimulusZoom(Detail::ServiceTask &task);
    void PollStimulusZoom(Detail::ServiceTask &task);
    void InvalidateZoomHistory(const Detail::ServiceTask &task) noexcept;
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
    void PollCalibration(Detail::ServiceTask &task);
    bool PrepareCalibration(Detail::ServiceTask &task);
    void ContinueCalibration(Detail::ServiceTask &task);
    bool BeginCalibrationSave(Detail::ServiceTask &task);
    void FailCalibration(const std::string &error, int code);

    VnaCore::AcquisitionCore &core_;
    const InstrumentProfile profile_;
    const bool simulator_;
    // Loaded channel data is a single-executor cache; const queries may exchange
    // contexts without changing the instrument's committed logical state.
    mutable InstrumentSettings settings_;
    VnaDisplay::DisplayWorkspace display_;
    mutable CalibrationWorkspace calibration_;
    CalibrationSaveHandler calibrationSave_;
    // 仅成功 BEGIN 冻结一次，标准采集不得覆盖进入前的扫描状态。
    mutable CalibrationRunState calibrationRun_;
    // 缓存核心最后成功下发的真实计划，其源和接收需求可以覆盖当前目录的子集。
    mutable VnaCore::SweepPlan activePlan_;
    mutable std::deque<Detail::StimulusZoomHistoryEntry> zoomHistory_;
    mutable std::map<unsigned, ChannelState> channels_;
    mutable std::map<unsigned, InstrumentSettings> settingsViews_;
    std::uint64_t logicalRevision_ = 0U;
    mutable unsigned contextChannelId_ = 1U;
    unsigned selectedChannelId_ = 1U;
    unsigned nextChannelId_ = 2U;
    mutable bool executingTask_ = false;
    mutable unsigned contextDepth_ = 0U;
    std::deque<Detail::ServiceTask> tasks_;
    std::deque<InstrumentError> errors_;
    bool profileReady_ = false;
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
