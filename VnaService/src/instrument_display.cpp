/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <utility>

#include "vna_core/frame_layout.h"
#include "vna_display/diagram_collection.h"
#include "vna_measurement/measurement_processor.h"
#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
OperationResult DisplayResult(const VnaDisplay::DisplayResult &display)
{
    OperationResult result;
    result.ok = display.ok;
    result.errorCode = display.ok ? 0 : Detail::DATA_OUT_OF_RANGE_ERROR;
    result.message = display.error;
    return result;
}

std::string NextMeasurementName(const std::vector<Measurement> &catalog)
{
    // 遵循 ZNB 的自动命名：使用当前目录中最小的空闲正整数编号。
    for (std::size_t index = 1U; index <= catalog.size() + 1U; ++index) {
        const auto name = "Trc" + std::to_string(index);
        const auto existing = std::find_if(catalog.begin(), catalog.end(), [&name](const Measurement &item) {
            return item.name == name;
        });
        if (existing == catalog.end()) {
            return name;
        }
    }
    return {};
}
}  // 匿名命名空间

const VnaDisplay::DisplayState &InstrumentService::Display() const noexcept
{
    return display_.State();
}

bool InstrumentService::PrepareDisplayConfiguration(Detail::ServiceTask &task)
{
    // 显示候选预先验证；核心成功后以无失败的移动同时提交目录与显示。
    task.displayCandidate = display_;
    if (task.resetInstrumentDisplay) {
        VnaDisplay::DisplayState defaults;
        defaults.revision = display_.State().revision;
        for (const auto &measurement : task.candidate.measurements) {
            defaults.displayedNames.push_back(measurement.name);
        }
        if (!defaults.displayedNames.empty()) {
            defaults.selectedMeasurement = defaults.displayedNames.front();
        }
        task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(defaults));
    }
    VnaDisplay::DisplayResult result;
    result.ok = true;
    if ((task.selectNewMeasurement || task.selectClonedMeasurement || task.deletedDiagram != 0U ||
            task.matrixPortCount != 0U) &&
        task.expectedDisplayRevision != display_.State().revision) {
        result.ok = false;
        result.error = "Display revision changed";
    }
    if (result.ok && task.matrixPortCount != 0U) {
        std::vector<Measurement> matrix;
        std::vector<Measurement> others;
        for (const auto &measurement : task.candidate.measurements) {
            (measurement.channelId == contextChannelId_ ? matrix : others).push_back(measurement);
        }
        if (others.empty()) {
            result = task.displayCandidate.ReplaceSParameterMatrix(matrix, task.matrixPortCount);
        } else {
            // Keep the other channels before reusing this channel's names.
            result = task.displayCandidate.Reconcile(others);
            auto diagrams = task.displayCandidate.State().diagrams;
            for (auto item = diagrams.rbegin(); result.ok && item != diagrams.rend(); ++item) {
                if (item->measurementNames.empty() && task.displayCandidate.State().diagrams.size() > 1U) {
                    result = task.displayCandidate.RemoveDiagram(item->number, true);
                }
            }
            if (result.ok) {
                result = task.displayCandidate.Reconcile(task.candidate.measurements);
            }
            VnaDisplay::DisplayWorkspace fresh;
            if (result.ok) {
                result = fresh.ReplaceSParameterMatrix(matrix, task.matrixPortCount);
            }
            for (const auto &measurement : matrix) {
                if (result.ok) {
                    result =
                        task.displayCandidate.AddPresentation(measurement.name, task.candidate.measurements, 0U, true);
                }
            }
            if (result.ok) {
                auto display = task.displayCandidate.Snapshot();
                for (auto &trace : display.traces) {
                    const auto replacement = std::find_if(fresh.State().traces.begin(), fresh.State().traces.end(),
                        [&trace](const VnaDisplay::TracePresentation &value) {
                            return value.name == trace.name;
                        });
                    if (replacement != fresh.State().traces.end()) {
                        trace = *replacement;
                    }
                }
                display.selectedMeasurement = matrix.front().name;
                for (const auto &diagram : display.diagrams) {
                    if (diagram.primaryMeasurement == matrix.front().name) {
                        display.selectedDiagram = diagram.number;
                    }
                }
                display.revision = display_.State().revision + 1U;
                task.displayCandidate = VnaDisplay::DisplayWorkspace(std::move(display));
            }
        }
    }
    if (task.matrixPortCount != 0U) {
        if (!result.ok) {
            auto failure = DisplayResult(result);
            failure.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
            Finish(std::move(failure));
        }
        // 整组从新默认显示建立，不协调或继承旧同名迹线的格式、刻度和标记。
        return result.ok;
    }
    if (result.ok && task.deletedDiagram != 0U) {
        result = task.displayCandidate.RemoveDiagram(task.deletedDiagram, true);
    }
    if (result.ok && task.kind == Detail::ServiceTaskKind::RESET && !task.resetInstrumentDisplay) {
        auto others = task.candidate.measurements;
        others.erase(std::remove_if(others.begin(), others.end(),
                         [this](const Measurement &measurement) {
                             return measurement.channelId == contextChannelId_;
                         }),
            others.end());
        result = task.displayCandidate.Reconcile(others);
    }
    if (result.ok) {
        result = task.displayCandidate.Reconcile(task.candidate.measurements);
    }
    if (result.ok && task.kind == Detail::ServiceTaskKind::RESET && !task.resetInstrumentDisplay) {
        unsigned emptyDiagram = 0U;
        for (const auto &diagram : task.displayCandidate.State().diagrams) {
            if (diagram.measurementNames.empty()) {
                emptyDiagram = diagram.number;
                break;
            }
        }
        result = task.displayCandidate.AddPresentation(
            task.channelTraceNames.front(), task.candidate.measurements, emptyDiagram, emptyDiagram == 0U);
        if (result.ok) {
            // 新增图窗默认切到 GRID；通道复位仍保留仪器原有的全局显示模式。
            VnaDisplay::DisplayChange layout;
            layout.kind = VnaDisplay::DisplayChangeKind::LAYOUT;
            layout.layout = display_.State().layout;
            result = task.displayCandidate.Apply(layout, task.candidate.measurements, {});
        }
    }
    // 改变测量量时采用该量的默认格式；仅改变显示格式不触碰采集需求。
    for (const auto &measurement : task.candidate.measurements) {
        const auto old = std::find_if(
            settings_.measurements.begin(), settings_.measurements.end(), [&measurement](const Measurement &value) {
                return value.name == measurement.name;
            });
        if (result.ok && old != settings_.measurements.end() && old->Parameter() != measurement.Parameter()) {
            VnaDisplay::DisplayChange format;
            format.kind = VnaDisplay::DisplayChangeKind::FORMAT;
            format.name = measurement.name;
            format.format = VnaDisplay::DisplayFormat::DB;
            result = task.displayCandidate.Apply(format, task.candidate.measurements, {});
        }
    }
    if (result.ok) {
        result = task.displayCandidate.ReconcileAxis(
            task.plan.frequencyHz, task.plan.frequencyHz == activePlan_.frequencyHz, &task.channelTraceNames);
    }
    if (result.ok && task.selectClonedMeasurement) {
        result = task.displayCandidate.ClonePresentation(task.cloneSourceName, task.candidate.measurements.back().name,
            task.candidate.measurements, task.targetDiagram, task.newDiagram, task.dropPosition);
    } else if (result.ok && task.selectNewMeasurement) {
        result = task.displayCandidate.AddPresentation(task.candidate.measurements.back().name,
            task.candidate.measurements, task.targetDiagram, task.newDiagram, task.dropPosition);
    }
    if (!result.ok) {
        auto failure = DisplayResult(result);
        failure.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
        Finish(std::move(failure));
        return false;
    }
    return true;
}

VnaDisplay::FrameContext InstrumentService::DisplayFrame(bool reachedBarrier) const
{
    const auto view = core_.ReadView(contextChannelId_);
    VnaDisplay::FrameContext result;
    result.frame = view.frame;
    result.acquisitionRevision = settings_.acquisitionRevision;
    result.runId = view.runId;
    result.frequencyHz = &activePlan_.frequencyHz;
    result.calibration = calibration_.Correction(Identity(), activePlan_);
    // 当前 DISPLAY 已到队首，排在其后的有限采集不能反向阻止本次读旧完整帧。
    result.current = profileReady_ && (reachedBarrier || !HasMeasurement()) && view.frame &&
                     view.state != VnaCore::CoreState::FAULTED && view.state != VnaCore::CoreState::CLOSED &&
                     view.configRevision == settings_.acquisitionRevision &&
                     view.frame->configRevision == settings_.acquisitionRevision && view.frame->runId == view.runId;
    return result;
}

Operation InstrumentService::ApplyDisplay(VnaDisplay::DisplayChange change, std::uint64_t expectedDisplayRevision,
    const std::string &owner, unsigned channelId)
{
    const bool traceAction =
        change.kind == VnaDisplay::DisplayChangeKind::SELECT ||
        change.kind == VnaDisplay::DisplayChangeKind::MEASUREMENT_SELECT ||
        (change.kind == VnaDisplay::DisplayChangeKind::DISPLAYED && !change.hasDisplayedNames) ||
        change.kind == VnaDisplay::DisplayChangeKind::FORMAT || change.kind == VnaDisplay::DisplayChangeKind::SCALE ||
        change.kind == VnaDisplay::DisplayChangeKind::AUTOSCALE ||
        change.kind == VnaDisplay::DisplayChangeKind::SMITH_RADIUS ||
        change.kind == VnaDisplay::DisplayChangeKind::VISIBLE || change.kind == VnaDisplay::DisplayChangeKind::MARKER ||
        change.kind == VnaDisplay::DisplayChangeKind::MARKER_INFO_LOCATION ||
        change.kind == VnaDisplay::DisplayChangeKind::TRACE_MOVE;
    if (traceAction && change.hasSlot && change.name.empty()) {
        const auto *diagram = VnaDisplay::DiagramCollection::Find(
            display_.State(), change.diagram == 0U ? display_.State().selectedDiagram : change.diagram);
        const auto *name =
            diagram == nullptr ? nullptr : VnaDisplay::DiagramCollection::NameAtSlot(*diagram, change.slot);
        if (name == nullptr) {
            return Rejected(Detail::INVALID_PARAMETER_ERROR, "Display trace slot does not exist", owner);
        }
        change.name = *name;
    }
    ContextScope channelScope(*this, channelId, change.name);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::DISPLAY;
    if (traceAction && change.name.empty() && !change.hasSlot) {
        change.name = SelectedMeasurement(contextChannelId_);
    }
    task.displayChange = std::move(change);
    task.expectedDisplayRevision = expectedDisplayRevision;
    task.checkDisplayRevision = expectedDisplayRevision != 0U || owner != "SCPI";
    return Enqueue(std::move(task), owner);
}

void InstrumentService::PollDisplay(Detail::ServiceTask &task)
{
    if (task.checkDisplayRevision && task.expectedDisplayRevision != display_.State().revision) {
        OperationResult failure;
        failure.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
        failure.message = "Display revision changed";
        Finish(std::move(failure));
        return;
    }
    auto candidate = display_;
    const auto applied = candidate.Apply(task.displayChange, settings_.measurements, DisplayFrame(true));
    if (applied.ok && CalibrationCollecting() &&
        MeasurementChannel(candidate.State().selectedMeasurement) != ActiveChannelId()) {
        Finish(DisplayResult({false, false, "Channel selection is locked during calibration collection"}));
        return;
    }
    if (applied.ok) {
        display_ = std::move(candidate);
        RememberSelection();
    }
    Finish(DisplayResult(applied));
}

VnaDisplay::ProjectedTrace InstrumentService::DisplayData(const std::string &name, unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId, name);
    if (!channelScope.Valid()) {
        VnaDisplay::ProjectedTrace failure;
        failure.error = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    VnaDisplay::ProjectedTrace result;
    const auto context = DisplayFrame();
    if (!context.current) {
        result.error = "No complete measurement for the current configuration";
        return result;
    }
    const auto &state = display_.State();
    const auto selected = name.empty() ? SelectedMeasurement(contextChannelId_) : name;
    const auto measurement = std::find_if(
        settings_.measurements.begin(), settings_.measurements.end(), [&selected](const Measurement &item) {
            return item.name == selected;
        });
    const auto presentation =
        std::find_if(state.traces.begin(), state.traces.end(), [&selected](const VnaDisplay::TracePresentation &item) {
            return item.name == selected;
        });
    if (measurement == settings_.measurements.end() || presentation == state.traces.end()) {
        result.error = "No measurement selected";
        return result;
    }
    const auto trace =
        VnaMeasurement::MeasurementProcessor::Calculate(*context.frame, *measurement, context.calibration.get());
    return VnaDisplay::TraceProjection::Calculate(
        *presentation, trace, context.frame->plan.frequencyHz, *measurement, context.acquisitionRevision);
}

Operation InstrumentService::CloneMeasurement(const std::string &sourceName, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner, TraceCreationOptions options, unsigned channelId)
{
    ContextScope channelScope(*this, channelId, sourceName);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (!options.parameter.empty()) {
        return Rejected(Detail::INVALID_PARAMETER_ERROR, "Clone must inherit its source measurement parameter", owner);
    }
    const auto source = sourceName.empty() ? SelectedMeasurement(contextChannelId_) : sourceName;
    return CreateMeasurement(source, expectedRevision, expectedDisplayRevision, owner, options);
}

Operation InstrumentService::AddMeasurement(std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision,
    const std::string &owner, TraceCreationOptions options, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    return CreateMeasurement({}, expectedRevision, expectedDisplayRevision, owner, options);
}

Operation InstrumentService::CreateAllSParameters(
    std::uint64_t expectedRevision, std::uint64_t expectedDisplayRevision, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    auto operation = ConfigureMeasurements(
        [this](InstrumentSettings &candidate) {
            OperationResult result;
            const unsigned ports = profile_.portCount;
            if (ports == 0U || ports > VnaCore::PHYSICAL_PORT_COUNT || ports * ports > VnaDisplay::MAX_DISPLAY_TRACES) {
                result.errorCode = Detail::DATA_OUT_OF_RANGE_ERROR;
                result.message = "S-parameter matrix exceeds instrument capacity";
                return result;
            }
            candidate.measurements.erase(std::remove_if(candidate.measurements.begin(), candidate.measurements.end(),
                                             [this](const Measurement &value) {
                                                 return value.channelId == contextChannelId_;
                                             }),
                candidate.measurements.end());
            if (candidate.measurements.size() + ports * ports > VnaDisplay::MAX_DISPLAY_TRACES) {
                result.errorCode = Detail::DATA_OUT_OF_RANGE_ERROR;
                result.message = "S-parameter matrix exceeds the global 128 trace capacity";
                return result;
            }
            for (unsigned receiver = 1U; receiver <= ports; ++receiver) {
                for (unsigned source = 1U; source <= ports; ++source) {
                    Measurement measurement;
                    measurement.name = NextMeasurementName(candidate.measurements);
                    measurement.channelId = contextChannelId_;
                    if (!measurement.SetParameter("S" + std::to_string(receiver) + std::to_string(source)) ||
                        !ValidMeasurement(measurement, profile_)) {
                        result.errorCode = Detail::DATA_OUT_OF_RANGE_ERROR;
                        result.message = "Unsupported matrix measurement definition";
                        return result;
                    }
                    candidate.measurements.push_back(std::move(measurement));
                }
            }
            result.ok = true;
            return result;
        },
        expectedRevision, owner);
    if (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        auto &task = tasks_.back();
        task.matrixPortCount = profile_.portCount;
        // 整组替换始终要求双版本，内部 SCPI 所有者也不能用零版本跳过校验。
        task.checkRevision = true;
        task.expectedDisplayRevision = expectedDisplayRevision;
    }
    return operation;
}

Operation InstrumentService::CreateMeasurement(const std::string &source, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner, TraceCreationOptions options)
{
    auto operation = ConfigureMeasurements(
        [this, source, options](InstrumentSettings &candidate) {
            OperationResult result;
            const auto measurement = std::find_if(
                candidate.measurements.begin(), candidate.measurements.end(), [&source](const Measurement &item) {
                    return item.name == source;
                });
            if ((!source.empty() && measurement == candidate.measurements.end()) ||
                candidate.measurements.size() >= VnaDisplay::MAX_DISPLAY_TRACES) {
                result.errorCode = Detail::INVALID_PARAMETER_ERROR;
                result.message = "Clone source is missing or measurement catalog is full";
                return result;
            }
            Measurement cloned;
            cloned.channelId = contextChannelId_;
            if (source.empty()) {
                const auto parameter =
                    options.parameter.empty() ? (profile_.portCount == 1U ? "S11" : "S21") : options.parameter;
                if (!cloned.SetParameter(parameter)) {
                    result.errorCode = Detail::INVALID_PARAMETER_ERROR;
                    result.message = "Unsupported measurement parameter";
                    return result;
                }
            } else {
                cloned = *measurement;
            }
            cloned.name = NextMeasurementName(candidate.measurements);
            // 创建与原位更新共享能力检查，禁止指定参数绕过启动端口数量。
            if (!ValidMeasurement(cloned, profile_)) {
                result.errorCode = Detail::DATA_OUT_OF_RANGE_ERROR;
                result.message = "Unsupported measurement definition";
                return result;
            }
            candidate.measurements.push_back(std::move(cloned));
            result.ok = true;
            return result;
        },
        expectedRevision, owner);
    if (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        auto &task = tasks_.back();
        task.selectClonedMeasurement = !source.empty();
        task.selectNewMeasurement = source.empty();
        task.cloneSourceName = source;
        task.targetDiagram = options.diagram;
        task.newDiagram = options.newDiagram;
        task.dropPosition = options.position;
        task.expectedDisplayRevision = expectedDisplayRevision;
    }
    return operation;
}

Operation InstrumentService::DeleteDiagram(unsigned diagram, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    auto operation = ConfigureMeasurements(
        [this, diagram](InstrumentSettings &candidate) {
            OperationResult result;
            const auto &state = display_.State();
            const auto *window = VnaDisplay::DiagramCollection::Find(state, diagram);
            if (window == nullptr || state.diagrams.size() <= 1U) {
                result.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
                result.message = "Cannot delete a missing or the last diagram";
                return result;
            }
            const auto &names = window->measurementNames;
            candidate.measurements.erase(std::remove_if(candidate.measurements.begin(), candidate.measurements.end(),
                                             [&names](const Measurement &measurement) {
                                                 return std::find(names.begin(), names.end(), measurement.name) !=
                                                        names.end();
                                             }),
                candidate.measurements.end());
            if (candidate.measurements.empty()) {
                result.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
                result.message = "Cannot delete the last measurement";
                return result;
            }
            result.ok = true;
            return result;
        },
        expectedRevision, owner);
    if (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        auto &task = tasks_.back();
        task.deletedDiagram = diagram;
        task.expectedDisplayRevision = expectedDisplayRevision;
    }
    return operation;
}

}  // 命名空间 VnaService
