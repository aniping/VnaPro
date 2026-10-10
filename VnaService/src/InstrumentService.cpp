#include "vna_service/InstrumentService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <set>
#include <utility>

#include "vna_core/AcquisitionCore.h"

namespace vna_service {
namespace {
OperationResult result(bool ok, int code = 0, const std::string& message = std::string())
{
    OperationResult value;
    value.ok = ok;
    value.errorCode = code;
    value.message = message;
    return value;
}

bool ready(const vna_core::Ticket& ticket)
{
    return ticket.valid() && ticket.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool validFrequency(double value)
{
    return std::isfinite(value) && value >= 1.0 && value <= 1000000000.0;
}

double quantizedBandwidth(double value)
{
    const double values[] = {10.0, 100.0, 1000.0, 10000.0, 100000.0};
    for (double valid : values) {
        if (value <= valid)
            return valid;
    }
    return 0.0;
}

vna_core::SweepPlan makePlan(const InstrumentSettings& settings)
{
    vna_core::SweepPlan plan;
    plan.ifBandwidthRaw = static_cast<std::uint32_t>(settings.ifBandwidthHz);
    std::set<unsigned> sources;
    for (const Measurement& measurement : settings.measurements) {
        sources.insert(measurement.sourcePort);
        plan.receiverMask |= 1U << (measurement.receiverPort - 1U);
    }
    if (sources.empty())
        sources.insert(1U);
    if (plan.receiverMask == 0U)
        plan.receiverMask = 1U;
    plan.sourcePorts.assign(sources.begin(), sources.end());
    for (unsigned point = 0; point < settings.points; ++point) {
        const double hz = settings.startHz + (settings.stopHz - settings.startHz) * static_cast<double>(point) /
                                                 static_cast<double>(settings.points - 1U);
        // Explicit mock contract: one raw frequency count = one Hz; table is opaque.
        const std::uint64_t raw = static_cast<std::uint64_t>(std::llround(hz));
        plan.frequencyRaw.push_back(raw);
        plan.frequencyHz.push_back(static_cast<double>(raw));
    }
    plan.tableData.assign(static_cast<std::size_t>(settings.points) * 9U, 0.0);
    return plan;
}
}  // namespace

class InstrumentService::Impl {
public:
    enum class Kind {
        Configure,
        Source,
        Mode,
        Trigger
    };

    struct Task {
        Kind kind = Kind::Configure;
        int stage = 0;
        bool measurement = false;
        bool forceInternalSource = false;
        std::string owner = "SCPI";
        std::uint64_t generation = 0;
        TriggerSource source = TriggerSource::Immediate;
        SweepMode mode = SweepMode::Hold;
        std::uint64_t expectedRevision = 0;
        bool checkRevision = false;
        std::function<OperationResult(InstrumentSettings&)> mutate;
        InstrumentSettings candidate;
        vna_core::Ticket ticket;
        std::promise<OperationResult> promise;
        Operation operation;

        Task() : operation(promise.get_future().share())
        {
        }
    };

    explicit Impl(vna_core::AcquisitionCore& value) : core(value)
    {
    }

    vna_core::AcquisitionCore& core;
    InstrumentSettings settings;
    std::deque<std::unique_ptr<Task>> tasks;
    std::deque<InstrumentError> errors;
    bool mockReady = false;
    std::string owner;
    std::uint64_t controlGeneration = 0;
    bool releasing = false;
    unsigned ese = 0;
    unsigned sre = 0;
    unsigned esr = 128U;
    unsigned generation = 0;
    std::string observedFault;

    void error(int code, const std::string& text)
    {
        if (code <= -100 && code > -200)
            esr |= 32U;
        else if (code <= -200 && code > -300)
            esr |= 16U;
        else if (code <= -300 && code > -400)
            esr |= 8U;
        else if (code <= -400 && code > -500)
            esr |= 4U;
        InstrumentError entry;
        entry.code = code;
        entry.message = text;
        if (errors.size() < 32U)
            errors.push_back(std::move(entry));
        else {
            errors.back().code = -350;
            errors.back().message = "Queue overflow";
            esr |= 8U;
        }
    }

    Operation rejected(int code, const std::string& message, const std::string& requester = "SCPI")
    {
        if (requester == "SCPI")
            error(code, message);
        std::promise<OperationResult> promise;
        Operation operation = promise.get_future().share();
        promise.set_value(result(false, code, message));
        return operation;
    }

    Operation enqueue(std::unique_ptr<Task> task, const std::string& requester = "SCPI")
    {
        if (owner != requester || requester.empty() || releasing)
            return rejected(-221, "No active instrument control", requester);
        if (!mockReady)
            return rejected(-221, "Verified backend profile is not initialized", requester);
        if (tasks.size() >= 32U)
            return rejected(-363, "Control input buffer overrun", requester);
        task->owner = requester;
        task->generation = controlGeneration;
        Operation operation = task->operation;
        tasks.push_back(std::move(task));
        return operation;
    }

    Operation configure(std::function<OperationResult(InstrumentSettings&)> mutate, std::uint64_t expected,
        const std::string& requester = "SCPI")
    {
        if (settings.sweepMode != SweepMode::Hold || hasMeasurement() ||
            core.readView().state != vna_core::CoreState::Ready)
            return rejected(-221, "Configuration requires HOLD and no pending measurement", requester);
        std::unique_ptr<Task> task(new Task);
        task->mutate = std::move(mutate);
        task->expectedRevision = expected;
        task->checkRevision = expected != 0U || requester != "SCPI";
        return enqueue(std::move(task), requester);
    }

    bool hasMeasurement() const
    {
        for (const auto& task : tasks)
            if (task->measurement)
                return true;
        return false;
    }

    bool hasConfiguration() const
    {
        for (const auto& task : tasks) {
            if (task->kind == Kind::Configure)
                return true;
        }
        return false;
    }

    void finish(OperationResult value)
    {
        if (!value.ok) {
            const auto view = core.readView();
            if (tasks.front()->measurement &&
                (view.state == vna_core::CoreState::Faulted || view.state == vna_core::CoreState::Closed))
                settings.sweepMode = SweepMode::Hold;
            if (tasks.front()->owner == "SCPI" || view.state == vna_core::CoreState::Faulted)
                error(value.errorCode, value.message);
            observedFault = view.error;
        }
        tasks.front()->promise.set_value(value);
        tasks.pop_front();
    }

    bool consume(Task& task)
    {
        if (!ready(task.ticket))
            return false;
        const vna_core::CommandResult value = task.ticket.get();
        task.ticket = vna_core::Ticket();
        if (!value.ok) {
            finish(result(false, -310, value.message));
            return false;
        }
        return true;
    }

    void commitMode(Task& task)
    {
        settings.triggerSource = task.candidate.triggerSource;
        settings.sweepMode = task.candidate.sweepMode;
        finish(result(true));
    }

    void pollTask()
    {
        if (tasks.empty())
            return;
        Task& task = *tasks.front();
        if (task.stage == 0 && (task.owner != owner || task.generation != controlGeneration)) {
            finish(result(false, -221, "Instrument control generation changed"));
            return;
        }
        if (task.kind == Kind::Configure) {
            if (task.stage == 0) {
                const auto view = core.readView();
                if (settings.sweepMode != SweepMode::Hold || view.state != vna_core::CoreState::Ready) {
                    finish(result(false, -221, "Configuration requires HOLD and no pending measurement"));
                    return;
                }
                if (task.checkRevision && task.expectedRevision != settings.revision) {
                    finish(result(false, -221, "Configuration revision changed"));
                    return;
                }
                task.candidate = settings;
                const OperationResult prepared = task.mutate(task.candidate);
                if (!prepared.ok) {
                    finish(prepared);
                    return;
                }
                task.ticket = core.configure(makePlan(task.candidate));
                task.stage = 1;
                return;
            }
            if (!ready(task.ticket))
                return;
            const auto value = task.ticket.get();
            if (!value.ok) {
                finish(result(false, -310, value.message));
                return;
            }
            task.candidate.revision = value.configRevision;
            settings = task.candidate;
            finish(result(true));
            return;
        }
        if (task.kind == Kind::Trigger) {
            if (task.stage == 0) {
                const auto view = core.readView();
                if (settings.triggerSource != TriggerSource::Manual || settings.sweepMode == SweepMode::Hold ||
                    view.state != vna_core::CoreState::Running) {
                    finish(result(false, -221, "INIT requires MANual source and an armed channel"));
                    return;
                }
                task.ticket = core.triggerOnce(view.runId, view.configRevision);
                task.stage = 1;
                return;
            }
            if (!consume(task))
                return;
            if (task.stage == 1 && settings.sweepMode == SweepMode::Single) {
                task.ticket = core.stop(core.readView().runId);
                task.stage = 2;
                return;
            }
            if (task.stage == 2)
                settings.sweepMode = SweepMode::Hold;
            finish(result(true));
            return;
        }
        // Mode/source changes are finite transitions. SING internally uses a
        // manual run, one trigger and a Stop barrier, so it cannot oversweep.
        if (task.stage == 0) {
            const auto view = core.readView();
            if (view.state != vna_core::CoreState::Ready && view.state != vna_core::CoreState::Running) {
                finish(result(false, -221, "Instrument is not ready"));
                return;
            }
            task.candidate = settings;
            if (task.kind == Kind::Mode)
                task.candidate.sweepMode = task.mode;
            else
                task.candidate.triggerSource = task.source;
            if (task.forceInternalSource)
                task.candidate.triggerSource = TriggerSource::Immediate;
            task.measurement = task.candidate.sweepMode == SweepMode::Single &&
                               task.candidate.triggerSource == TriggerSource::Immediate;
            if (view.state == vna_core::CoreState::Running) {
                task.ticket = core.stop(view.runId);
                task.stage = 1;
                return;
            }
            task.stage = 2;
        }
        if (task.stage == 1) {
            if (!consume(task))
                return;
            task.stage = 2;
        }
        if (task.stage == 2) {
            if (task.candidate.sweepMode == SweepMode::Hold) {
                commitMode(task);
                return;
            }
            const auto mode = task.candidate.triggerSource == TriggerSource::Immediate &&
                                      task.candidate.sweepMode == SweepMode::Continuous
                                  ? vna_core::RunMode::Continuous
                                  : vna_core::RunMode::Manual;
            task.ticket = core.start(mode);
            task.stage = 3;
            return;
        }
        if (task.stage == 3) {
            if (!consume(task))
                return;
            if (task.candidate.sweepMode == SweepMode::Single) {
                settings.sweepMode = SweepMode::Single;
                settings.triggerSource = task.candidate.triggerSource;
            }
            if (task.candidate.sweepMode != SweepMode::Single ||
                task.candidate.triggerSource != TriggerSource::Immediate) {
                commitMode(task);
                return;
            }
            const auto view = core.readView();
            task.ticket = core.triggerOnce(view.runId, view.configRevision);
            task.stage = 4;
            return;
        }
        if (task.stage == 4) {
            if (!consume(task))
                return;
            task.ticket = core.stop(core.readView().runId);
            task.stage = 5;
            return;
        }
        if (task.stage == 5) {
            if (!consume(task))
                return;
            task.candidate.sweepMode = SweepMode::Hold;
            commitMode(task);
        }
    }
};

InstrumentService::InstrumentService(vna_core::AcquisitionCore& core) : impl_(new Impl(core))
{
}

InstrumentService::~InstrumentService() = default;

bool InstrumentService::bootstrapMockProfile(std::string& error)
{
    const auto view = impl_->core.readView();
    if (impl_->mockReady || !impl_->owner.empty() || view.state != vna_core::CoreState::Ready ||
        view.deviceIdn.find("VnaPro,NativeSimulator,") != 0U) {
        error = "Mock profile requires an initialized NativeSimulator before connection admission";
        return false;
    }
    Measurement initial;
    initial.name = "DefaultA";
    impl_->settings.measurements.push_back(initial);
    impl_->settings.selectedMeasurement = initial.name;
    const auto configured = impl_->core.configure(makePlan(impl_->settings)).get();
    if (!configured.ok) {
        error = configured.message;
        return false;
    }
    impl_->settings.revision = configured.configRevision;
    impl_->mockReady = true;
    return true;
}

void InstrumentService::poll()
{
    impl_->pollTask();
    const auto view = impl_->core.readView();
    if (impl_->tasks.empty() && view.state == vna_core::CoreState::Faulted && view.error != impl_->observedFault) {
        impl_->error(-310, view.error);
        impl_->observedFault = view.error;
    }
}

bool InstrumentService::hasPending() const
{
    return !impl_->tasks.empty();
}

std::vector<Operation> InstrumentService::pendingOperations() const
{
    std::vector<Operation> operations;
    for (const auto& task : impl_->tasks)
        operations.push_back(task->operation);
    return operations;
}

bool InstrumentService::tryAcquireControl(const std::string& owner)
{
    if (!impl_->owner.empty() || owner.empty())
        return false;
    impl_->owner = owner;
    impl_->releasing = false;
    ++impl_->controlGeneration;
    return true;
}

void InstrumentService::beginReleaseControl(const std::string& owner)
{
    if (impl_->owner == owner)
        impl_->releasing = true;
}

void InstrumentService::releaseControl(const std::string& owner)
{
    if (impl_->owner == owner && impl_->tasks.empty()) {
        impl_->owner.clear();
        impl_->releasing = false;
        ++impl_->controlGeneration;
    }
}

std::string InstrumentService::controlOwner() const
{
    return impl_->owner;
}

bool InstrumentService::controlReleasing() const
{
    return impl_->releasing;
}

InstrumentSnapshot InstrumentService::snapshot() const
{
    InstrumentSnapshot value;
    value.settings = impl_->settings;
    value.core = impl_->core.readView();
    value.controlOwner = impl_->owner;
    value.releasing = impl_->releasing;
    value.pending = !impl_->tasks.empty();
    value.finitePending = impl_->hasMeasurement();
    return value;
}

const InstrumentSettings& InstrumentService::settings() const
{
    return impl_->settings;
}

std::string InstrumentService::identity() const
{
    return impl_->mockReady ? "VnaPro,NativeSimulator,4PORT,1.0" : "VnaPro,UnverifiedBackend,0,1.0";
}

Operation InstrumentService::setStartHz(double value, std::uint64_t expected)
{
    return impl_->configure(
        [value](InstrumentSettings& settings) {
            if (!validFrequency(value))
                return result(false, -222, "Start frequency out of mock range");
            const double span = settings.stopHz - settings.startHz;
            const double start = std::round(value);
            const double stop = start > settings.stopHz ? start + span : settings.stopHz;
            if (!validFrequency(stop))
                return result(false, -222, "Coupled stop frequency out of mock range");
            settings.startHz = start;
            settings.stopHz = stop;
            return result(true);
        },
        expected);
}

Operation InstrumentService::setStopHz(double value, std::uint64_t expected)
{
    return impl_->configure(
        [value](InstrumentSettings& settings) {
            if (!validFrequency(value))
                return result(false, -222, "Stop frequency out of mock range");
            const double span = settings.stopHz - settings.startHz;
            const double stop = std::round(value);
            const double start = stop < settings.startHz ? stop - span : settings.startHz;
            if (!validFrequency(start))
                return result(false, -222, "Coupled start frequency out of mock range");
            settings.startHz = start;
            settings.stopHz = stop;
            return result(true);
        },
        expected);
}

Operation InstrumentService::setPoints(unsigned value, std::uint64_t expected)
{
    return impl_->configure(
        [value](InstrumentSettings& settings) {
            if (value < 2U || value > 10001U)
                return result(false, -222, "Point count outside 2..10001 mock range");
            settings.points = value;
            return result(true);
        },
        expected);
}

Operation InstrumentService::setIfBandwidthHz(double value, std::uint64_t expected)
{
    return impl_->configure(
        [value](InstrumentSettings& settings) {
            if (!std::isfinite(value) || value <= 0.0)
                return result(false, -222, "IF bandwidth out of range");
            const double quantized = quantizedBandwidth(value);
            if (quantized == 0.0)
                return result(false, -222, "IF bandwidth out of mock range");
            settings.ifBandwidthHz = quantized;
            return result(true);
        },
        expected);
}

Operation InstrumentService::applyConfiguration(ConfigPatch patch, std::uint64_t expected, const std::string& owner)
{
    return impl_->configure(
        [patch](InstrumentSettings& settings) {
            if (!patch.hasStartHz && !patch.hasStopHz && !patch.hasPoints && !patch.hasIfBandwidthHz)
                return result(false, -109, "Configuration patch is empty");
            const double span = settings.stopHz - settings.startHz;
            double start = settings.startHz;
            double stop = settings.stopHz;
            if (patch.hasStartHz) {
                if (!validFrequency(patch.startHz))
                    return result(false, -222, "Start frequency out of mock range");
                start = std::round(patch.startHz);
            }
            if (patch.hasStopHz) {
                if (!validFrequency(patch.stopHz))
                    return result(false, -222, "Stop frequency out of mock range");
                stop = std::round(patch.stopHz);
            }
            if (patch.hasStartHz && patch.hasStopHz && start > stop)
                return result(false, -222, "Start frequency exceeds stop frequency");
            if (patch.hasStartHz && !patch.hasStopHz && start > stop)
                stop = start + span;
            if (patch.hasStopHz && !patch.hasStartHz && stop < start)
                start = stop - span;
            if (!validFrequency(start) || !validFrequency(stop))
                return result(false, -222, "Coupled frequency endpoint out of mock range");
            if (patch.hasPoints && (patch.points < 2U || patch.points > 10001U))
                return result(false, -222, "Point count outside 2..10001");
            if (patch.hasIfBandwidthHz) {
                if (!std::isfinite(patch.ifBandwidthHz) || patch.ifBandwidthHz <= 0 ||
                    quantizedBandwidth(patch.ifBandwidthHz) == 0)
                    return result(false, -222, "IF bandwidth out of mock range");
                settings.ifBandwidthHz = quantizedBandwidth(patch.ifBandwidthHz);
            }
            settings.startHz = start;
            settings.stopHz = stop;
            if (patch.hasPoints)
                settings.points = patch.points;
            return result(true);
        },
        expected, owner);
}

Operation InstrumentService::defineMeasurement(
    Measurement measurement, std::uint64_t expected, const std::string& owner)
{
    measurement.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(measurement.receiverKind)));
    return impl_->configure(
        [measurement](InstrumentSettings& settings) {
            if (measurement.name.empty() || measurement.name.size() > 128U ||
                (measurement.receiverKind != 'a' && measurement.receiverKind != 'b') || measurement.receiverPort < 1U ||
                measurement.receiverPort > 4U || measurement.sourcePort < 1U || measurement.sourcePort > 4U)
                return result(false, -222, "Invalid non-ratio receiver measurement");
            if (settings.measurements.size() >= 32U)
                return result(false, -222, "Measurement catalog is full");
            for (const auto& existing : settings.measurements)
                if (existing.name == measurement.name)
                    return result(false, -221, "Measurement name already exists");
            settings.measurements.push_back(measurement);
            if (settings.selectedMeasurement.empty())
                settings.selectedMeasurement = measurement.name;
            return result(true);
        },
        expected, owner);
}

Operation InstrumentService::deleteMeasurement(
    const std::string& name, std::uint64_t expected, const std::string& owner)
{
    return impl_->configure(
        [name](InstrumentSettings& settings) {
            const auto found = std::find_if(
                settings.measurements.begin(), settings.measurements.end(), [&name](const Measurement& measurement) {
                    return measurement.name == name;
                });
            if (found == settings.measurements.end())
                return result(false, -224, "Measurement name does not exist");
            settings.measurements.erase(found);
            if (settings.selectedMeasurement == name)
                settings.selectedMeasurement =
                    settings.measurements.empty() ? std::string() : settings.measurements.front().name;
            return result(true);
        },
        expected, owner);
}

Operation InstrumentService::setTriggerSource(TriggerSource source, const std::string& owner)
{
    std::unique_ptr<Impl::Task> task(new Impl::Task);
    task->kind = Impl::Kind::Source;
    task->source = source;
    task->measurement = source == TriggerSource::Immediate && impl_->settings.sweepMode == SweepMode::Single;
    return impl_->enqueue(std::move(task), owner);
}

Operation InstrumentService::setSweepMode(SweepMode mode, const std::string& owner)
{
    if (mode == SweepMode::Single && impl_->hasMeasurement())
        return impl_->rejected(-221, "Measurement already in progress", owner);
    std::unique_ptr<Impl::Task> task(new Impl::Task);
    task->kind = Impl::Kind::Mode;
    task->mode = mode;
    task->measurement = mode == SweepMode::Single && impl_->settings.triggerSource == TriggerSource::Immediate;
    return impl_->enqueue(std::move(task), owner);
}

Operation InstrumentService::trigger(const std::string& owner)
{
    if (impl_->hasMeasurement())
        return impl_->rejected(-221, "Busy INIT restart is not supported", owner);
    std::unique_ptr<Impl::Task> task(new Impl::Task);
    task->kind = Impl::Kind::Trigger;
    task->measurement = true;
    return impl_->enqueue(std::move(task), owner);
}

Operation InstrumentService::runSingle(const std::string& owner)
{
    if (impl_->hasMeasurement())
        return impl_->rejected(-221, "Measurement already in progress", owner);
    std::unique_ptr<Impl::Task> task(new Impl::Task);
    task->kind = Impl::Kind::Mode;
    task->mode = SweepMode::Single;
    task->forceInternalSource = true;
    task->measurement = true;
    return impl_->enqueue(std::move(task), owner);
}

Operation InstrumentService::runContinuous(const std::string& owner)
{
    std::unique_ptr<Impl::Task> task(new Impl::Task);
    task->kind = Impl::Kind::Mode;
    task->mode = SweepMode::Continuous;
    task->forceInternalSource = true;
    return impl_->enqueue(std::move(task), owner);
}

bool InstrumentService::selectMeasurement(const std::string& name)
{
    if (impl_->owner != "SCPI" || impl_->releasing || impl_->hasConfiguration()) {
        impl_->error(-221, "Measurement selection requires control and no pending configuration");
        return false;
    }
    for (const auto& measurement : impl_->settings.measurements) {
        if (measurement.name == name) {
            impl_->settings.selectedMeasurement = name;
            return true;
        }
    }
    impl_->error(-224, "Measurement name does not exist");
    return false;
}

bool InstrumentService::setCurrentTriggerScope(bool current)
{
    if (impl_->owner != "SCPI" || impl_->releasing || impl_->hasConfiguration()) {
        impl_->error(-221, "Trigger scope requires control and no pending configuration");
        return false;
    }
    impl_->settings.currentTriggerScope = current;
    return true;
}

DataResult InstrumentService::stimulusAxis() const
{
    DataResult value;
    const auto view = impl_->core.readView();
    if (!impl_->mockReady || view.state == vna_core::CoreState::Faulted || view.state == vna_core::CoreState::Closed ||
        view.configRevision != impl_->settings.revision) {
        value.error = "No valid committed stimulus configuration";
        return value;
    }
    value.values = makePlan(impl_->settings).frequencyHz;
    value.ok = true;
    return value;
}

DataResult InstrumentService::measurementData() const
{
    DataResult value;
    if (impl_->hasMeasurement()) {
        value.error = "Finite measurement has not completed";
        return value;
    }
    const std::string selected = impl_->settings.selectedMeasurement;
    const auto found = std::find_if(impl_->settings.measurements.begin(), impl_->settings.measurements.end(),
        [&selected](const Measurement& measurement) {
            return measurement.name == selected;
        });
    if (found == impl_->settings.measurements.end()) {
        value.error = "No measurement selected";
        return value;
    }
    const Measurement measurement = *found;
    const auto view = impl_->core.readView();
    if (!view.frame || view.state == vna_core::CoreState::Faulted || view.state == vna_core::CoreState::Closed ||
        view.frame->configRevision != impl_->settings.revision || view.configRevision != impl_->settings.revision ||
        view.frame->runId != view.runId) {
        value.error = "No complete measurement for the current configuration";
        return value;
    }
    const auto& frame = *view.frame;
    if ((frame.plan.receiverMask & (1U << (measurement.receiverPort - 1U))) == 0U) {
        value.error = "Receiver was not active";
        return value;
    }
    const auto port =
        std::find_if(frame.ports.begin(), frame.ports.end(), [&measurement](const vna_core::PortFrame& source) {
            return source.sourcePort == measurement.sourcePort;
        });
    const std::size_t points = frame.plan.frequencyHz.size();
    if (port == frame.ports.end() || port->iq.size() != points * 16U) {
        value.error = "Receiver source frame is unavailable";
        return value;
    }
    value.values.reserve(points * 2U);
    const std::size_t component = (measurement.receiverPort - 1U) * 4U + (measurement.receiverKind == 'b' ? 2U : 0U);
    for (std::size_t point = 0; point < points; ++point) {
        const double real = port->iq[point * 16U + component];
        const double imaginary = port->iq[point * 16U + component + 1U];
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

void InstrumentService::recordError(int code, const std::string& message)
{
    impl_->error(code, message);
}

InstrumentError InstrumentService::popError()
{
    if (impl_->errors.empty())
        return InstrumentError();
    InstrumentError value = impl_->errors.front();
    impl_->errors.pop_front();
    return value;
}

unsigned InstrumentService::errorCount() const
{
    return static_cast<unsigned>(impl_->errors.size());
}

void InstrumentService::clearStatus()
{
    impl_->errors.clear();
    impl_->esr = 0;
    ++impl_->generation;
}

unsigned InstrumentService::statusGeneration() const
{
    return impl_->generation;
}

void InstrumentService::setEventEnable(unsigned value)
{
    impl_->ese = value & 255U;
}

unsigned InstrumentService::eventEnable() const
{
    return impl_->ese;
}

void InstrumentService::setServiceRequestEnable(unsigned value)
{
    impl_->sre = value & 191U;
}

unsigned InstrumentService::serviceRequestEnable() const
{
    return impl_->sre;
}

unsigned InstrumentService::readEventRegister()
{
    const unsigned value = impl_->esr;
    impl_->esr = 0;
    return value;
}

void InstrumentService::markOperationComplete()
{
    impl_->esr |= 1U;
}

unsigned InstrumentService::statusByte(bool outputAvailable) const
{
    unsigned value = impl_->errors.empty() ? 0U : 4U;
    if (outputAvailable)
        value |= 16U;
    if ((impl_->esr & impl_->ese) != 0U)
        value |= 32U;
    if ((value & impl_->sre) != 0U)
        value |= 64U;
    return value;
}

}  // namespace vna_service
