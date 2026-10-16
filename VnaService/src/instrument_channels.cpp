/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
constexpr unsigned MAX_CHANNELS = 16U;

OperationResult ChannelResult(bool ok, const std::string &error = {}, int code = Detail::SETTINGS_CONFLICT_ERROR)
{
    OperationResult result;
    result.ok = ok;
    result.errorCode = ok ? 0 : code;
    result.message = error;
    return result;
}

std::string NextTraceName(const std::vector<Measurement> &catalog)
{
    for (std::size_t index = 1U; index <= catalog.size() + 1U; ++index) {
        const auto name = "Trc" + std::to_string(index);
        if (std::none_of(catalog.begin(), catalog.end(), [&name](const Measurement &item) {
                return item.name == name;
            })) {
            return name;
        }
    }
    return {};
}
}  // 匿名命名空间

InstrumentService::ContextScope::ContextScope(
    InstrumentService &service, unsigned channelId, const std::string &traceName, bool executing)
    : service_(service), previous_(service.contextChannelId_), previousExecuting_(service.executingTask_)
{
    const unsigned traceChannel = traceName.empty() ? 0U : service.MeasurementChannel(traceName);
    if (channelId != 0U && traceChannel != 0U && traceChannel != channelId) {
        return;
    }
    if (channelId == 0U) {
        channelId = traceChannel != 0U
                        ? traceChannel
                        : (service.contextDepth_ != 0U ? service.contextChannelId_ : service.ActiveChannelId());
    }
    valid_ = service.ActivateContext(channelId);
    if (valid_) {
        ++service_.contextDepth_;
        service_.executingTask_ = executing || previousExecuting_;
    }
}

InstrumentService::ContextScope::~ContextScope()
{
    if (valid_) {
        --service_.contextDepth_;
        service_.executingTask_ = previousExecuting_;
        if (!service_.ActivateContext(previous_)) {
            service_.ActivateContext(service_.ActiveChannelId());
        }
    }
}

bool InstrumentService::ContextScope::Valid() const noexcept
{
    return valid_;
}

bool InstrumentService::ActivateContext(unsigned channelId)
{
    if (!HasChannel(channelId)) {
        return false;
    }
    if (channelId == contextChannelId_) {
        return true;
    }
    // 目录只有这一份；切上下文时转移它，不给各通道保存可分叉的目录副本。
    auto catalog = std::move(settings_.measurements);
    auto &previous = channels_.at(contextChannelId_);
    previous.settings = std::move(settings_);
    previous.calibration = std::move(calibration_);
    previous.calibrationRun = calibrationRun_;
    previous.activePlan = std::move(activePlan_);
    previous.zoomHistory = std::move(zoomHistory_);
    auto &next = channels_.at(channelId);
    settings_ = std::move(next.settings);
    calibration_ = std::move(next.calibration);
    calibrationRun_ = next.calibrationRun;
    activePlan_ = std::move(next.activePlan);
    zoomHistory_ = std::move(next.zoomHistory);
    settings_.measurements = std::move(catalog);
    settings_.revision = logicalRevision_;
    contextChannelId_ = channelId;
    return true;
}

void InstrumentService::CommitLogicalRevision(std::uint64_t revision) noexcept
{
    logicalRevision_ = revision;
    settings_.revision = revision;
    for (auto &channel : channels_) {
        channel.second.settings.revision = revision;
    }
}

bool InstrumentService::HasChannel(unsigned channelId) const noexcept
{
    const auto found = channels_.find(channelId);
    return found != channels_.end() && found->second.committed;
}

bool InstrumentService::CalibrationCollecting() const noexcept
{
    if (calibration_.Collecting()) {
        return true;
    }
    for (const auto &channel : channels_) {
        if (channel.first != contextChannelId_ && channel.second.committed && channel.second.calibration.Collecting()) {
            return true;
        }
    }
    return false;
}

unsigned InstrumentService::MeasurementChannel(const std::string &name) const noexcept
{
    const auto found =
        std::find_if(settings_.measurements.begin(), settings_.measurements.end(), [&name](const Measurement &value) {
            return value.name == name;
        });
    return found == settings_.measurements.end() ? 0U : found->channelId;
}

unsigned InstrumentService::ActiveChannelId() const noexcept
{
    if (HasChannel(selectedChannelId_)) {
        return selectedChannelId_;
    }
    const auto owner = MeasurementChannel(display_.State().selectedMeasurement);
    if (HasChannel(owner)) {
        return owner;
    }
    for (const auto &channel : channels_) {
        if (channel.second.committed) {
            return channel.first;
        }
    }
    return 1U;
}

std::string InstrumentService::SelectedMeasurement(unsigned channelId) const
{
    const auto id = channelId == 0U ? (contextDepth_ != 0U ? contextChannelId_ : ActiveChannelId()) : channelId;
    const auto found = channels_.find(id);
    if (found == channels_.end() || !found->second.committed) {
        return {};
    }
    const auto &remembered = found->second.selectedMeasurement;
    if (MeasurementChannel(remembered) == id) {
        return remembered;
    }
    const auto first =
        std::find_if(settings_.measurements.begin(), settings_.measurements.end(), [id](const Measurement &value) {
            return value.channelId == id;
        });
    return first == settings_.measurements.end() ? std::string() : first->name;
}

void InstrumentService::RememberSelection()
{
    const auto owner = MeasurementChannel(display_.State().selectedMeasurement);
    if (HasChannel(owner)) {
        selectedChannelId_ = owner;
        channels_.at(owner).selectedMeasurement = display_.State().selectedMeasurement;
    }
}

std::vector<std::string> InstrumentService::ChannelTraceNames(unsigned channelId) const
{
    std::vector<std::string> result;
    for (const auto &measurement : settings_.measurements) {
        if (measurement.channelId == channelId) {
            result.push_back(measurement.name);
        }
    }
    return result;
}

InstrumentChannelSnapshot InstrumentService::ChannelSnapshot(unsigned channelId) const
{
    ContextScope context(const_cast<InstrumentService &>(*this), channelId);
    InstrumentChannelSnapshot result;
    result.id = channelId;
    result.name = "Ch" + std::to_string(channelId);
    result.core = core_.ReadView(channelId);
    if (!context.Valid()) {
        return result;
    }
    result.settings = settings_;
    result.settings.measurements.erase(
        std::remove_if(result.settings.measurements.begin(), result.settings.measurements.end(),
            [channelId](const Measurement &value) {
                return value.channelId != channelId;
            }),
        result.settings.measurements.end());
    result.selectedMeasurement = SelectedMeasurement(channelId);
    result.calibration = Calibration(channelId);
    result.stimulusZoom = StimulusZoom(channelId);
    if (result.calibration.compatible) {
        result.correction = calibration_.Correction(Identity(), activePlan_);
    }
    for (const auto &task : tasks_) {
        if (task.channelId == channelId) {
            result.pending = true;
            result.finitePending = result.finitePending || task.measurement;
        }
    }
    return result;
}

Operation InstrumentService::CreateChannel(ChannelCreationOptions options, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner)
{
    ContextScope context(*this, options.sourceChannelId);
    if (!context.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Source channel does not exist", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::CHANNEL_CREATE;
    task.copyChannel = options.copyChannel;
    task.newDiagram = options.newDiagram;
    task.expectedRevision = expectedRevision;
    task.expectedDisplayRevision = expectedDisplayRevision;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::SelectChannel(
    unsigned channelId, std::uint64_t expectedDisplayRevision, const std::string &owner)
{
    ContextScope context(*this, channelId);
    if (!context.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Selected channel does not exist", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::CHANNEL_SELECT;
    task.expectedDisplayRevision = expectedDisplayRevision;
    return Enqueue(std::move(task), owner);
}

Operation InstrumentService::DeleteChannel(
    unsigned channelId, std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision, const std::string &owner)
{
    ContextScope context(*this, channelId);
    if (!context.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Deleted channel does not exist", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::CHANNEL_DELETE;
    task.expectedRevision = expectedRevision;
    task.expectedDisplayRevision = expectedDisplayRevision;
    return Enqueue(std::move(task), owner);
}

bool InstrumentService::PrepareChannelCreation(Detail::ServiceTask &task)
{
    if (channels_.size() >= MAX_CHANNELS || nextChannelId_ == std::numeric_limits<unsigned>::max()) {
        Finish(ChannelResult(false, "Channel capacity exhausted (maximum 16 live channels)"));
        return false;
    }
    const auto &catalog = settings_.measurements;
    std::vector<Measurement> traces;
    if (task.copyChannel) {
        for (const auto &measurement : catalog) {
            if (measurement.channelId == contextChannelId_) {
                traces.push_back(measurement);
            }
        }
    } else {
        Measurement trace;
        trace.SetParameter(profile_.portCount == 1U ? "S11" : "S21");
        traces.push_back(std::move(trace));
    }
    if (traces.empty()) {
        Finish(ChannelResult(false, "Source channel has no traces to copy"));
        return false;
    }
    if (catalog.size() + traces.size() > VnaDisplay::MAX_DISPLAY_TRACES) {
        Finish(ChannelResult(false, "Channel needs a trace and the instrument supports at most 128 traces"));
        return false;
    }
    task.targetChannelId = nextChannelId_++;
    task.candidate = task.copyChannel ? settings_ : InstrumentSettings{};
    if (!task.copyChannel) {
        task.candidate.startHz = profile_.defaultStartHz;
        task.candidate.stopHz = profile_.defaultStopHz;
        task.candidate.points = profile_.defaultPoints;
        task.candidate.ifBandwidthHz = profile_.defaultIfBandwidthHz;
        task.candidate.powerDbm = profile_.defaultPowerDbm;
        task.candidate.sweepMode = SweepMode::CONTINUOUS;
    }
    task.candidate.measurements = catalog;
    task.candidate.revision = logicalRevision_ + 1U;
    task.candidate.acquisitionRevision = 0U;
    task.calibrationCandidate = task.copyChannel ? calibration_ : CalibrationWorkspace{};
    std::size_t calibrationBytes = task.calibrationCandidate.StorageBytes() + calibration_.StorageBytes();
    for (const auto &channel : channels_) {
        if (channel.first != contextChannelId_) {
            calibrationBytes += channel.second.calibration.StorageBytes();
        }
    }
    if (calibrationBytes > MAX_INSTRUMENT_CALIBRATION_STORAGE_BYTES) {
        Finish(ChannelResult(false, "Channel copy exceeds the instrument calibration pool budget"));
        return false;
    }
    for (auto measurement : traces) {
        const auto sourceName = task.copyChannel ? measurement.name : std::string();
        measurement.name = NextTraceName(task.candidate.measurements);
        measurement.channelId = task.targetChannelId;
        task.channelTraceCopies.emplace_back(sourceName, measurement.name);
        task.channelTraceNames.push_back(measurement.name);
        task.candidate.measurements.push_back(std::move(measurement));
    }
    task.plan = MakeChannelPlan(task.candidate, task.targetChannelId);
    task.calibrationCandidate.ExtendPlan(task.plan);
    task.calibrationCandidate.PrepareForPlan(Identity(), task.plan);
    task.displayCandidate = display_;
    auto result = task.displayCandidate.Reconcile(task.candidate.measurements);
    unsigned diagram = task.newDiagram ? 0U : display_.State().selectedDiagram;
    for (std::size_t index = 0U; result.ok && index < task.channelTraceCopies.size(); ++index) {
        const auto &names = task.channelTraceCopies[index];
        const bool newDiagram = task.newDiagram && index == 0U;
        result = task.copyChannel ? task.displayCandidate.ClonePresentation(
                                        names.first, names.second, task.candidate.measurements, diagram, newDiagram)
                                  : task.displayCandidate.AddPresentation(
                                        names.second, task.candidate.measurements, diagram, newDiagram);
        diagram = task.displayCandidate.State().selectedDiagram;
    }
    const auto selectedSource = SelectedMeasurement(contextChannelId_);
    auto selected = task.channelTraceCopies.front().second;
    for (const auto &names : task.channelTraceCopies) {
        if (names.first == selectedSource) {
            selected = names.second;
        }
    }
    VnaDisplay::DisplayChange select;
    select.kind = VnaDisplay::DisplayChangeKind::MEASUREMENT_SELECT;
    select.name = selected;
    if (result.ok) {
        result = task.displayCandidate.Apply(select, task.candidate.measurements, {});
    }
    if (result.ok) {
        result = task.displayCandidate.ReconcileAxis(task.plan.frequencyHz, task.copyChannel, &task.channelTraceNames);
    }
    if (!result.ok) {
        Finish(ChannelResult(false, result.error));
        return false;
    }
    auto display = task.displayCandidate.Snapshot();
    display.revision = display_.State().revision + 1U;
    task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(display));
    ChannelState staged;
    staged.committed = false;
    staged.settings = task.candidate;
    staged.settings.measurements.clear();
    staged.calibration = task.calibrationCandidate;
    staged.calibration.SetChannelId(task.targetChannelId);
    staged.selectedMeasurement = selected;
    if (task.copyChannel) {
        staged.zoomHistory = zoomHistory_;
        for (auto &entry : staged.zoomHistory) {
            for (const auto &names : task.channelTraceCopies) {
                if (entry.traceName == names.first) {
                    entry.traceName = names.second;
                    break;
                }
            }
        }
    }
    channels_.emplace(task.targetChannelId, std::move(staged));
    task.ticket = core_.CreateChannel(task.targetChannelId);
    task.stage = Detail::ServiceTaskStage::WAITING_CHANNEL_CREATE;
    return true;
}

bool InstrumentService::PrepareChannelDeletion(Detail::ServiceTask &task)
{
    if (channels_.size() <= 1U) {
        Finish(ChannelResult(false, "Cannot delete the last channel"));
        return false;
    }
    task.candidate = settings_;
    task.candidate.measurements.erase(
        std::remove_if(task.candidate.measurements.begin(), task.candidate.measurements.end(),
            [this](const Measurement &measurement) {
                return measurement.channelId == contextChannelId_;
            }),
        task.candidate.measurements.end());
    task.displayCandidate = display_;
    auto result = task.displayCandidate.Reconcile(task.candidate.measurements);
    auto diagrams = task.displayCandidate.State().diagrams;
    for (auto iterator = diagrams.rbegin(); result.ok && iterator != diagrams.rend(); ++iterator) {
        if (iterator->measurementNames.empty() && task.displayCandidate.State().diagrams.size() > 1U) {
            result = task.displayCandidate.RemoveDiagram(iterator->number, true);
        }
    }
    if (result.ok && !task.candidate.measurements.empty()) {
        VnaDisplay::DisplayChange select;
        select.kind = VnaDisplay::DisplayChangeKind::MEASUREMENT_SELECT;
        select.name = display_.State().selectedMeasurement;
        if (std::none_of(task.candidate.measurements.begin(), task.candidate.measurements.end(),
                [&select](const Measurement &measurement) {
                    return measurement.name == select.name;
                })) {
            select.name = task.candidate.measurements.front().name;
        }
        result = task.displayCandidate.Apply(select, task.candidate.measurements, {});
    }
    if (!result.ok) {
        Finish(ChannelResult(false, result.error));
        return false;
    }
    auto display = task.displayCandidate.Snapshot();
    display.revision = display_.State().revision + 1U;
    task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(display));
    task.ticket = core_.RemoveChannel(contextChannelId_);
    task.stage = Detail::ServiceTaskStage::WAITING_CHANNEL_REMOVE;
    return true;
}

void InstrumentService::PollChannel(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        if (CalibrationCollecting()) {
            Finish(ChannelResult(false, "Channel changes are locked during calibration collection"));
            return;
        }
        if (task.expectedDisplayRevision != display_.State().revision ||
            display_.State().revision == std::numeric_limits<std::uint64_t>::max() ||
            (task.kind != Detail::ServiceTaskKind::CHANNEL_SELECT &&
                (task.expectedRevision != logicalRevision_ ||
                    logicalRevision_ == std::numeric_limits<std::uint64_t>::max()))) {
            Finish(ChannelResult(false, "Channel or display revision changed"));
            return;
        }
        if (task.kind == Detail::ServiceTaskKind::CHANNEL_SELECT) {
            VnaDisplay::DisplayChange change;
            change.kind = VnaDisplay::DisplayChangeKind::MEASUREMENT_SELECT;
            change.name = SelectedMeasurement(contextChannelId_);
            if (change.name.empty()) {
                auto state = display_.Snapshot();
                state.selectedMeasurement.clear();
                state.revision += 1U;
                display_ = VnaDisplay::DisplayWorkspace(std::move(state));
                selectedChannelId_ = contextChannelId_;
                Finish(ChannelResult(true));
                return;
            }
            const auto result = display_.Apply(change, settings_.measurements, DisplayFrame(true));
            if (result.ok) {
                RememberSelection();
            }
            Finish(ChannelResult(result.ok, result.error));
            return;
        }
        const auto view = core_.ReadView(contextChannelId_);
        if (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) {
            Finish(ChannelResult(false, "Channel changes require a healthy ready or running instrument"));
            return;
        }
        if (task.kind == Detail::ServiceTaskKind::CHANNEL_CREATE) {
            PrepareChannelCreation(task);
        } else {
            PrepareChannelDeletion(task);
        }
        return;
    }
    if (task.ticket.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const auto result = task.ticket.get();
    if (task.kind == Detail::ServiceTaskKind::CHANNEL_DELETE) {
        if (!result.ok) {
            Finish(ChannelResult(false, result.message, Detail::DEVICE_SPECIFIC_ERROR));
            return;
        }
        const auto removed = contextChannelId_;
        unsigned survivor = 0U;
        for (const auto &channel : channels_) {
            if (channel.first != removed && channel.second.committed) {
                survivor = channel.first;
                break;
            }
        }
        ActivateContext(survivor);
        settings_.measurements = std::move(task.candidate.measurements);
        display_ = std::move(task.displayCandidate);
        channels_.erase(removed);
        CommitLogicalRevision(logicalRevision_ + 1U);
        settingsViews_.erase(removed);
        RememberSelection();
        Finish(ChannelResult(true));
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_CHANNEL_REMOVE) {
        channels_.erase(task.targetChannelId);
        Finish(task.channelFailure);
        return;
    }
    if (!result.ok) {
        task.channelFailure = ChannelResult(false, result.message, Detail::DEVICE_SPECIFIC_ERROR);
        if (task.stage == Detail::ServiceTaskStage::WAITING_CHANNEL_CREATE) {
            channels_.erase(task.targetChannelId);
            Finish(task.channelFailure);
        } else {
            task.ticket = core_.RemoveChannel(task.targetChannelId);
            task.stage = Detail::ServiceTaskStage::WAITING_CHANNEL_REMOVE;
        }
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_CHANNEL_CREATE) {
        task.ticket = core_.Configure(task.plan, task.targetChannelId);
        task.stage = Detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    auto &staged = channels_.at(task.targetChannelId);
    if (task.stage == Detail::ServiceTaskStage::WAITING_CONFIGURATION) {
        staged.settings.acquisitionRevision = result.configRevision;
        staged.activePlan = task.plan;
        if (staged.settings.sweepMode != SweepMode::HOLD) {
            const auto mode = staged.settings.sweepMode == SweepMode::CONTINUOUS &&
                                      staged.settings.triggerSource == TriggerSource::IMMEDIATE
                                  ? VnaCore::RunMode::CONTINUOUS
                                  : VnaCore::RunMode::MANUAL;
            task.ticket = core_.Start(mode, task.targetChannelId);
            task.stage = Detail::ServiceTaskStage::WAITING_CHANNEL_START;
            return;
        }
    }
    const auto view = core_.ReadView(task.targetChannelId);
    if (view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED) {
        task.channelFailure = ChannelResult(false, view.error, Detail::DEVICE_SPECIFIC_ERROR);
        task.ticket = core_.RemoveChannel(task.targetChannelId);
        task.stage = Detail::ServiceTaskStage::WAITING_CHANNEL_REMOVE;
        return;
    }
    task.displayCandidate.BindAcquisitionRevision(staged.settings.acquisitionRevision, &task.channelTraceNames);
    settings_.measurements = std::move(task.candidate.measurements);
    display_ = std::move(task.displayCandidate);
    staged.committed = true;
    CommitLogicalRevision(task.candidate.revision);
    RememberSelection();
    Finish(ChannelResult(true));
}

}  // 命名空间 VnaService
