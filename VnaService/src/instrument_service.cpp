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
#include <memory>
#include <set>
#include <utility>

#include "instrument_configuration.h"
#include "vna_core/acquisition_core.h"
#include "vna_core/frame_layout.h"
#include "vna_log/logger.h"
#include "vna_measurement/measurement_processor.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
Detail::ServiceTask::ServiceTask() : operation(promise.get_future().share())
{
}

namespace {
constexpr std::size_t TASK_QUEUE_CAPACITY = 32U;
constexpr std::size_t ERROR_QUEUE_CAPACITY = 32U;
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
        case Detail::ServiceTaskKind::CHANNEL_CREATE:
            return "CHANNEL_CREATE";
        case Detail::ServiceTaskKind::CHANNEL_SELECT:
            return "CHANNEL_SELECT";
        case Detail::ServiceTaskKind::CHANNEL_DELETE:
            return "CHANNEL_DELETE";
        case Detail::ServiceTaskKind::CONFIGURE:
            return "CONFIGURE";
        case Detail::ServiceTaskKind::MEASUREMENTS:
            return "MEASUREMENTS";
        case Detail::ServiceTaskKind::RESET:
            return "RESET";
        case Detail::ServiceTaskKind::DISPLAY:
            return "DISPLAY";
        case Detail::ServiceTaskKind::STIMULUS_ZOOM:
            return "STIMULUS_ZOOM";
        case Detail::ServiceTaskKind::SOURCE:
            return "SOURCE";
        case Detail::ServiceTaskKind::MODE:
            return "MODE";
        case Detail::ServiceTaskKind::SELECT_SOURCE:
            return "SELECT_SOURCE";
        case Detail::ServiceTaskKind::SELECT_MODE:
            return "SELECT_MODE";
        case Detail::ServiceTaskKind::START:
            return "START";
        case Detail::ServiceTaskKind::TRIGGER:
            return "TRIGGER";
        case Detail::ServiceTaskKind::CALIBRATION:
            return "CALIBRATION";
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

bool UnavailableState(VnaCore::CoreState state)
{
    return state == VnaCore::CoreState::FAULTED || state == VnaCore::CoreState::CLOSED;
}

using Detail::ApplySettingsPatch;
using Detail::CoversPlan;
using Detail::MakePlan;
using Detail::QuantizedBandwidth;
using Detail::ValidFrequency;

DataResult ExtractMeasurements(const VnaCore::MeasurementFrame &frame, const std::vector<Measurement> &measurements,
    const VnaCalibration::CalibrationData *calibration)
{
    DataResult value;
    const auto traces = VnaMeasurement::MeasurementProcessor::CalculateMany(frame, measurements, calibration);
    std::size_t componentCount = 0U;
    for (const auto &trace : traces) {
        if (!trace.available) {
            value.error = trace.error;
            return value;
        }
        for (const auto &point : trace.points) {
            if (!point.valid) {
                value.error = "Measurement frame contains invalid points";
                return value;
            }
        }
        componentCount += trace.points.size() * VnaCore::COMPLEX_COMPONENT_COUNT;
    }
    value.values.reserve(componentCount);
    for (const auto &trace : traces) {
        for (const auto &point : trace.points) {
            value.values.push_back(point.real);
            value.values.push_back(point.imag);
        }
    }
    value.ok = true;
    return value;
}

}  // 匿名命名空间

VnaCore::SweepPlan InstrumentService::MakeChannelPlan(const InstrumentSettings &settings, unsigned channelId) const
{
    return MakePlan(settings, profile_, channelId);
}

bool InstrumentService::ValidMeasurement(const Measurement &measurement, const InstrumentProfile &profile) noexcept
{
    return !measurement.name.empty() && measurement.name.size() <= MAX_MEASUREMENT_NAME_LENGTH &&
           measurement.IsSupported() && measurement.sourcePort <= profile.portCount &&
           measurement.receiverPort <= profile.portCount;
}

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
    if (task.channelId == 0U) {
        task.channelId = contextChannelId_;
    }
    if (owner_ != requester || requester.empty() || releasing_) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "No active instrument control", requester);
    }
    if (!profileReady_) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Verified backend profile is not initialized", requester);
    }
    if (tasks_.size() >= TASK_QUEUE_CAPACITY) {
        return Rejected(Detail::INPUT_BUFFER_OVERRUN, "Control input buffer overrun", requester);
    }
    if (task.kind == Detail::ServiceTaskKind::CALIBRATION &&
        task.calibrationChange.kind == CalibrationChangeKind::IMPORT_SET && task.calibrationChange.importedSet) {
        CalibrationSet snapshot = *task.calibrationChange.importedSet;
        if (snapshot.data) {
            snapshot.data = std::make_shared<const VnaCalibration::CalibrationData>(*snapshot.data);
        }
        // 与配置值一样冻结受理时的输入，调用者保留的可变别名不能改变排队请求。
        task.calibrationChange.importedSet = std::make_shared<const CalibrationSet>(std::move(snapshot));
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
    const auto state = core_.ReadView(contextChannelId_).state;
    if (state != VnaCore::CoreState::READY && state != VnaCore::CoreState::RUNNING) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Configuration requires Ready or Running", requester);
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
    const auto state = core_.ReadView(contextChannelId_).state;
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
        if (task.channelId == contextChannelId_ && task.measurement) {
            return true;
        }
    }
    return false;
}

bool InstrumentService::HasConfiguration() const noexcept
{
    for (const auto &task : tasks_) {
        if (task.channelId == contextChannelId_ &&
            (task.kind == Detail::ServiceTaskKind::CONFIGURE || task.kind == Detail::ServiceTaskKind::MEASUREMENTS ||
                task.kind == Detail::ServiceTaskKind::STIMULUS_ZOOM || task.kind == Detail::ServiceTaskKind::RESET)) {
            return true;
        }
    }
    return false;
}

void InstrumentService::Finish(OperationResult value)
{
    const auto view = core_.ReadView(contextChannelId_);
    if (view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED) {
        settings_.sweepMode = SweepMode::HOLD;
        if (value.ok && tasks_.front().calibrationResuming) {
            value = Result(false, Detail::DEVICE_SPECIFIC_ERROR,
                view.error.empty() ? "Acquisition restoration failed before operation completion" : view.error);
        }
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
    settings_.selectedSweepMode = task.candidate.selectedSweepMode;
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
    ContextScope channelScope(*this, task.channelId, {}, true);
    if (!channelScope.Valid()) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Target channel no longer exists"));
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::PENDING &&
        (task.owner != owner_ || task.generation != controlGeneration_)) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Instrument control generation changed"));
        return;
    }
    if (task.kind == Detail::ServiceTaskKind::CHANNEL_CREATE || task.kind == Detail::ServiceTaskKind::CHANNEL_SELECT ||
        task.kind == Detail::ServiceTaskKind::CHANNEL_DELETE) {
        PollChannel(task);
    } else if (task.kind == Detail::ServiceTaskKind::CONFIGURE || task.kind == Detail::ServiceTaskKind::MEASUREMENTS) {
        PollConfiguration(task);
    } else if (task.kind == Detail::ServiceTaskKind::RESET) {
        PollReset(task);
    } else if (task.kind == Detail::ServiceTaskKind::DISPLAY) {
        PollDisplay(task);
    } else if (task.kind == Detail::ServiceTaskKind::STIMULUS_ZOOM) {
        PollStimulusZoom(task);
    } else if (task.kind == Detail::ServiceTaskKind::TRIGGER) {
        PollTrigger(task);
    } else if (task.kind == Detail::ServiceTaskKind::CALIBRATION) {
        PollCalibration(task);
    } else {
        PollMode(task);
    }
}

bool InstrumentService::PrepareConfiguration(Detail::ServiceTask &task)
{
    if (CalibrationCollecting()) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration is locked during calibration collection"));
        return false;
    }
    const auto view = core_.ReadView(contextChannelId_);
    if (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration requires Ready or Running"));
        return false;
    }
    if (view.configRevision != settings_.acquisitionRevision) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Acquisition plan changed outside instrument service"));
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
    task.channelTraceNames.clear();
    for (const auto &measurement : task.candidate.measurements) {
        if (measurement.channelId == contextChannelId_) {
            task.channelTraceNames.push_back(measurement.name);
        }
    }
    task.plan = MakePlan(task.candidate, profile_, contextChannelId_);
    calibration_.ExtendPlan(task.plan);
    task.calibrationCandidate = calibration_;
    task.calibrationCandidate.PrepareForPlan(Identity(), task.plan);
    if (!PrepareDisplayConfiguration(task)) {
        return false;
    }
    return true;
}

void InstrumentService::CommitConfiguration(Detail::ServiceTask &task)
{
    const bool activeTarget = ActiveChannelId() == contextChannelId_;
    InvalidateZoomHistory(task);
    task.displayCandidate.BindAcquisitionRevision(task.candidate.acquisitionRevision, &task.channelTraceNames);
    settings_ = std::move(task.candidate);
    calibration_ = std::move(task.calibrationCandidate);
    CommitLogicalRevision(settings_.revision);
    display_ = std::move(task.displayCandidate);
    if (activeTarget && ChannelTraceNames(contextChannelId_).empty()) {
        auto state = display_.Snapshot();
        state.selectedMeasurement.clear();
        display_ = VnaDisplay::DisplayWorkspace(std::move(state));
        selectedChannelId_ = contextChannelId_;
    } else {
        RememberSelection();
    }
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
        // 核心工作线程在完整帧边界替换计划；运行身份、扫描模式和触发源保持不变。
        task.ticket = core_.Configure(task.plan, contextChannelId_);
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

void InstrumentService::PollReset(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        if (!PrepareConfiguration(task)) {
            return;
        }
        const auto view = core_.ReadView(contextChannelId_);
        if (view.state == VnaCore::CoreState::RUNNING) {
            task.ticket = core_.Stop(view.runId, contextChannelId_);
            task.stage = Detail::ServiceTaskStage::WAITING_STOP;
            return;
        }
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_STOP) {
        if (!Consume(task)) {
            return;
        }
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
    }
    if (task.stage == Detail::ServiceTaskStage::READY_TO_START) {
        task.ticket = core_.Configure(task.plan, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    if (!Ready(task.ticket)) {
        return;
    }
    const auto configured = task.ticket.get();
    if (!configured.ok) {
        settings_.sweepMode = SweepMode::HOLD;
        Finish(Result(false, Detail::DEVICE_SPECIFIC_ERROR, configured.message));
        return;
    }
    task.candidate.acquisitionRevision = configured.configRevision;
    activePlan_ = std::move(task.plan);
    CommitConfiguration(task);
}

void InstrumentService::PollTrigger(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        if (CalibrationCollecting()) {
            Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "INIT is locked during calibration collection"));
            return;
        }
        const auto view = core_.ReadView(contextChannelId_);
        if (settings_.triggerSource != TriggerSource::MANUAL || settings_.sweepMode == SweepMode::HOLD ||
            view.state != VnaCore::CoreState::RUNNING) {
            Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "INIT requires MANual source and an armed channel"));
            return;
        }
        task.ticket = core_.TriggerOnce(view.runId, view.configRevision, contextChannelId_);
        task.sweepRunId = view.runId;
        task.sweepConfigRevision = view.configRevision;
        task.stage = Detail::ServiceTaskStage::WAITING_TRIGGER;
        return;
    }
    if (!Consume(task)) {
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_TRIGGER && settings_.sweepMode == SweepMode::SINGLE) {
        task.ticket = core_.Stop(core_.ReadView(contextChannelId_).runId, contextChannelId_);
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
    if (CalibrationCollecting() && (task.kind != Detail::ServiceTaskKind::MODE || task.mode != SweepMode::HOLD)) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "RUN is locked during calibration collection"));
        return false;
    }
    const auto view = core_.ReadView(contextChannelId_);
    if (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) {
        Finish(Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Instrument is not ready"));
        return false;
    }
    task.candidate = settings_;
    if (task.kind == Detail::ServiceTaskKind::MODE) {
        task.candidate.sweepMode = task.mode;
        if (task.mode != SweepMode::HOLD) {
            task.candidate.selectedSweepMode = task.mode;
        }
    } else if (task.kind == Detail::ServiceTaskKind::START) {
        task.candidate.sweepMode = settings_.selectedSweepMode;
    } else if (task.kind == Detail::ServiceTaskKind::SELECT_MODE ||
               task.kind == Detail::ServiceTaskKind::SELECT_SOURCE) {
        const bool unchanged = task.kind == Detail::ServiceTaskKind::SELECT_MODE
                                   ? settings_.selectedSweepMode == task.mode
                                   : settings_.triggerSource == task.source;
        if (unchanged && !task.forceHold) {
            Finish(Result(true));
            return false;
        }
        if (task.kind == Detail::ServiceTaskKind::SELECT_MODE) {
            task.candidate.sweepMode = task.mode == SweepMode::CONTINUOUS ? SweepMode::CONTINUOUS : SweepMode::HOLD;
            task.candidate.selectedSweepMode = task.mode;
        } else {
            // 切源沿用执行时的运行或布防方式；停止态仅修改选项，不隐式启动。
            task.candidate.triggerSource = task.source;
        }
    } else {
        task.candidate.triggerSource = task.source;
    }
    if (task.forceInternalSource) {
        task.candidate.triggerSource = TriggerSource::IMMEDIATE;
    }
    task.measurement =
        task.candidate.sweepMode == SweepMode::SINGLE && task.candidate.triggerSource == TriggerSource::IMMEDIATE;
    if (view.state == VnaCore::CoreState::RUNNING) {
        task.ticket = core_.Stop(view.runId, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_STOP;
        return false;
    }
    task.stage = Detail::ServiceTaskStage::READY_TO_START;

    return true;
}

void InstrumentService::PollMode(Detail::ServiceTask &task)
{
    if (!ConsumeEarlySweepStop(task)) {
        return;
    }
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
        task.ticket = core_.Start(mode, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_START;
        return;
    }
    PollStartedMode(task);
}

void InstrumentService::PollModeStart(Detail::ServiceTask &task)
{
    if (!Consume(task)) {
        return;
    }
    // 连续首帧可能在服务消费 Start 票据前失败，不能提交已故障的候选状态。
    const auto view = core_.ReadView(contextChannelId_);
    if (UnavailableState(view.state)) {
        Finish(Result(false, Detail::DEVICE_SPECIFIC_ERROR,
            view.error.empty() ? "Acquisition failed before start completion" : view.error));
        return;
    }
    if (task.candidate.sweepMode == SweepMode::SINGLE) {
        settings_.selectedSweepMode = task.candidate.selectedSweepMode;
        settings_.sweepMode = SweepMode::SINGLE;
        settings_.triggerSource = task.candidate.triggerSource;
    }
    if (task.candidate.sweepMode != SweepMode::SINGLE || task.candidate.triggerSource != TriggerSource::IMMEDIATE) {
        CommitMode(task);
        return;
    }
    task.ticket = core_.TriggerOnce(view.runId, view.configRevision, contextChannelId_);
    task.sweepRunId = view.runId;
    task.sweepConfigRevision = view.configRevision;
    task.stage = Detail::ServiceTaskStage::WAITING_SINGLE_FRAME;
}

void InstrumentService::PollStartedMode(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::WAITING_START) {
        PollModeStart(task);
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_SINGLE_FRAME) {
        if (!Consume(task)) {
            return;
        }
        task.ticket = core_.Stop(core_.ReadView(contextChannelId_).runId, contextChannelId_);
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

InstrumentService::InstrumentService(VnaCore::AcquisitionCore &core, InstrumentProfile profile, bool simulator)
    : core_(core), profile_(std::move(profile)), simulator_(simulator)
{
    ValidateInstrumentProfile(profile_);
    settings_.startHz = profile_.defaultStartHz;
    settings_.stopHz = profile_.defaultStopHz;
    settings_.points = profile_.defaultPoints;
    settings_.ifBandwidthHz = profile_.defaultIfBandwidthHz;
    settings_.powerDbm = profile_.defaultPowerDbm;
    channels_.emplace(1U, ChannelState{});
}

InstrumentService::~InstrumentService() = default;

bool InstrumentService::BootstrapProfile(std::string &error)
{
    const auto view = core_.ReadView(contextChannelId_);
    if (profileReady_ || !owner_.empty() || view.state != VnaCore::CoreState::READY) {
        error = "Instrument profile requires a ready core without a controller and may only be initialized once";
        return false;
    }
    Measurement initial;
    initial.name = "Trc1";
    initial.channelId = 1U;
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
    auto plan = MakePlan(candidate, profile_, 1U);
    const auto configured = core_.Configure(plan, contextChannelId_).get();
    if (!configured.ok) {
        error = configured.message;
        return false;
    }
    candidate.revision = 1U;
    candidate.acquisitionRevision = configured.configRevision;
    activePlan_ = std::move(plan);
    settings_ = std::move(candidate);
    CommitLogicalRevision(settings_.revision);
    display_ = std::move(initialDisplay);
    RememberSelection();
    profileReady_ = true;
    calibration_.PrepareForPlan(Identity(), activePlan_);
    VnaLog::Info(
        VnaLog::Module::SERVICE, "仪器配置已就绪，revision={}，points={}", settings_.revision, settings_.points);
    return true;
}

void InstrumentService::Poll()
{
    WakeAcceptedStop();
    PollTask();
    for (auto &channel : channels_) {
        if (!channel.second.committed) {
            continue;
        }
        const auto channelView = core_.ReadView(channel.first);
        if (UnavailableState(channelView.state)) {
            if (channel.first == contextChannelId_) {
                settings_.sweepMode = SweepMode::HOLD;
            } else {
                channel.second.settings.sweepMode = SweepMode::HOLD;
            }
        }
    }
    const auto view = core_.ReadView(contextChannelId_);
    if (UnavailableState(view.state)) {
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

InstrumentSnapshot InstrumentService::Snapshot(unsigned channelId) const
{
    const auto targetId = channelId == 0U ? ActiveChannelId() : channelId;
    const auto target = ChannelSnapshot(targetId);
    InstrumentSnapshot value;
    value.activeChannelId = ActiveChannelId();
    for (const auto &channel : channels_) {
        if (channel.second.committed) {
            value.channels.push_back(ChannelSnapshot(channel.first));
        }
    }
    value.settings = target.settings;
    value.settings.measurements = settings_.measurements;
    value.display = display_.Snapshot();
    value.core = target.core;
    value.calibration = target.calibration;
    value.correction = target.correction;
    value.stimulusZoom = target.stimulusZoom;
    value.controlOwner = owner_;
    value.releasing = releasing_;
    value.pending = !tasks_.empty();
    value.finitePending = target.finitePending;
    return value;
}

const InstrumentSettings &InstrumentService::Settings(unsigned channelId) const
{
    const unsigned id = channelId == 0U ? (contextDepth_ == 0U ? ActiveChannelId() : contextChannelId_) : channelId;
    auto &view = settingsViews_[id];
    view = ChannelSnapshot(id).settings;
    view.measurements = settings_.measurements;
    return view;
}

const InstrumentProfile &InstrumentService::Profile() const noexcept
{
    return profile_;
}

std::string InstrumentService::Identity() const
{
    if (!profileReady_) {
        return "VnaPro,UnverifiedBackend,0,1.0";
    }
    return simulator_ ? "VnaPro,NativeSimulator," + std::to_string(profile_.portCount) + "PORT,1.0"
                      : core_.ReadView(contextChannelId_).deviceIdn;
}

bool InstrumentService::IsSimulator() const noexcept
{
    return simulator_;
}

Operation InstrumentService::SetStartHz(double value, std::uint64_t expected, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", "SCPI");
    }
    return Configure(
        [this, value](InstrumentSettings &settings) {
            if (!ValidFrequency(value, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency out of instrument range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double start = std::round(value);
            const double stop = start > settings.stopHz ? start + span : settings.stopHz;
            if (!ValidFrequency(stop, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Coupled stop frequency out of instrument range");
            }
            settings.startHz = start;
            settings.stopHz = stop;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetStopHz(double value, std::uint64_t expected, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", "SCPI");
    }
    return Configure(
        [this, value](InstrumentSettings &settings) {
            if (!ValidFrequency(value, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Stop frequency out of instrument range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double stop = std::round(value);
            const double start = stop < settings.startHz ? stop - span : settings.startHz;
            if (!ValidFrequency(start, profile_)) {
                return Result(
                    false, Detail::DATA_OUT_OF_RANGE_ERROR, "Coupled start frequency out of instrument range");
            }
            settings.startHz = start;
            settings.stopHz = stop;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetPoints(unsigned value, std::uint64_t expected, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", "SCPI");
    }
    return Configure(
        [value](InstrumentSettings &settings) {
            if (value < MIN_SWEEP_POINTS || value > MAX_SWEEP_POINTS) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001 instrument range");
            }
            settings.points = value;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::SetIfBandwidthHz(double value, std::uint64_t expected, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", "SCPI");
    }
    return Configure(
        [value](InstrumentSettings &settings) {
            if (!std::isfinite(value) || value <= 0.0) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of range");
            }
            const double quantized = QuantizedBandwidth(value);
            if (quantized == 0.0) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of instrument range");
            }
            settings.ifBandwidthHz = quantized;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::ApplyConfiguration(
    ConfigPatch patch, std::uint64_t expected, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    return Configure(
        [this, patch](InstrumentSettings &settings) {
            return ApplySettingsPatch(
                patch, settings, profile_, core_.ReadView(contextChannelId_).manualSweepTimeSupported);
        },
        expected, owner);
}

Operation InstrumentService::SetPowerDbm(double value, const std::string &owner, unsigned channelId)
{
    ContextScope scope(*this, channelId);
    if (!scope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel", owner);
    }
    ConfigPatch patch;
    patch.hasPowerDbm = true;
    patch.powerDbm = value;
    return ApplyConfiguration(patch, settings_.revision, owner, contextChannelId_);
}

Operation InstrumentService::DefineMeasurement(
    Measurement measurement, std::uint64_t expected, const std::string &owner, unsigned channelId)
{
    if (channelId == 0U && measurement.channelId != 0U) {
        channelId = measurement.channelId;
    }
    ContextScope channelScope(*this, channelId, measurement.name);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    measurement.channelId = contextChannelId_;
    measurement.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(measurement.receiverKind)));
    return ConfigureMeasurements(
        [this, measurement](InstrumentSettings &settings) {
            if (!ValidMeasurement(measurement, profile_)) {
                return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Unsupported measurement definition");
            }
            if (settings.measurements.size() >= VnaDisplay::MAX_DISPLAY_TRACES) {
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
    Measurement measurement, std::uint64_t expected, const std::string &owner, unsigned channelId)
{
    if (channelId == 0U && measurement.channelId != 0U) {
        channelId = measurement.channelId;
    }
    ContextScope channelScope(*this, channelId, measurement.name);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    measurement.channelId = contextChannelId_;
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
    const std::string &name, std::uint64_t expected, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId, name);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    return ConfigureMeasurements(
        [name, owner](InstrumentSettings &settings) {
            const auto found = std::find_if(
                settings.measurements.begin(), settings.measurements.end(), [&name](const Measurement &measurement) {
                    return measurement.name == name;
                });
            if (found == settings.measurements.end()) {
                return Result(false, Detail::INVALID_PARAMETER_ERROR, "Measurement name does not exist");
            }
            if (owner != "SCPI" && settings.measurements.size() <= 1U) {
                return Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Cannot delete the last measurement");
            }
            settings.measurements.erase(found);
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::DeleteAllMeasurements(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel", owner);
    }
    const unsigned target = contextChannelId_;
    return ConfigureMeasurements(
        [target](InstrumentSettings &settings) {
            settings.measurements.erase(std::remove_if(settings.measurements.begin(), settings.measurements.end(),
                                            [target](const Measurement &measurement) {
                                                return measurement.channelId == target;
                                            }),
                settings.measurements.end());
            return Result(true);
        },
        owner == "SCPI" ? 0U : settings_.revision, owner);
}

Operation InstrumentService::ResetMeasurements(
    const std::string &owner, unsigned channelId, bool resetInstrumentDisplay)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel", owner);
    }
    const unsigned target = contextChannelId_;
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::RESET;
    task.resetInstrumentDisplay = resetInstrumentDisplay;
    task.mutate = [this, target](InstrumentSettings &settings) {
        settings.measurements.erase(std::remove_if(settings.measurements.begin(), settings.measurements.end(),
                                        [target](const Measurement &measurement) {
                                            return measurement.channelId == target;
                                        }),
            settings.measurements.end());
        Measurement initial;
        for (std::size_t index = 1U; index <= settings.measurements.size() + 1U; ++index) {
            initial.name = "Trc" + std::to_string(index);
            if (std::none_of(settings.measurements.begin(), settings.measurements.end(),
                    [&initial](const Measurement &existing) {
                        return existing.name == initial.name;
                    })) {
                break;
            }
        }
        initial.channelId = target;
        initial.kind = MeasurementKind::S_PARAMETER;
        initial.receiverKind = 'b';
        initial.receiverPort = profile_.portCount >= 2U ? 2U : 1U;
        settings.measurements.push_back(std::move(initial));
        settings.startHz = profile_.defaultStartHz;
        settings.stopHz = profile_.defaultStopHz;
        settings.points = profile_.defaultPoints;
        settings.ifBandwidthHz = profile_.defaultIfBandwidthHz;
        settings.powerDbm = profile_.defaultPowerDbm;
        settings.sweepTimeAuto = true;
        settings.sweepTimeSeconds = 1.0;
        settings.triggerSource = TriggerSource::IMMEDIATE;
        settings.sweepMode = SweepMode::HOLD;
        settings.selectedSweepMode = SweepMode::CONTINUOUS;
        settings.currentTriggerScope = false;
        return Result(true);
    };
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SetTriggerSource(TriggerSource source, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (source != TriggerSource::IMMEDIATE && source != TriggerSource::MANUAL) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Invalid trigger source", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::SOURCE;
    task.source = source;
    task.measurement = source == TriggerSource::IMMEDIATE && settings_.sweepMode == SweepMode::SINGLE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SetSweepMode(SweepMode mode, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (mode != SweepMode::HOLD && mode != SweepMode::SINGLE && mode != SweepMode::CONTINUOUS) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Invalid sweep mode", owner);
    }
    if (mode == SweepMode::SINGLE && HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MODE;
    task.mode = mode;
    task.wakeManualSweep = mode == SweepMode::HOLD;
    task.measurement = mode == SweepMode::SINGLE && settings_.triggerSource == TriggerSource::IMMEDIATE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::Trigger(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Busy INIT restart is not supported", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::TRIGGER;
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunSingle(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
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

Operation InstrumentService::RunContinuous(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::MODE;
    task.mode = SweepMode::CONTINUOUS;
    task.forceInternalSource = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SelectSweepMode(SweepMode mode, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (mode != SweepMode::SINGLE && mode != SweepMode::CONTINUOUS) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Selected sweep mode must be Continuous or Single", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::SELECT_MODE;
    task.mode = mode;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SelectSingleAndHold(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::SELECT_MODE;
    task.mode = SweepMode::SINGLE;
    task.forceHold = true;
    task.wakeManualSweep = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SelectTriggerSource(TriggerSource source, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (source != TriggerSource::IMMEDIATE && source != TriggerSource::MANUAL) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Invalid trigger source", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::SELECT_SOURCE;
    task.source = source;
    // FIFO 前序操作可能布防 Single；执行前先保护切到 Free Run 所产生的有限采集。
    task.measurement = source == TriggerSource::IMMEDIATE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::StartSweep(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::START;
    // 执行时才读取 FIFO 前序操作提交的模式；受理期间先保护有限采集不重叠。
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunSelectedSingle(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    return SetSweepMode(SweepMode::SINGLE, owner);
}

Operation InstrumentService::StopSweep(const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    return SetSweepMode(SweepMode::HOLD, owner);
}

bool InstrumentService::SelectMeasurement(const std::string &name, unsigned channelId)
{
    ContextScope channelScope(*this, channelId, name);
    if (!channelScope.Valid()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch");
        return false;
    }
    if (CalibrationCollecting() && MeasurementChannel(name) != ActiveChannelId()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Channel selection is locked during calibration collection");
        return false;
    }
    if (owner_ != "SCPI" || releasing_ || HasPending()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Measurement selection requires control and no pending configuration");
        return false;
    }
    VnaDisplay::DisplayChange select;
    select.kind = VnaDisplay::DisplayChangeKind::MEASUREMENT_SELECT;
    select.name = name;
    const auto result = display_.Apply(select, settings_.measurements, DisplayFrame());
    if (result.ok) {
        RememberSelection();
    }
    if (!result.ok) {
        Error(Detail::INVALID_PARAMETER_ERROR, result.error);
    }
    return result.ok;
}

bool InstrumentService::SetCurrentTriggerScope(bool current, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch");
        return false;
    }
    if (owner_ != "SCPI" || releasing_ || HasConfiguration()) {
        Error(Detail::SETTINGS_CONFLICT_ERROR, "Trigger scope requires control and no pending configuration");
        return false;
    }
    if (!current && channels_.size() > 1U) {
        Error(Detail::INVALID_PARAMETER_ERROR, "All-channel trigger scope is not implemented; select CURRent");
        return false;
    }
    settings_.currentTriggerScope = current;
    return true;
}

DataResult InstrumentService::StimulusAxis(unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        DataResult failure;
        failure.error = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    DataResult value;
    const auto view = core_.ReadView(contextChannelId_);
    if (!profileReady_ || view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED ||
        view.configRevision != settings_.acquisitionRevision) {
        value.error = "No valid committed stimulus configuration";
        return value;
    }
    value.values = activePlan_.frequencyHz;
    value.ok = true;
    return value;
}

DataResult InstrumentService::MeasurementData(bool calibrated, unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        DataResult failure;
        failure.error = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    DataResult value;
    const std::string selected = SelectedMeasurement(contextChannelId_);
    const auto found = std::find_if(
        settings_.measurements.begin(), settings_.measurements.end(), [&selected](const Measurement &measurement) {
            return measurement.name == selected;
        });
    if (found == settings_.measurements.end()) {
        value.error = "No measurement selected";
        return value;
    }
    return ReadMeasurementData({*found}, calibrated);
}

DataResult InstrumentService::MeasurementData(
    const std::vector<unsigned> &selection, bool calibrated, unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    DataResult value;
    if (!channelScope.Valid()) {
        value.error = "Unknown channel or trace/channel mismatch";
        return value;
    }
    std::vector<Measurement> catalog;
    for (const auto &measurement : settings_.measurements) {
        if (measurement.channelId == contextChannelId_) {
            catalog.push_back(measurement);
        }
    }
    if (selection.empty() || selection.size() > VnaDisplay::MAX_DISPLAY_TRACES) {
        value.error = "Measurement selection is empty or exceeds the trace budget";
        return value;
    }
    std::vector<Measurement> measurements;
    measurements.reserve(selection.size());
    for (const unsigned index : selection) {
        if (index == 0U || index > catalog.size()) {
            value.error = "Measurement index is outside the channel catalog";
            return value;
        }
        measurements.push_back(catalog[index - 1U]);
    }
    return ReadMeasurementData(measurements, calibrated, calibrated);
}

DataResult InstrumentService::ReadMeasurementData(
    const std::vector<Measurement> &measurements, bool calibrated, bool requireCalibration) const
{
    DataResult value;
    if (HasMeasurement()) {
        value.error = "Finite measurement has not completed";
        return value;
    }
    const auto view = core_.ReadView(contextChannelId_);
    if (!view.frame || view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED ||
        view.frame->configRevision != settings_.acquisitionRevision ||
        view.configRevision != settings_.acquisitionRevision || view.frame->runId != view.runId) {
        value.error = "No complete measurement for the current configuration";
        return value;
    }
    const auto correction =
        calibrated ? calibration_.Correction(Identity(), activePlan_) : VnaCalibration::CalibrationPtr{};
    if (requireCalibration) {
        const auto calibration = calibration_.Snapshot(Identity(), activePlan_);
        if (!calibration.enabled || !calibration.compatible || !correction) {
            value.error = "Enabled compatible calibration is required for multi-trace SDATA";
            return value;
        }
        for (const auto &measurement : measurements) {
            if (measurement.kind != MeasurementKind::S_PARAMETER ||
                !correction->Covers(measurement.receiverPort, measurement.sourcePort)) {
                value.error = "Calibration does not cover every requested S parameter";
                return value;
            }
        }
    }
    return ExtractMeasurements(*view.frame, measurements, correction.get());
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
