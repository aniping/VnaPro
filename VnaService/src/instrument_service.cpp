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
#include <set>
#include <utility>

#include "vna_core/acquisition_core.h"
#include "vna_core/frame_layout.h"
#include "vna_service/service_error_code.h"

namespace vna_service {
namespace {
constexpr double MIN_FREQUENCY_HZ = 1.0;
constexpr double MAX_FREQUENCY_HZ = 1000000000.0;
constexpr unsigned MIN_SWEEP_POINTS = 2U;
constexpr unsigned MAX_SWEEP_POINTS = 10001U;
constexpr std::size_t TASK_QUEUE_CAPACITY = 32U;
constexpr std::size_t ERROR_QUEUE_CAPACITY = 32U;
constexpr std::size_t MEASUREMENT_CAPACITY = 32U;
constexpr std::size_t MAX_MEASUREMENT_NAME_LENGTH = 128U;
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

OperationResult Result(bool ok, int code = 0, const std::string &message = std::string())
{
    OperationResult value;
    value.ok = ok;
    value.errorCode = code;
    value.message = message;
    return value;
}

bool Ready(const vna_core::Ticket &ticket)
{
    return ticket.valid() && ticket.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool ValidFrequency(double value) noexcept
{
    return std::isfinite(value) && value >= MIN_FREQUENCY_HZ && value <= MAX_FREQUENCY_HZ;
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

vna_core::SweepPlan MakePlan(const InstrumentSettings &settings)
{
    vna_core::SweepPlan plan;
    plan.ifBandwidthRaw = static_cast<std::uint32_t>(settings.ifBandwidthHz);
    std::set<unsigned> sources;
    for (const Measurement &measurement : settings.measurements) {
        sources.insert(measurement.sourcePort);
        plan.receiverMask |= 1U << (measurement.receiverPort - 1U);
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
        // Explicit mock contract: one raw frequency count = one Hz; table is opaque.
        const std::uint64_t raw = static_cast<std::uint64_t>(std::llround(hz));
        plan.frequencyRaw.push_back(raw);
        plan.frequencyHz.push_back(static_cast<double>(raw));
    }
    plan.tableData.assign(static_cast<std::size_t>(settings.points) * vna_core::TABLE_VALUES_PER_POINT, 0.0);
    return plan;
}

OperationResult ApplyFrequencyPatch(const ConfigPatch &patch, InstrumentSettings &settings)
{
    const double span = settings.stopHz - settings.startHz;
    double start = settings.startHz;
    double stop = settings.stopHz;
    if (patch.hasStartHz) {
        if (!ValidFrequency(patch.startHz)) {
            return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency out of mock range");
        }
        start = std::round(patch.startHz);
    }
    if (patch.hasStopHz) {
        if (!ValidFrequency(patch.stopHz)) {
            return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Stop frequency out of mock range");
        }
        stop = std::round(patch.stopHz);
    }
    if (patch.hasStartHz && patch.hasStopHz && start > stop) {
        return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency exceeds stop frequency");
    }
    if (patch.hasStartHz && !patch.hasStopHz && start > stop) {
        stop = start + span;
    }
    if (patch.hasStopHz && !patch.hasStartHz && stop < start) {
        start = stop - span;
    }
    if (!ValidFrequency(start) || !ValidFrequency(stop)) {
        return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Coupled frequency endpoint out of mock range");
    }
    settings.startHz = start;
    settings.stopHz = stop;
    return Result(true);
}

OperationResult ApplySettingsPatch(const ConfigPatch &patch, InstrumentSettings &settings)
{
    if (!patch.hasStartHz && !patch.hasStopHz && !patch.hasPoints && !patch.hasIfBandwidthHz) {
        return Result(false, detail::MISSING_PARAMETER_ERROR, "Configuration patch is empty");
    }
    const OperationResult frequency = ApplyFrequencyPatch(patch, settings);
    if (!frequency.ok) {
        return frequency;
    }
    if (patch.hasPoints && (patch.points < MIN_SWEEP_POINTS || patch.points > MAX_SWEEP_POINTS)) {
        return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001");
    }
    if (patch.hasIfBandwidthHz) {
        if (!std::isfinite(patch.ifBandwidthHz) || patch.ifBandwidthHz <= 0 ||
            QuantizedBandwidth(patch.ifBandwidthHz) == 0) {
            return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of mock range");
        }
        settings.ifBandwidthHz = QuantizedBandwidth(patch.ifBandwidthHz);
    }
    if (patch.hasPoints) {
        settings.points = patch.points;
    }
    return Result(true);
}

DataResult ExtractMeasurement(const vna_core::MeasurementFrame &frame, const Measurement &measurement)
{
    DataResult value;

    if ((frame.plan.receiverMask & (1U << (measurement.receiverPort - 1U))) == 0U) {
        value.error = "Receiver was not active";
        return value;
    }
    const auto port =
        std::find_if(frame.ports.begin(), frame.ports.end(), [&measurement](const vna_core::PortFrame &source) {
            return source.sourcePort == measurement.sourcePort;
        });
    const std::size_t points = frame.plan.frequencyHz.size();
    if (port == frame.ports.end() || port->iq.size() != points * vna_core::IQ_VALUES_PER_POINT) {
        value.error = "Receiver source frame is unavailable";
        return value;
    }
    value.values.reserve(points * vna_core::COMPLEX_COMPONENT_COUNT);
    const std::size_t component = (measurement.receiverPort - 1U) * vna_core::IQ_VALUES_PER_RECEIVER +
                                  (measurement.receiverKind == 'b' ? vna_core::COMPLEX_COMPONENT_COUNT : 0U);
    for (std::size_t point = 0; point < points; ++point) {
        const double real = port->iq[point * vna_core::IQ_VALUES_PER_POINT + component];
        const double imaginary = port->iq[point * vna_core::IQ_VALUES_PER_POINT + component + 1U];
        if (!std::isfinite(real) || !std::isfinite(imaginary)) {
            value.values.clear();
            value.error = "Receiver frame contains invalid values";
            return value;
        }
        value.values.push_back(real);
        value.values.push_back(imaginary);
    }
    value.ok = true;
    return value;
}

}  // namespace

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
        errors_.back().code = detail::ERROR_QUEUE_OVERFLOW;
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
    return operation;
}

Operation InstrumentService::Enqueue(detail::ServiceTask task, const std::string &requester)
{
    if (owner_ != requester || requester.empty() || releasing_) {
        return Rejected(detail::SETTINGS_CONFLICT_ERROR, "No active instrument control", requester);
    }
    if (!mockReady_) {
        return Rejected(detail::SETTINGS_CONFLICT_ERROR, "Verified backend profile is not initialized", requester);
    }
    if (tasks_.size() >= TASK_QUEUE_CAPACITY) {
        return Rejected(detail::INPUT_BUFFER_OVERRUN, "Control input buffer overrun", requester);
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
        core_.ReadView().state != vna_core::CoreState::READY) {
        return Rejected(
            detail::SETTINGS_CONFLICT_ERROR, "Configuration requires HOLD and no pending measurement", requester);
    }
    detail::ServiceTask task;
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
        if (task.kind == detail::ServiceTaskKind::CONFIGURE) {
            return true;
        }
    }
    return false;
}

void InstrumentService::Finish(OperationResult value)
{
    if (!value.ok) {
        const auto view = core_.ReadView();
        if (tasks_.front().measurement &&
            (view.state == vna_core::CoreState::FAULTED || view.state == vna_core::CoreState::CLOSED)) {
            settings_.sweepMode = SweepMode::HOLD;
        }
        if (tasks_.front().owner == "SCPI" || view.state == vna_core::CoreState::FAULTED) {
            Error(value.errorCode, value.message);
        }
        observedFault_ = view.error;
    }
    tasks_.front().promise.set_value(std::move(value));
    tasks_.pop_front();
}

bool InstrumentService::Consume(detail::ServiceTask &task)
{
    if (!Ready(task.ticket)) {
        return false;
    }
    const vna_core::CommandResult value = task.ticket.get();
    task.ticket = vna_core::Ticket();
    if (!value.ok) {
        Finish(Result(false, detail::DEVICE_SPECIFIC_ERROR, value.message));
        return false;
    }
    return true;
}

void InstrumentService::CommitMode(detail::ServiceTask &task)
{
    settings_.triggerSource = task.candidate.triggerSource;
    settings_.sweepMode = task.candidate.sweepMode;
    Finish(Result(true));
}

void InstrumentService::PollTask()
{
    if (tasks_.empty()) {
        return;
    }
    detail::ServiceTask &task = tasks_.front();
    if (task.stage == detail::ServiceTaskStage::PENDING &&
        (task.owner != owner_ || task.generation != controlGeneration_)) {
        Finish(Result(false, detail::SETTINGS_CONFLICT_ERROR, "Instrument control generation changed"));
        return;
    }
    if (task.kind == detail::ServiceTaskKind::CONFIGURE) {
        PollConfiguration(task);
    } else if (task.kind == detail::ServiceTaskKind::TRIGGER) {
        PollTrigger(task);
    } else {
        PollMode(task);
    }
}

void InstrumentService::PollConfiguration(detail::ServiceTask &task)
{
    if (task.stage == detail::ServiceTaskStage::PENDING) {
        const auto view = core_.ReadView();
        if (settings_.sweepMode != SweepMode::HOLD || view.state != vna_core::CoreState::READY) {
            Finish(Result(
                false, detail::SETTINGS_CONFLICT_ERROR, "Configuration requires HOLD and no pending measurement"));
            return;
        }
        if (task.checkRevision && task.expectedRevision != settings_.revision) {
            Finish(Result(false, detail::SETTINGS_CONFLICT_ERROR, "Configuration revision changed"));
            return;
        }
        task.candidate = settings_;
        const OperationResult prepared = task.mutate(task.candidate);
        if (!prepared.ok) {
            Finish(prepared);
            return;
        }
        task.ticket = core_.Configure(MakePlan(task.candidate));
        task.stage = detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    if (!Ready(task.ticket)) {
        return;
    }
    const auto value = task.ticket.get();
    if (!value.ok) {
        Finish(Result(false, detail::DEVICE_SPECIFIC_ERROR, value.message));
        return;
    }
    task.candidate.revision = value.configRevision;
    settings_ = std::move(task.candidate);
    Finish(Result(true));
    return;
}

void InstrumentService::PollTrigger(detail::ServiceTask &task)
{
    if (task.stage == detail::ServiceTaskStage::PENDING) {
        const auto view = core_.ReadView();
        if (settings_.triggerSource != TriggerSource::MANUAL || settings_.sweepMode == SweepMode::HOLD ||
            view.state != vna_core::CoreState::RUNNING) {
            Finish(Result(false, detail::SETTINGS_CONFLICT_ERROR, "INIT requires MANual source and an armed channel"));
            return;
        }
        task.ticket = core_.TriggerOnce(view.runId, view.configRevision);
        task.stage = detail::ServiceTaskStage::WAITING_TRIGGER;
        return;
    }
    if (!Consume(task)) {
        return;
    }
    if (task.stage == detail::ServiceTaskStage::WAITING_TRIGGER && settings_.sweepMode == SweepMode::SINGLE) {
        task.ticket = core_.Stop(core_.ReadView().runId);
        task.stage = detail::ServiceTaskStage::WAITING_STOP;
        return;
    }
    if (task.stage == detail::ServiceTaskStage::WAITING_STOP) {
        settings_.sweepMode = SweepMode::HOLD;
    }
    Finish(Result(true));
    return;
}

bool InstrumentService::BeginMode(detail::ServiceTask &task)
{
    const auto view = core_.ReadView();
    if (view.state != vna_core::CoreState::READY && view.state != vna_core::CoreState::RUNNING) {
        Finish(Result(false, detail::SETTINGS_CONFLICT_ERROR, "Instrument is not ready"));
        return false;
    }
    task.candidate = settings_;
    if (task.kind == detail::ServiceTaskKind::MODE) {
        task.candidate.sweepMode = task.mode;
    } else {
        task.candidate.triggerSource = task.source;
    }
    if (task.forceInternalSource) {
        task.candidate.triggerSource = TriggerSource::IMMEDIATE;
    }
    task.measurement =
        task.candidate.sweepMode == SweepMode::SINGLE && task.candidate.triggerSource == TriggerSource::IMMEDIATE;
    if (view.state == vna_core::CoreState::RUNNING) {
        task.ticket = core_.Stop(view.runId);
        task.stage = detail::ServiceTaskStage::WAITING_STOP;
        return false;
    }
    task.stage = detail::ServiceTaskStage::READY_TO_START;

    return true;
}

void InstrumentService::PollMode(detail::ServiceTask &task)
{
    // A single sweep uses a manual trigger followed by a Stop barrier.
    if (task.stage == detail::ServiceTaskStage::PENDING && !BeginMode(task)) {
        return;
    }

    if (task.stage == detail::ServiceTaskStage::WAITING_STOP) {
        if (!Consume(task)) {
            return;
        }
        task.stage = detail::ServiceTaskStage::READY_TO_START;
    }
    if (task.stage == detail::ServiceTaskStage::READY_TO_START) {
        if (task.candidate.sweepMode == SweepMode::HOLD) {
            CommitMode(task);
            return;
        }
        const auto mode = task.candidate.triggerSource == TriggerSource::IMMEDIATE &&
                                  task.candidate.sweepMode == SweepMode::CONTINUOUS
                              ? vna_core::RunMode::CONTINUOUS
                              : vna_core::RunMode::MANUAL;
        task.ticket = core_.Start(mode);
        task.stage = detail::ServiceTaskStage::WAITING_START;
        return;
    }
    PollStartedMode(task);
}

void InstrumentService::PollStartedMode(detail::ServiceTask &task)
{
    if (task.stage == detail::ServiceTaskStage::WAITING_START) {
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
        task.stage = detail::ServiceTaskStage::WAITING_SINGLE_FRAME;
        return;
    }
    if (task.stage == detail::ServiceTaskStage::WAITING_SINGLE_FRAME) {
        if (!Consume(task)) {
            return;
        }
        task.ticket = core_.Stop(core_.ReadView().runId);
        task.stage = detail::ServiceTaskStage::WAITING_FINAL_STOP;
        return;
    }
    if (task.stage == detail::ServiceTaskStage::WAITING_FINAL_STOP) {
        if (!Consume(task)) {
            return;
        }
        task.candidate.sweepMode = SweepMode::HOLD;
        CommitMode(task);
    }
}

InstrumentService::InstrumentService(vna_core::AcquisitionCore &core) : core_(core)
{
}

InstrumentService::~InstrumentService() = default;

bool InstrumentService::BootstrapMockProfile(std::string &error)
{
    const auto view = core_.ReadView();
    if (mockReady_ || !owner_.empty() || view.state != vna_core::CoreState::READY ||
        view.deviceIdn.find("VnaPro,NativeSimulator,") != 0U) {
        error = "Mock profile requires an initialized NativeSimulator before connection admission";
        return false;
    }
    Measurement initial;
    initial.name = "DefaultA";
    InstrumentSettings candidate = settings_;
    candidate.measurements.push_back(initial);
    candidate.selectedMeasurement = initial.name;
    const auto configured = core_.Configure(MakePlan(candidate)).get();
    if (!configured.ok) {
        error = configured.message;
        return false;
    }
    candidate.revision = configured.configRevision;
    settings_ = std::move(candidate);
    mockReady_ = true;
    return true;
}

void InstrumentService::Poll()
{
    PollTask();
    const auto view = core_.ReadView();
    if (tasks_.empty() && view.state == vna_core::CoreState::FAULTED && view.error != observedFault_) {
        Error(detail::DEVICE_SPECIFIC_ERROR, view.error);
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

std::string InstrumentService::Identity() const
{
    return mockReady_ ? "VnaPro,NativeSimulator,4PORT,1.0" : "VnaPro,UnverifiedBackend,0,1.0";
}

Operation InstrumentService::SetStartHz(double value, std::uint64_t expected)
{
    return Configure(
        [value](InstrumentSettings &settings) {
            if (!ValidFrequency(value)) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Start frequency out of mock range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double start = std::round(value);
            const double stop = start > settings.stopHz ? start + span : settings.stopHz;
            if (!ValidFrequency(stop)) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Coupled stop frequency out of mock range");
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
        [value](InstrumentSettings &settings) {
            if (!ValidFrequency(value)) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Stop frequency out of mock range");
            }
            const double span = settings.stopHz - settings.startHz;
            const double stop = std::round(value);
            const double start = stop < settings.startHz ? stop - span : settings.startHz;
            if (!ValidFrequency(start)) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Coupled start frequency out of mock range");
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
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001 mock range");
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
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of range");
            }
            const double quantized = QuantizedBandwidth(value);
            if (quantized == 0.0) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of mock range");
            }
            settings.ifBandwidthHz = quantized;
            return Result(true);
        },
        expected);
}

Operation InstrumentService::ApplyConfiguration(ConfigPatch patch, std::uint64_t expected, const std::string &owner)
{
    return Configure(
        [patch](InstrumentSettings &settings) {
            return ApplySettingsPatch(patch, settings);
        },
        expected, owner);
}

Operation InstrumentService::DefineMeasurement(
    Measurement measurement, std::uint64_t expected, const std::string &owner)
{
    measurement.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(measurement.receiverKind)));
    return Configure(
        [measurement](InstrumentSettings &settings) {
            if (measurement.name.empty() || measurement.name.size() > MAX_MEASUREMENT_NAME_LENGTH ||
                (measurement.receiverKind != 'a' && measurement.receiverKind != 'b') || measurement.receiverPort < 1U ||
                measurement.receiverPort > vna_core::PHYSICAL_PORT_COUNT || measurement.sourcePort < 1U ||
                measurement.sourcePort > vna_core::PHYSICAL_PORT_COUNT) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Invalid non-ratio receiver measurement");
            }
            if (settings.measurements.size() >= MEASUREMENT_CAPACITY) {
                return Result(false, detail::DATA_OUT_OF_RANGE_ERROR, "Measurement catalog is full");
            }
            for (const auto &existing : settings.measurements) {
                if (existing.name == measurement.name) {
                    return Result(false, detail::SETTINGS_CONFLICT_ERROR, "Measurement name already exists");
                }
            }
            settings.measurements.push_back(measurement);
            if (settings.selectedMeasurement.empty()) {
                settings.selectedMeasurement = measurement.name;
            }
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::DeleteMeasurement(
    const std::string &name, std::uint64_t expected, const std::string &owner)
{
    return Configure(
        [name](InstrumentSettings &settings) {
            const auto found = std::find_if(
                settings.measurements.begin(), settings.measurements.end(), [&name](const Measurement &measurement) {
                    return measurement.name == name;
                });
            if (found == settings.measurements.end()) {
                return Result(false, detail::INVALID_PARAMETER_ERROR, "Measurement name does not exist");
            }
            settings.measurements.erase(found);
            if (settings.selectedMeasurement == name) {
                settings.selectedMeasurement =
                    settings.measurements.empty() ? std::string() : settings.measurements.front().name;
            }
            return Result(true);
        },
        expected, owner);
}

Operation InstrumentService::SetTriggerSource(TriggerSource source, const std::string &owner)
{
    if (source != TriggerSource::IMMEDIATE && source != TriggerSource::MANUAL) {
        return Rejected(detail::INVALID_PARAMETER_ERROR, "Invalid trigger source", owner);
    }
    detail::ServiceTask task;
    task.kind = detail::ServiceTaskKind::SOURCE;
    task.source = source;
    task.measurement = source == TriggerSource::IMMEDIATE && settings_.sweepMode == SweepMode::SINGLE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SetSweepMode(SweepMode mode, const std::string &owner)
{
    if (mode != SweepMode::HOLD && mode != SweepMode::SINGLE && mode != SweepMode::CONTINUOUS) {
        return Rejected(detail::INVALID_PARAMETER_ERROR, "Invalid sweep mode", owner);
    }
    if (mode == SweepMode::SINGLE && HasMeasurement()) {
        return Rejected(detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    detail::ServiceTask task;
    task.kind = detail::ServiceTaskKind::MODE;
    task.mode = mode;
    task.measurement = mode == SweepMode::SINGLE && settings_.triggerSource == TriggerSource::IMMEDIATE;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::Trigger(const std::string &owner)
{
    if (HasMeasurement()) {
        return Rejected(detail::SETTINGS_CONFLICT_ERROR, "Busy INIT restart is not supported", owner);
    }
    detail::ServiceTask task;
    task.kind = detail::ServiceTaskKind::TRIGGER;
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunSingle(const std::string &owner)
{
    if (HasMeasurement()) {
        return Rejected(detail::SETTINGS_CONFLICT_ERROR, "Measurement already in progress", owner);
    }
    detail::ServiceTask task;
    task.kind = detail::ServiceTaskKind::MODE;
    task.mode = SweepMode::SINGLE;
    task.forceInternalSource = true;
    task.measurement = true;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::RunContinuous(const std::string &owner)
{
    detail::ServiceTask task;
    task.kind = detail::ServiceTaskKind::MODE;
    task.mode = SweepMode::CONTINUOUS;
    task.forceInternalSource = true;
    return Enqueue(std::move(task), owner);
}

bool InstrumentService::SelectMeasurement(const std::string &name)
{
    if (owner_ != "SCPI" || releasing_ || HasConfiguration()) {
        Error(detail::SETTINGS_CONFLICT_ERROR, "Measurement selection requires control and no pending configuration");
        return false;
    }
    for (const auto &measurement : settings_.measurements) {
        if (measurement.name == name) {
            settings_.selectedMeasurement = name;
            return true;
        }
    }
    Error(detail::INVALID_PARAMETER_ERROR, "Measurement name does not exist");
    return false;
}

bool InstrumentService::SetCurrentTriggerScope(bool current)
{
    if (owner_ != "SCPI" || releasing_ || HasConfiguration()) {
        Error(detail::SETTINGS_CONFLICT_ERROR, "Trigger scope requires control and no pending configuration");
        return false;
    }
    settings_.currentTriggerScope = current;
    return true;
}

DataResult InstrumentService::StimulusAxis() const
{
    DataResult value;
    const auto view = core_.ReadView();
    if (!mockReady_ || view.state == vna_core::CoreState::FAULTED || view.state == vna_core::CoreState::CLOSED ||
        view.configRevision != settings_.revision) {
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
    const std::string selected = settings_.selectedMeasurement;
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
    if (!view.frame || view.state == vna_core::CoreState::FAULTED || view.state == vna_core::CoreState::CLOSED ||
        view.frame->configRevision != settings_.revision || view.configRevision != settings_.revision ||
        view.frame->runId != view.runId) {
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

}  // namespace vna_service
