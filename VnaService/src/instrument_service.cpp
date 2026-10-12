/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_service/instrument_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <set>
#include <utility>

#include "vna_core/acquisition_core.h"
#include "vna_core/frame_layout.h"
#include "vna_log/logger.h"
#include "vna_measurement/measurement_processor.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
constexpr std::size_t TASK_QUEUE_CAPACITY = 32U;
constexpr std::size_t ERROR_QUEUE_CAPACITY = 32U;
constexpr std::size_t MEASUREMENT_CAPACITY = 32U;
constexpr std::size_t MAX_MEASUREMENT_NAME_LENGTH = 128U;
constexpr std::uint64_t SETTINGS_REVISION_MAX = (std::numeric_limits<std::uint64_t>::max)();
constexpr unsigned COMMAND_ERROR_EVENT = 32U;
constexpr unsigned EXECUTION_ERROR_EVENT = 16U;
constexpr unsigned DEVICE_ERROR_EVENT = 8U;
constexpr unsigned QUERY_ERROR_EVENT = 4U;
constexpr unsigned OPERATION_COMPLETE_EVENT = 1U;
constexpr unsigned ERROR_QUEUE_STATUS = 4U;
constexpr unsigned MESSAGE_AVAILABLE_STATUS = 16U;
constexpr unsigned EVENT_SUMMARY_STATUS = 32U;
constexpr unsigned SERVICE_REQUEST_STATUS = 64U;
constexpr unsigned EVENT_ENABLE_MASK = 255U;
constexpr unsigned SERVICE_REQUEST_ENABLE_MASK = 191U;
constexpr int COMMAND_ERROR_RANGE_BEGIN = -100;
constexpr int EXECUTION_ERROR_RANGE_BEGIN = -200;
constexpr int DEVICE_ERROR_RANGE_BEGIN = -300;
constexpr int QUERY_ERROR_RANGE_BEGIN = -400;
constexpr int RESERVED_ERROR_RANGE_BEGIN = -500;

const char *TaskName(Detail::ServiceTaskKind kind) noexcept
{
    switch (kind) {
        case Detail::ServiceTaskKind::CONFIGURE:
            return "CONFIGURE";
        case Detail::ServiceTaskKind::MEASUREMENTS:
            return "MEASUREMENTS";
        case Detail::ServiceTaskKind::DISPLAY:
            return "DISPLAY";
        case Detail::ServiceTaskKind::SOURCE:
            return "SOURCE";
        case Detail::ServiceTaskKind::MODE:
            return "MODE";
        case Detail::ServiceTaskKind::TRIGGER:
            return "TRIGGER";
        default:
            return "UNKNOWN";
    }
}

const char *ModeName(SweepMode mode) noexcept
{
    switch (mode) {
        case SweepMode::HOLD:
            return "HOLD";
        case SweepMode::SINGLE:
            return "SINGLE";
        case SweepMode::CONTINUOUS:
            return "CONTINUOUS";
        default:
            return "UNKNOWN";
    }
}

OperationResult Result(bool ok, int code = 0, const std::string &message = std::string())
{
    OperationResult value;
    value.ok = ok;
    value.errorCode = code;
    value.message = message;
    return value;
}

bool Ready(const VnaCore::Ticket &ticket)
{
    return ticket.valid() && ticket.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool ValidFrequency(double value, const InstrumentProfile &profile) noexcept
{
    return std::isfinite(value) && value >= profile.minFrequencyHz && value <= profile.maxFrequencyHz;
}

bool ValidMeasurement(const Measurement &measurement, const InstrumentProfile &profile) noexcept
{
    return !measurement.name.empty() && measurement.name.size() <= MAX_MEASUREMENT_NAME_LENGTH &&
           measurement.IsSupported() && measurement.sourcePort <= profile.portCount &&
           measurement.receiverPort <= profile.portCount;
}

double QuantizedBandwidth(double value) noexcept
{
    const double values[] = {10.0, 100.0, 1000.0, 10000.0, 100000.0};
    for (double valid : values) {
        if (value <= valid) {
            return valid;
        }
    }
    return 0.0;
}

VnaCore::SweepPlan MakePlan(const InstrumentSettings &settings)
{
    VnaCore::SweepPlan plan;
    plan.ifBandwidthRaw = static_cast<std::uint32_t>(settings.ifBandwidthHz);
    std::set<unsigned> sources;
    for (const Measurement &measurement : settings.measurements) {
        sources.insert(measurement.sourcePort);
        plan.receiverMask |= 1U << (measurement.receiverPort - 1U);
        if (measurement.kind == MeasurementKind::S_PARAMETER) {
            // 每个 S 参数使用自身激励端口的参考波，必须与响应波来自同一源帧。
            plan.receiverMask |= 1U << (measurement.sourcePort - 1U);
        }
    }
    if (sources.empty()) {
        sources.insert(1U);
    }
    if (plan.receiverMask == 0U) {
        plan.receiverMask = 1U;
    }
    plan.sourcePorts.assign(sources.begin(), sources.end());
    for (unsigned point = 0; point < settings.points; ++point) {
        const double hz = settings.startHz + (settings.stopHz - settings.startHz) * static_cast<double>(point) /
                                                 static_cast<double>(settings.points - 1U);
        // 明确的模拟设备约定：一个原始频率计数对应 1 Hz；此处不解释驱动数据表。
        const std::uint64_t raw = static_cast<std::uint64_t>(std::llround(hz));
        plan.frequencyRaw.push_back(raw);
        plan.frequencyHz.push_back(static_cast<double>(raw));
    }
    plan.tableData.assign(static_cast<std::size_t>(settings.points) * VnaCore::TABLE_VALUES_PER_POINT, 0.0);
    return plan;
}

bool CoversPlan(const VnaCore::SweepPlan &active, const VnaCore::SweepPlan &requested)
{
    // 保留已下发的源和接收机；目录收缩不重启扫描，也不使已有完整 IQ 失效。
    return active.frequencyRaw == requested.frequencyRaw && active.frequencyHz == requested.frequencyHz &&
           active.ifBandwidthRaw == requested.ifBandwidthRaw && active.tableData == requested.tableData &&
           (active.receiverMask & requested.receiverMask) == requested.receiverMask &&
           std::includes(active.sourcePorts.begin(), active.sourcePorts.end(), requested.sourcePorts.begin(),
               requested.sourcePorts.end());
}

OperationResult ApplyCenterSpanPatch(
    const ConfigPatch &patch, InstrumentSettings &settings, const InstrumentProfile &profile)
{
    if (patch.hasStartHz || patch.hasStopHz) {
        return Result(false, Detail::INVALID_PARAMETER_ERROR, "Center/span cannot be mixed with start/stop");
    }
    const double center = patch.hasCenterHz ? patch.centerHz : (settings.startHz + settings.stopHz) / 2.0;
    const double span = patch.hasSpanHz ? patch.spanHz : settings.stopHz - settings.startHz;
    if (!std::isfinite(center) || !std::isfinite(span) || span < 0.0) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Invalid center or span frequency");
    }
    const double start = std::round(center - span / 2.0);
    const double stop = std::round(center + span / 2.0);
    if (!ValidFrequency(start, profile) || !ValidFrequency(stop, profile)) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Center/span exceeds instrument frequency range");
    }
    settings.startHz = start;
    settings.stopHz = stop;
    return Result(true);
}

OperationResult ApplyFrequencyPatch(
    const ConfigPatch &patch, InstrumentSettings &settings, const InstrumentProfile &profile)
{
    if (patch.hasCenterHz || patch.hasSpanHz) {
        return ApplyCenterSpanPatch(patch, settings, profile);
    }
    const double span = settings.stopHz - settings.startHz;
    double start = settings.startHz;
    double stop = settings.stopHz;
    if (patch.hasStartHz) {
        if (!ValidFrequency(patch.startHz, profile)) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency out of mock range");
        }
        start = std::round(patch.startHz);
    }
    if (patch.hasStopHz) {
        if (!ValidFrequency(patch.stopHz, profile)) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Stop frequency out of mock range");
        }
        stop = std::round(patch.stopHz);
    }
    if (patch.hasStartHz && patch.hasStopHz && start > stop) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency exceeds stop frequency");
    }
    if (patch.hasStartHz && !patch.hasStopHz && start > stop) {
        stop = start + span;
    }
    if (patch.hasStopHz && !patch.hasStartHz && stop < start) {
        start = stop - span;
    }
    if (!ValidFrequency(start, profile) || !ValidFrequency(stop, profile)) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Coupled frequency endpoint out of mock range");
    }
    settings.startHz = start;
    settings.stopHz = stop;
    return Result(true);
}

OperationResult ApplySettingsPatch(
    const ConfigPatch &patch, InstrumentSettings &settings, const InstrumentProfile &profile)
{
    if (!patch.hasStartHz && !patch.hasStopHz && !patch.hasCenterHz && !patch.hasSpanHz && !patch.hasPoints &&
        !patch.hasIfBandwidthHz) {
        return Result(false, Detail::MISSING_PARAMETER_ERROR, "Configuration patch is empty");
    }
    const OperationResult frequency = ApplyFrequencyPatch(patch, settings, profile);
    if (!frequency.ok) {
        return frequency;
    }
    if (patch.hasPoints && (patch.points < MIN_SWEEP_POINTS || patch.points > MAX_SWEEP_POINTS)) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001");
    }
    if (patch.hasIfBandwidthHz) {
        if (!std::isfinite(patch.ifBandwidthHz) || patch.ifBandwidthHz <= 0 ||
            QuantizedBandwidth(patch.ifBandwidthHz) == 0) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of mock range");
        }
        settings.ifBandwidthHz = QuantizedBandwidth(patch.ifBandwidthHz);
    }
    if (patch.hasPoints) {
        settings.points = patch.points;
    }
    return Result(true);
}

DataResult ExtractMeasurement(const VnaCore::MeasurementFrame &frame, const Measurement &measurement)
{
    DataResult value;
    const auto trace = VnaMeasurement::MeasurementProcessor::Calculate(frame, measurement);
    if (!trace.available) {
        value.error = trace.error;
        return value;
    }
    value.values.reserve(trace.points.size() * VnaCore::COMPLEX_COMPONENT_COUNT);
    for (const auto &point : trace.points) {
        if (!point.valid) {
            value.values.clear();
            value.error = "Measurement frame contains invalid points";
            return value;
        }
        value.values.push_back(point.real);
        value.values.push_back(point.imag);
    }
    value.ok = true;
    return value;
}

}  // 匿名命名空间

void InstrumentService::Error(int code, const std::string &text)
{
    if (code <= COMMAND_ERROR_RANGE_BEGIN && code > EXECUTION_ERROR_RANGE_BEGIN) {
        esr_ |= COMMAND_ERROR_EVENT;
    } else if (code <= EXECUTION_ERROR_RANGE_BEGIN && code > DEVICE_ERROR_RANGE_BEGIN) {
        esr_ |= EXECUTION_ERROR_EVENT;
    } else if (code <= DEVICE_ERROR_RANGE_BEGIN && code > QUERY_ERROR_RANGE_BEGIN) {
        esr_ |= DEVICE_ERROR_EVENT;
    } else if (code <= QUERY_ERROR_RANGE_BEGIN && code > RESERVED_ERROR_RANGE_BEGIN) {
        esr_ |= QUERY_ERROR_EVENT;
    }
    InstrumentError entry;
    entry.code = code;
    entry.message = text;
    if (errors_.size() < ERROR_QUEUE_CAPACITY) {
        errors_.push_back(std::move(entry));
    } else {
        errors_.back().code = Detail::ERROR_QUEUE_OVERFLOW;
        errors_.back().message = "Queue overflow";
        esr_ |= DEVICE_ERROR_EVENT;
    }
}

Operation InstrumentService::Rejected(int code, const std::string &message, const std::string &requester)
{
    if (requester == "SCPI") {
        Error(code, message);
    }
    std::promise<OperationResult> promise;
    Operation operation = promise.get_future().share();
    promise.set_value(Result(false, code, message));
    VnaLog::Debug(VnaLog::Module::SERVICE, "控制请求被拒绝，code={}，原因={}", code, message);
    return operation;
}

Operation InstrumentService::Enqueue(Detail::ServiceTask task, const std::string &requester)
{
    if (owner_ != requester || requester.empty() || releasing_) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "No active instrument control", requester);
    }
    if (!mockReady_) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Verified backend profile is not initialized", requester);
    }
    if (tasks_.size() >= TASK_QUEUE_CAPACITY) {
        return Rejected(Detail::INPUT_BUFFER_OVERRUN, "Control input buffer overrun", requester);
    }
    task.owner = requester;
    task.generation = controlGeneration_;
    Operation operation = task.operation;
    tasks_.push_back(std::move(task));
    return operation;
}

Operation InstrumentService::Configure(
    std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected, const std::string &requester)
{
    if (settings_.sweepMode != SweepMode::HOLD || HasMeasurement() ||
        core_.ReadView().state != VnaCore::CoreState::READY) {
        return Rejected(
            Detail::SETTINGS_CONFLICT_ERROR, "Configuration requires HOLD and no pending measurement", requester);
    }
    Detail::ServiceTask task;
    task.mutate = std::move(mutate);
    task.expectedRevision = expected;
    task.checkRevision = expected != 0U || requester != "SCPI";
    return Enqueue(std::move(task), requester);
}

Operation InstrumentService::ConfigureMeasurements(
    std::function<OperationResult(InstrumentSettings &)> mutate, std::uint64_t expected, const std::string &requester)
{
    const auto state = core_.ReadView().state;
    if (state != VnaCore::CoreState::READY && state != VnaCore::CoreState::RUNNING) {
        return Rejected(
            Detail::SETTINGS_CONFLICT_ERROR, "Measurement configuration requires Ready or Running", requester);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MEASUREMENTS;
    task.mutate = std::move(mutate);
    task.expectedRevision = expected;
    task.checkRevision = expected != 0U || requester != "SCPI";
    return Enqueue(std::move(task), requester);
}

bool InstrumentService::HasMeasurement() const noexcept
{
    for (const auto &task : tasks_) {
        if (task.measurement) {
            return true;
        }
    }
    return false;
}

bool InstrumentService::HasConfiguration() const noexcept
{
    for (const auto &task : tasks_) {
        if (task.kind == Detail::ServiceTaskKind::CONFIGURE || task.kind == Detail::ServiceTaskKind::MEASUREMENTS) {
            return true;
        }
    }
    return false;
}

void InstrumentService::Finish(OperationResult value)
{
    const auto view = core_.ReadView();
    if (view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED) {
        settings_.sweepMode = SweepMode::HOLD;
    }
    if (!value.ok) {
        if (tasks_.front().owner == "SCPI" || view.state == VnaCore::CoreState::FAULTED) {
            Error(value.errorCode, value.message);
        }
        observedFault_ = view.error;
        VnaLog::Error(VnaLog::Module::SERVICE, "控制操作失败，operation={}，code={}，原因={}",
            TaskName(tasks_.front().kind), value.errorCode, value.message);
    } else {
        VnaLog::Debug(VnaLog::Module::SERVICE, "控制操作完成，operation={}，revision={}", TaskName(tasks_.front().kind),
            settings_.revision);
    }
    tasks_.front().promise.set_value(std::move(value));
    tasks_.pop_front();
}

bool InstrumentService::Consume(Detail::ServiceTask &task)
{
    if (!Ready(task.ticket)) {
        return false;
    }
    const VnaCore::CommandResult value = task.ticket.get();
    task.ticket = VnaCore::Ticket();
    if (!value.ok) {
        Finish(Result(false, Detail::DEVICE_SPECIFIC_ERROR, value.message));
        return false;
    }
    return true;
}

void InstrumentService::CommitMode(Detail::ServiceTask &task)
{
    settings_.triggerSource = task.candidate.triggerSource;
    settings_.sweepMode = task.candidate.sweepMode;
    VnaLog::Info(VnaLog::Module::SERVICE, "扫描模式已提交，mode={}，trigger={}，revision={}",
        ModeName(settings_.sweepMode), settings_.triggerSource == TriggerSource::IMMEDIATE ? "IMMEDIATE" : "MANUAL",
        settings_.revision);
    Finish(Result(true));
}

void InstrumentService::PollTask()
{
    if (tasks_.empty()) {
        return;
    }
    Detail::ServiceTask &task = tasks_.front();
    if (task.stage == Detail::ServiceTaskStage::PENDING &&
        (task.owner != owner_ || task.generation != controlGeneration_)) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Instrument control generation changed"));
        return;
    }
    if (task.kind == Detail::ServiceTaskKind::CONFIGURE || task.kind == Detail::ServiceTaskKind::MEASUREMENTS) {
        PollConfiguration(task);
    } else if (task.kind == Detail::ServiceTaskKind::DISPLAY) {
        PollDisplay(task);
    } else if (task.kind == Detail::ServiceTaskKind::TRIGGER) {
        PollTrigger(task);
    } else {
        PollMode(task);
    }
}

bool InstrumentService::PrepareConfiguration(Detail::ServiceTask &task)
{
    const auto view = core_.ReadView();
    if (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration requires Ready or Running"));
        return false;
    }
    if (view.configRevision != settings_.acquisitionRevision) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Acquisition plan changed outside instrument service"));
        return false;
    }
    if (task.kind == Detail::ServiceTaskKind::CONFIGURE &&
        (settings_.sweepMode != SweepMode::HOLD || view.state != VnaCore::CoreState::READY)) {
        Finish(
            Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration requires HOLD and no pending measurement"));
        return false;
    }
    if (task.checkRevision && task.expectedRevision != settings_.revision) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration revision changed"));
        return false;
    }
    if (settings_.revision == SETTINGS_REVISION_MAX) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration revision exhausted"));
        return false;
    }
    task.candidate = settings_;
    const OperationResult prepared = task.mutate(task.candidate);
    if (!prepared.ok) {
        Finish(prepared);
        return false;
    }
    task.candidate.revision = settings_.revision + 1U;
    task.plan = MakePlan(task.candidate);
    if (!PrepareDisplayConfiguration(task)) {
        return false;
    }
    return true;
}

void InstrumentService::CommitConfiguration(Detail::ServiceTask &task)
{
    task.displayCandidate.BindAcquisitionRevision(task.candidate.acquisitionRevision);
    settings_ = std::move(task.candidate);
    display_ = std::move(task.displayCandidate);
    Finish(Result(true));
}

void InstrumentService::PollConfiguration(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        if (!PrepareConfiguration(task)) {
            return;
        }
        if (task.kind == Detail::ServiceTaskKind::MEASUREMENTS &&
            (task.candidate.measurements.empty() || CoversPlan(activePlan_, task.plan))) {
            CommitConfiguration(task);
            return;
        }
        task.ticket = core_.Configure(task.plan);
        task.stage = Detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    if (!Ready(task.ticket)) {
        return;
    }
    const auto value = task.ticket.get();
    if (!value.ok) {
        Finish(Result(false, Detail::DEVICE_SPECIFIC_ERROR, value.message));
        return;
    }
    task.candidate.acquisitionRevision = value.configRevision;
    activePlan_ = std::move(task.plan);
    CommitConfiguration(task);
}

void InstrumentService::PollTrigger(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        const auto view = core_.ReadView();
        if (settings_.triggerSource != TriggerSource::MANUAL || settings_.sweepMode == SweepMode::HOLD ||
            view.state != VnaCore::CoreState::RUNNING) {
            Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "INIT requires MANual source and an armed channel"));
            return;
        }
        task.ticket = core_.TriggerOnce(view.runId, view.configRevision);
        task.stage = Detail::ServiceTaskStage::WAITING_TRIGGER;
        return;
    }
    if (!Consume(task)) {
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_TRIGGER && settings_.sweepMode == SweepMode::SINGLE) {
        task.ticket = core_.Stop(core_.ReadView().runId);
        task.stage = Detail::ServiceTaskStage::WAITING_STOP;
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_STOP) {
        settings_.sweepMode = SweepMode::HOLD;
    }
    Finish(Result(true));
    return;
}

bool InstrumentService::BeginMode(Detail::ServiceTask &task)
{
    const auto view = core_.ReadView();
    if (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Instrument is not ready"));
        return false;
    }
    task.candidate = settings_;
    if (task.kind == Detail::ServiceTaskKind::MODE) {
        task.candidate.sweepMode = task.mode;
    } else {
        task.candidate.triggerSource = task.source;
    }
    if (task.forceInternalSource) {
        task.candidate.triggerSource = TriggerSource::IMMEDIATE;
    }
    task.measurement =
        task.candidate.sweepMode == SweepMode::SINGLE && task.candidate.triggerSource == TriggerSource::IMMEDIATE;
    if (view.state == VnaCore::CoreState::RUNNING) {
        task.ticket = core_.Stop(view.runId);
        task.stage = Detail::ServiceTaskStage::WAITING_STOP;
        return false;
    }
    task.stage = Detail::ServiceTaskStage::READY_TO_START;

    return true;
}

void InstrumentService::PollMode(Detail::ServiceTask &task)
{
    // 单次扫描使用手动触发，随后经过 Stop 完成屏障。
    if (task.stage == Detail::ServiceTaskStage::PENDING && !BeginMode(task)) {
        return;
    }

    if (task.stage == Detail::ServiceTaskStage::WAITING_STOP) {
        if (!Consume(task)) {
            return;
        }
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
    }
    if (task.stage == Detail::ServiceTaskStage::READY_TO_START) {
        if (task.candidate.sweepMode == SweepMode::HOLD) {
            CommitMode(task);
            return;
        }
        const auto mode = task.candidate.triggerSource == TriggerSource::IMMEDIATE &&
                                  task.candidate.sweepMode == SweepMode::CONTINUOUS
                              ? VnaCore::RunMode::CONTINUOUS
                              : VnaCore::RunMode::MANUAL;
        task.ticket = core_.Start(mode);
        task.stage = Detail::ServiceTaskStage::WAITING_START;
        return;
    }
    PollStartedMode(task);
}

void InstrumentService::PollStartedMode(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::WAITING_START) {
        if (!Consume(task)) {
            return;
        }
        if (task.candidate.sweepMode == SweepMode::SINGLE) {
            settings_.sweepMode = SweepMode::SINGLE;
            settings_.triggerSource = task.candidate.triggerSource;
        }
        if (task.candidate.sweepMode != SweepMode::SINGLE || task.candidate.triggerSource != TriggerSource::IMMEDIATE) {
            CommitMode(task);
            return;
        }
        const auto view = core_.ReadView();
        task.ticket = core_.TriggerOnce(view.runId, view.configRevision);
        task.stage = Detail::ServiceTaskStage::WAITING_SINGLE_FRAME;
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_SINGLE_FRAME) {
        if (!Consume(task)) {
            return;
        }
        task.ticket = core_.Stop(core_.ReadView().runId);
        task.stage = Detail::ServiceTaskStage::WAITING_FINAL_STOP;
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_FINAL_STOP) {
        if (!Consume(task)) {
            return;
        }
        task.candidate.sweepMode = SweepMode::HOLD;
        CommitMode(task);
    }
}

InstrumentService::InstrumentService(VnaCore::AcquisitionCore &core, InstrumentProfile profile)
    : core_(core), profile_(std::move(profile))
{
    ValidateInstrumentProfile(profile_);
    settings_.startHz = profile_.defaultStartHz;
    settings_.stopHz = profile_.defaultStopHz;
    settings_.points = profile_.defaultPoints;
    settings_.ifBandwidthHz = profile_.defaultIfBandwidthHz;
}

InstrumentService::~InstrumentService() = default;

bool InstrumentService::BootstrapMockProfile(std::string &error)
{
    const auto view = core_.ReadView();
    if (mockReady_ || !owner_.empty() || view.state != VnaCore::CoreState::READY ||
        view.deviceIdn.find("VnaPro,NativeSimulator,") != 0U) {
        error = "Mock profile requires an initialized NativeSimulator before connection admission";
        return false;
    }
    Measurement initial;
    initial.name = "Trc1";
    initial.kind = MeasurementKind::S_PARAMETER;
    initial.receiverKind = 'b';
    initial.receiverPort = profile_.portCount >= 2U ? 2U : 1U;
    InstrumentSettings candidate = settings_;
    candidate.measurements.push_back(initial);
    VnaDisplay::DisplayWorkspace initialDisplay;
    const auto reconciled = initialDisplay.Reconcile(candidate.measurements);
    if (!reconciled.ok) {
        error = reconciled.error;
        return false;
    }
    auto plan = MakePlan(candidate);
    const auto configured = core_.Configure(plan).get();
    if (!configured.ok) {
        error = configured.message;
        return false;
    }
    candidate.revision = 1U;
    candidate.acquisitionRevision = configured.configRevision;
    activePlan_ = std::move(plan);
    settings_ = std::move(candidate);
    display_ = std::move(initialDisplay);
    mockReady_ = true;
    VnaLog::Info(
        VnaLog::Module::SERVICE, "模拟设备配置已就绪，revision={}，points={}", settings_.revision, settings_.points);
    return true;
}

void InstrumentService::Poll()
{
    PollTask();
    const auto view = core_.ReadView();
    if (view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED) {
        settings_.sweepMode = SweepMode::HOLD;
    }
    if (tasks_.empty() && view.state == VnaCore::CoreState::FAULTED && view.error != observedFault_) {
        Error(Detail::DEVICE_SPECIFIC_ERROR, view.error);
        observedFault_ = view.error;
    }
}

bool InstrumentService::HasPending() const noexcept
{
    return !tasks_.empty();
}

std::vector<Operation> InstrumentService::PendingOperations() const
{
    std::vector<Operation> operations;
    for (const auto &task : tasks_) {
        operations.push_back(task.operation);
    }
    return operations;
}

bool InstrumentService::TryAcquireControl(const std::string &owner)
{
    if (!owner_.empty() || owner.empty()) {
        return false;
    }
    owner_ = owner;
    releasing_ = false;
    ++controlGeneration_;
    VnaLog::Debug(VnaLog::Module::SERVICE, "仪器控制权已取得，generation={}", controlGeneration_);
    return true;
}

void InstrumentService::BeginReleaseControl(const std::string &owner) noexcept
{
    if (owner_ == owner) {
        releasing_ = true;
    }
}

void InstrumentService::ReleaseControl(const std::string &owner) noexcept
{
    if (owner_ == owner && tasks_.empty()) {
        owner_.clear();
        releasing_ = false;
        ++controlGeneration_;
        VnaLog::Debug(VnaLog::Module::SERVICE, "仪器控制权已释放，generation={}", controlGeneration_);
    }
}

std::string InstrumentService::ControlOwner() const
{
    return owner_;
}

bool InstrumentService::ControlReleasing() const noexcept
{
    return releasing_;
}

InstrumentSnapshot InstrumentService::Snapshot() const
{
    InstrumentSnapshot value;
    value.settings = settings_;
    value.display = display_.Snapshot();
    value.core = core_.ReadView();
    value.controlOwner = owner_;
    value.releasing = releasing_;
    value.pending = !tasks_.empty();
    value.finitePending = HasMeasurement();
    return value;
}

const InstrumentSettings &InstrumentService::Settings() const noexcept
{
    return settings_;
}

const InstrumentProfile &InstrumentService::Profile() const noexcept
{
    return profile_;
}

std::string InstrumentService::Identity() const
{
    return mockReady_ ? "VnaPro,NativeSimulator," + std::to_string(profile_.portCount) + "PORT,1.0"
                      : "VnaPro,UnverifiedBackend,0,1.0";
}

Operation InstrumentService::SetStartHz(double value, std::uint64_t expected)
{
    return Configure(
        [this, value](InstrumentSettings &settings) {
            if (!ValidFrequency(value, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency out of mock range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double start = std::round(value);
            const double stop = start > settings.stopHz ? start + span : settings.stopHz;
            if (!ValidFrequency(stop, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Coupled stop frequency out of mock range");
            }
            settings.startHz = start;
            settings.stopHz = stop;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetStopHz(double value, std::uint64_t expected)
{
    return Configure(
        [this, value](InstrumentSettings &settings) {
            if (!ValidFrequency(value, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Stop frequency out of mock range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double stop = std::round(value);
            const double start = stop < settings.startHz ? stop - span : settings.startHz;
            if (!ValidFrequency(start, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Coupled start frequency out of mock range");
            }
            settings.startHz = start;
            settings.stopHz = stop;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetPoints(unsigned value, std::uint64_t expected)
{
    return Configure(
        [value](InstrumentSettings &settings) {
            if (value < MIN_SWEEP_POINTS || value > MAX_SWEEP_POINTS) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001 mock range");
            }
            settings.points = value;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetIfBandwidthHz(double value, std::uint64_t expected)
{
    return Configure(
        [value](InstrumentSettings &settings) {
            if (!std::isfinite(value) || value <= 0.0) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of range");
            }
            const double quantized = QuantizedBandwidth(value);
            if (quantized == 0.0) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of mock range");
            }
            settings.ifBandwidthHz = quantized;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::ApplyConfiguration(ConfigPatch patch, std::uint64_t expected, const std::string &owner)
{
    return Configure(
        [this, patch](InstrumentSettings &settings) {
            return ApplySettingsPatch(patch, settings, profile_);
        },
        expected, owner);
}

Operation InstrumentService::DefineMeasurement(
    Measurement measurement, std::uint64_t expected, const std::string &owner)
{
    measurement.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(measurement.receiverKind)));
    return ConfigureMeasurements(
        [this, measurement](InstrumentSettings &settings) {
            if (!ValidMeasurement(measurement, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Unsupported measurement definition");
            }
            if (settings.measurements.size() >= MEASUREMENT_CAPACITY) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Measurement catalog is full");
            }
            for (const auto &existing : settings.measurements) {
                if (existing.name == measurement.name) {
                    return Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Measurement name already exists");
                }
            }
            settings.measurements.push_back(measurement);
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::UpdateMeasurement(
    Measurement measurement, std::uint64_t expected, const std::string &owner)
{
    measurement.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(measurement.receiverKind)));
    return ConfigureMeasurements(
        [this, measurement](InstrumentSettings &settings) {
            if (!ValidMeasurement(measurement, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Unsupported measurement definition");
            }
            const auto found = std::find_if(settings.measurements.begin(), settings.measurements.end(),
                [&measurement](const Measurement &existing) {
                    return existing.name == measurement.name;
                });
            if (found == settings.measurements.end()) {
                return Result(false, Detail::INVALID_PARAMETER_ERROR, "Measurement name does not exist");
            }
            *found = measurement;
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::DeleteMeasurement(
    const std::string &name, std::uint64_t expected, const std::string &owner)
{
    return ConfigureMeasurements(
        [name](InstrumentSettings &settings) {
            const auto found = std::find_if(
                settings.measurements.begin(), settings.measurements.end(), [&name](const Measurement &measurement) {
                    return measurement.name == name;
                });
            if (found == settings.measurements.end()) {
                return Result(false, Detail::INVALID_PARAMETER_ERROR, "Measurement name does not exist");
            }
            settings.measurements.erase(found);
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::SetTriggerSource(TriggerSource source, const std::string &owner)
{
    if (source != TriggerSource::IMMEDIATE && source != TriggerSource::MANUAL) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Invalid trigger source", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::SOURCE;
    task.source = source;
    task.measurement = source == TriggerSource::IMMEDIATE && settings_.sweepMode == SweepMode::SINGLE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SetSweepMode(SweepMode mode, const std::string &owner)
{
    if (mode != SweepMode::HOLD && mode != SweepMode::SINGLE && mode != SweepMode::CONTINUOUS) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Invalid sweep mode", owner);
    }
    if (mode == SweepMode::SINGLE && HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MODE;
    task.mode = mode;
    task.measurement = mode == SweepMode::SINGLE && settings_.triggerSource == TriggerSource::IMMEDIATE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::Trigger(const std::string &owner)
{
    if (HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Busy INIT restart is not supported", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::TRIGGER;
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunSingle(const std::string &owner)
{
    if (HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MODE;
    task.mode = SweepMode::SINGLE;
    task.forceInternalSource = true;
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunContinuous(const std::string &owner)
{
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MODE;
    task.mode = SweepMode::CONTINUOUS;
    task.forceInternalSource = true;
    return Enqueue(std::move(task), owner);
}

bool InstrumentService::SelectMeasurement(const std::string &name)
{
    if (owner_ != "SCPI" || releasing_ || HasPending()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Measurement selection requires control and no pending configuration");
        return false;
    }
    VnaDisplay::DisplayChange select;
    select.name = name;
    const auto result = display_.Apply(select, settings_.measurements, DisplayFrame());
    if (!result.ok) {
        Error(Detail::INVALID_PARAMETER_ERROR, result.error);
    }
    return result.ok;
}

bool InstrumentService::SetCurrentTriggerScope(bool current)
{
    if (owner_ != "SCPI" || releasing_ || HasConfiguration()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Trigger scope requires control and no pending configuration");
        return false;
    }
    settings_.currentTriggerScope = current;
    return true;
}

DataResult InstrumentService::StimulusAxis() const
{
    DataResult value;
    const auto view = core_.ReadView();
    if (!mockReady_ || view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED ||
        view.configRevision != settings_.acquisitionRevision) {
        value.error = "No valid committed stimulus configuration";
        return value;
    }
    value.values = MakePlan(settings_).frequencyHz;
    value.ok = true;
    return value;
}

DataResult InstrumentService::MeasurementData() const
{
    DataResult value;
    if (HasMeasurement()) {
        value.error = "Finite measurement has not completed";
        return value;
    }
    const std::string selected = display_.State().selectedMeasurement;
    const auto found = std::find_if(
        settings_.measurements.begin(), settings_.measurements.end(), [&selected](const Measurement &measurement) {
            return measurement.name == selected;
        });
    if (found == settings_.measurements.end()) {
        value.error = "No measurement selected";
        return value;
    }
    const Measurement measurement = *found;
    const auto view = core_.ReadView();
    if (!view.frame || view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED ||
        view.frame->configRevision != settings_.acquisitionRevision ||
        view.configRevision != settings_.acquisitionRevision || view.frame->runId != view.runId) {
        value.error = "No complete measurement for the current configuration";
        return value;
    }
    return ExtractMeasurement(*view.frame, measurement);
}

void InstrumentService::RecordError(int code, const std::string &message)
{
    Error(code, message);
}

InstrumentError InstrumentService::PopError()
{
    if (errors_.empty()) {
        return InstrumentError();
    }
    InstrumentError value = errors_.front();
    errors_.pop_front();
    return value;
}

unsigned InstrumentService::ErrorCount() const noexcept
{
    return static_cast<unsigned>(errors_.size());
}

void InstrumentService::ClearStatus() noexcept
{
    errors_.clear();
    esr_ = 0;
    ++generation_;
}

unsigned InstrumentService::StatusGeneration() const noexcept
{
    return generation_;
}

void InstrumentService::SetEventEnable(unsigned value) noexcept
{
    ese_ = value & EVENT_ENABLE_MASK;
}

unsigned InstrumentService::EventEnable() const noexcept
{
    return ese_;
}

void InstrumentService::SetServiceRequestEnable(unsigned value) noexcept
{
    sre_ = value & SERVICE_REQUEST_ENABLE_MASK;
}

unsigned InstrumentService::ServiceRequestEnable() const noexcept
{
    return sre_;
}

unsigned InstrumentService::ReadEventRegister() noexcept
{
    const unsigned value = esr_;
    esr_ = 0;
    return value;
}

void InstrumentService::MarkOperationComplete() noexcept
{
    esr_ |= OPERATION_COMPLETE_EVENT;
}

unsigned InstrumentService::StatusByte(bool outputAvailable) const noexcept
{
    unsigned value = errors_.empty() ? 0U : ERROR_QUEUE_STATUS;
    if (outputAvailable) {
        value |= MESSAGE_AVAILABLE_STATUS;
    }
    if ((esr_ & ese_) != 0U) {
        value |= EVENT_SUMMARY_STATUS;
    }
    if ((value & sre_) != 0U) {
        value |= SERVICE_REQUEST_STATUS;
    }
    return value;
}

}  // 命名空间 VnaService
