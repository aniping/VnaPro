/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
constexpr std::size_t ZOOM_HISTORY_CAPACITY = 16U;

OperationResult ZoomResult(bool ok, int code = 0, const std::string &message = {})
{
    OperationResult result;
    result.ok = ok;
    result.errorCode = code;
    result.message = message;
    return result;
}

const VnaDisplay::TracePresentation *FindTrace(const VnaDisplay::DisplayState &display, const std::string &name)
{
    const auto found =
        std::find_if(display.traces.begin(), display.traces.end(), [&name](const VnaDisplay::TracePresentation &trace) {
            return trace.name == name;
        });
    return found == display.traces.end() ? nullptr : &*found;
}

std::string TraceParameter(const InstrumentSettings &settings, const std::string &name)
{
    const auto found = std::find_if(
        settings.measurements.begin(), settings.measurements.end(), [&name](const Measurement &measurement) {
            return measurement.name == name;
        });
    return found == settings.measurements.end() ? std::string() : found->Parameter();
}

std::string ActiveTraceReason(
    const VnaDisplay::DisplayState &display, const std::string &name, const std::string &selected)
{
    const auto *trace = FindTrace(display, name);
    const auto diagram = std::find_if(
        display.diagrams.begin(), display.diagrams.end(), [&name](const VnaDisplay::DisplayDiagram &value) {
            return value.primaryMeasurement == name;
        });
    if (trace == nullptr || name != selected || diagram == display.diagrams.end() ||
        diagram->primaryMeasurement != name ||
        std::find(diagram->measurementNames.begin(), diagram->measurementNames.end(), name) ==
            diagram->measurementNames.end()) {
        return "Stimulus Zoom requires the active displayed trace";
    }
    if (!trace->visible) {
        return "Stimulus Zoom requires a visible trace";
    }
    if (VnaDisplay::TraceProjection::IsCircular(trace->format)) {
        return "Stimulus Zoom is unavailable for circular trace formats";
    }
    return {};
}

bool SameScale(const VnaDisplay::DisplayScale &left, const VnaDisplay::DisplayScale &right)
{
    return left.min == right.min && left.max == right.max && left.referencePosition == right.referencePosition;
}

std::string RestoreReason(const Detail::StimulusZoomHistoryEntry &entry, const InstrumentSettings &settings,
    const VnaDisplay::DisplayState &display)
{
    const auto *trace = FindTrace(display, entry.traceName);
    if (!entry.targetUnchanged || trace == nullptr || TraceParameter(settings, entry.traceName) != entry.parameter) {
        return "Zoom trace was removed or its measurement changed";
    }
    if (settings.startHz != entry.appliedStartHz || settings.stopHz != entry.appliedStopHz ||
        trace->format != entry.format || !SameScale(trace->scale, entry.appliedScale)) {
        return "Frequency range or zoomed trace format/scale changed after Zoom";
    }
    return {};
}

bool ClampSelection(double &minimum, double &maximum, double lower, double upper, double tolerance)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || minimum < lower - tolerance ||
        maximum > upper + tolerance) {
        return false;
    }
    minimum = std::max(lower, minimum);
    maximum = std::min(upper, maximum);
    return minimum < maximum;
}
}  // 匿名命名空间

void InstrumentService::InvalidateZoomHistory(const Detail::ServiceTask &task) noexcept
{
    const auto invalidate = [this, &task](std::deque<Detail::StimulusZoomHistoryEntry> &history, unsigned channelId) {
        for (auto &entry : history) {
            const auto matches = [&entry](const Measurement &value) {
                return value.name == entry.traceName;
            };
            const auto before = std::find_if(settings_.measurements.begin(), settings_.measurements.end(), matches);
            const auto after =
                std::find_if(task.candidate.measurements.begin(), task.candidate.measurements.end(), matches);
            if ((task.matrixPortCount != 0U && channelId == contextChannelId_) ||
                before == settings_.measurements.end() || after == task.candidate.measurements.end() ||
                before->kind != after->kind || before->receiverKind != after->receiverKind ||
                before->receiverPort != after->receiverPort || before->sourcePort != after->sourcePort) {
                entry.targetUnchanged = false;
            }
        }
    };
    invalidate(zoomHistory_, contextChannelId_);
    for (auto &channel : channels_) {
        if (channel.first != contextChannelId_ && channel.second.committed) {
            invalidate(channel.second.zoomHistory, channel.first);
        }
    }
}

StimulusZoomStatus InstrumentService::StimulusZoom(unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        StimulusZoomStatus failure;
        failure.reason = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    StimulusZoomStatus result;
    result.depth = static_cast<unsigned>(zoomHistory_.size());
    const auto view = core_.ReadView(contextChannelId_);
    if (!profileReady_ || (view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) ||
        view.configRevision != settings_.acquisitionRevision) {
        result.reason = "Stimulus Zoom requires a valid committed acquisition configuration";
        return result;
    }
    if (CalibrationCollecting()) {
        result.reason = "Stimulus Zoom is locked during calibration collection";
        return result;
    }
    if (HasMeasurement()) {
        result.reason = "Stimulus Zoom waits for the pending finite acquisition";
        return result;
    }
    const auto &display = display_.State();
    result.reason =
        ActiveTraceReason(display, SelectedMeasurement(contextChannelId_), SelectedMeasurement(contextChannelId_));
    if (result.reason.empty() && settings_.startHz >= settings_.stopHz) {
        result.reason = "Stimulus Zoom requires a nonzero frequency span";
    }
    result.available = result.reason.empty();
    if (!zoomHistory_.empty()) {
        const auto restoreReason = RestoreReason(zoomHistory_.back(), settings_, display);
        result.canZoomOut = restoreReason.empty();
        if (!restoreReason.empty()) {
            result.reason = restoreReason;
        }
    }
    return result;
}

Operation InstrumentService::ApplyStimulusZoom(StimulusZoomChange change, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId, change.traceName);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::STIMULUS_ZOOM;
    task.stimulusZoomChange = std::move(change);
    task.expectedRevision = expectedRevision;
    task.checkRevision = true;
    task.expectedDisplayRevision = expectedDisplayRevision;
    return Enqueue(std::move(task), owner);
}

bool InstrumentService::PrepareStimulusZoom(Detail::ServiceTask &task)
{
    if (task.expectedRevision != settings_.revision) {
        Finish(ZoomResult(false, Detail::SETTINGS_CONFLICT_ERROR, "Configuration revision changed"));
        return false;
    }
    if (task.expectedDisplayRevision != display_.State().revision) {
        Finish(ZoomResult(false, Detail::SETTINGS_CONFLICT_ERROR, "Display revision changed"));
        return false;
    }
    if (display_.State().revision == std::numeric_limits<std::uint64_t>::max()) {
        Finish(ZoomResult(false, Detail::SETTINGS_CONFLICT_ERROR, "Display revision exhausted"));
        return false;
    }
    auto change = task.stimulusZoomChange;
    VnaDisplay::DisplayScale targetScale;
    if (change.action == StimulusZoomAction::OUT) {
        const auto reason = zoomHistory_.empty() ? "No previous Stimulus Zoom"
                                                 : RestoreReason(zoomHistory_.back(), settings_, display_.State());
        if (!reason.empty()) {
            Finish(ZoomResult(false, Detail::SETTINGS_CONFLICT_ERROR, reason));
            return false;
        }
        const auto &previous = zoomHistory_.back();
        change.traceName = previous.traceName;
        change.startHz = previous.previousStartHz;
        change.stopHz = previous.previousStopHz;
        targetScale = previous.previousScale;
    } else if (change.action == StimulusZoomAction::APPLY) {
        const auto reason =
            ActiveTraceReason(display_.State(), change.traceName, SelectedMeasurement(contextChannelId_));
        if (!reason.empty()) {
            Finish(ZoomResult(false, Detail::SETTINGS_CONFLICT_ERROR, reason));
            return false;
        }
        const auto *trace = FindTrace(display_.State(), change.traceName);
        if (trace->format == VnaDisplay::DisplayFormat::LOG_MAGNITUDE && (change.min <= 0.0 || change.max <= 0.0)) {
            Finish(ZoomResult(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Log magnitude Zoom requires positive limits"));
            return false;
        }
        const double frequencyTolerance = 0.5 + 64.0 * std::numeric_limits<double>::epsilon() *
                                                    std::max(std::abs(settings_.startHz), std::abs(settings_.stopHz));
        const double scaleTolerance =
            64.0 * std::numeric_limits<double>::epsilon() *
            std::max({std::abs(trace->scale.min), std::abs(trace->scale.max), trace->scale.max - trace->scale.min});
        if (!ClampSelection(change.startHz, change.stopHz, settings_.startHz, settings_.stopHz, frequencyTolerance) ||
            !ClampSelection(change.min, change.max, trace->scale.min, trace->scale.max, scaleTolerance)) {
            Finish(
                ZoomResult(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Zoom selection must lie within the current axes"));
            return false;
        }
        change.startHz = std::round(change.startHz);
        change.stopHz = std::round(change.stopHz);
        targetScale = trace->scale;
        targetScale.min = change.min;
        targetScale.max = change.max;
    } else {
        Finish(ZoomResult(false, Detail::INVALID_PARAMETER_ERROR, "Unsupported Stimulus Zoom action"));
        return false;
    }
    if (!std::isfinite(change.startHz) || !std::isfinite(change.stopHz) || change.startHz >= change.stopHz ||
        change.startHz < profile_.minFrequencyHz || change.stopHz > profile_.maxFrequencyHz) {
        Finish(ZoomResult(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Zoom frequency range is outside instrument limits"));
        return false;
    }
    task.mutate = [change](InstrumentSettings &candidate) {
        candidate.startHz = change.startHz;
        candidate.stopHz = change.stopHz;
        return ZoomResult(true);
    };
    if (!PrepareConfiguration(task)) {
        return false;
    }
    const auto *before = FindTrace(display_.State(), change.traceName);
    if (change.startHz == settings_.startHz && change.stopHz == settings_.stopHz &&
        SameScale(targetScale, before->scale)) {
        Finish(ZoomResult(true));
        return false;
    }
    // 轴协调和纵轴设置属于同一笔 Zoom，以一个显示版本发布全部候选变化。
    auto candidate = task.displayCandidate.Snapshot();
    candidate.revision = display_.State().revision;
    task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(candidate));
    VnaDisplay::DisplayChange scale;
    scale.kind = VnaDisplay::DisplayChangeKind::SCALE;
    scale.name = change.traceName;
    scale.scale.hasMin = true;
    scale.scale.hasMax = true;
    scale.scale.hasReferencePosition = true;
    scale.scale.min = targetScale.min;
    scale.scale.max = targetScale.max;
    scale.scale.referencePosition = targetScale.referencePosition;
    const auto applied = task.displayCandidate.Apply(scale, task.candidate.measurements, {});
    if (!applied.ok) {
        Finish(ZoomResult(false, Detail::DATA_OUT_OF_RANGE_ERROR, applied.error));
        return false;
    }
    candidate = task.displayCandidate.Snapshot();
    // 尺度已通过统一规则验证；保留原始边界，避免 log10/pow 往返使历史值发生 ULP 漂移。
    const auto target = std::find_if(
        candidate.traces.begin(), candidate.traces.end(), [&change](const VnaDisplay::TracePresentation &trace) {
            return trace.name == change.traceName;
        });
    target->scale = targetScale;
    candidate.revision = display_.State().revision + 1U;
    task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(candidate));
    task.zoomHistoryCandidate = zoomHistory_;
    if (change.action == StimulusZoomAction::OUT) {
        task.zoomHistoryCandidate.pop_back();
    } else {
        Detail::StimulusZoomHistoryEntry entry;
        entry.traceName = change.traceName;
        entry.parameter = TraceParameter(settings_, change.traceName);
        entry.format = before->format;
        entry.previousStartHz = settings_.startHz;
        entry.previousStopHz = settings_.stopHz;
        entry.previousScale = before->scale;
        entry.appliedStartHz = change.startHz;
        entry.appliedStopHz = change.stopHz;
        entry.appliedScale = FindTrace(task.displayCandidate.State(), change.traceName)->scale;
        if (task.zoomHistoryCandidate.size() == ZOOM_HISTORY_CAPACITY) {
            task.zoomHistoryCandidate.pop_front();
        }
        task.zoomHistoryCandidate.push_back(std::move(entry));
    }
    return true;
}

void InstrumentService::PollStimulusZoom(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        if (!PrepareStimulusZoom(task)) {
            return;
        }
        if (task.candidate.startHz == settings_.startHz && task.candidate.stopHz == settings_.stopHz) {
            // 仅纵轴变化无需重新下发计划，Manual 的当前完整帧与配置版本继续有效。
            task.candidate.revision = settings_.revision;
            zoomHistory_ = std::move(task.zoomHistoryCandidate);
            CommitConfiguration(task);
            return;
        }
        task.ticket = core_.Configure(task.plan, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    if (task.ticket.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const auto configured = task.ticket.get();
    if (!configured.ok) {
        Finish(ZoomResult(false, Detail::DEVICE_SPECIFIC_ERROR, configured.message));
        return;
    }
    task.candidate.acquisitionRevision = configured.configRevision;
    activePlan_ = std::move(task.plan);
    zoomHistory_ = std::move(task.zoomHistoryCandidate);
    CommitConfiguration(task);
}

}  // 命名空间 VnaService
