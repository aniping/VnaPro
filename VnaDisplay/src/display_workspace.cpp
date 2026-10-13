/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/display_workspace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "marker_resolver.h"
#include "scale_calculator.h"
#include "vna_core/frame_layout.h"
#include "vna_display/diagram_collection.h"
#include "vna_display/trace_projection.h"
#include "vna_measurement/measurement_processor.h"

namespace VnaDisplay {
namespace {
constexpr double MIN_CIRCULAR_RADIUS = 0.01;
constexpr double MIN_ANNOTATION_FONT_SCALE = 80.0;
constexpr double MAX_ANNOTATION_FONT_SCALE = 170.0;

DisplayResult Success(bool changed = false)
{
    DisplayResult result;
    result.ok = true;
    result.changed = changed;
    return result;
}

DisplayResult Failure(const std::string &error)
{
    DisplayResult result;
    result.error = error;
    return result;
}

const VnaMeasurement::Measurement *FindMeasurement(
    const std::vector<VnaMeasurement::Measurement> &catalog, const std::string &name)
{
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&name](const VnaMeasurement::Measurement &item) {
        return item.name == name;
    });
    return found == catalog.end() ? nullptr : &*found;
}

TracePresentation *FindPresentation(DisplayState &state, const std::string &name)
{
    const auto found = std::find_if(state.traces.begin(), state.traces.end(), [&name](const TracePresentation &item) {
        return item.name == name;
    });
    return found == state.traces.end() ? nullptr : &*found;
}

bool EqualTrace(const TracePresentation &left, const TracePresentation &right)
{
    return left.name == right.name && left.format == right.format && left.cartesianFormat == right.cartesianFormat &&
           left.scale.min == right.scale.min && left.scale.max == right.scale.max &&
           left.scale.referencePosition == right.scale.referencePosition && left.smithRadius == right.smithRadius &&
           left.visible == right.visible && left.markerInfoLocation == right.markerInfoLocation &&
           left.markers.SameSettings(right.markers);
}

bool EqualState(const DisplayState &left, const DisplayState &right)
{
    return left.selectedMeasurement == right.selectedMeasurement && left.selectedDiagram == right.selectedDiagram &&
           left.layout == right.layout && left.gridColumns == right.gridColumns &&
           left.traces.size() == right.traces.size() && left.infoTable.visible == right.infoTable.visible &&
           left.infoTable.position == right.infoTable.position &&
           left.annotations.activeTraceOnly == right.annotations.activeTraceOnly &&
           left.annotations.stimulusInfoVisible == right.annotations.stimulusInfoVisible &&
           left.annotations.fontScale == right.annotations.fontScale && left.diagrams.size() == right.diagrams.size() &&
           std::equal(left.traces.begin(), left.traces.end(), right.traces.begin(), EqualTrace) &&
           std::equal(left.diagrams.begin(), left.diagrams.end(), right.diagrams.begin(),
               [](const DisplayDiagram &a, const DisplayDiagram &b) {
                   return a.number == b.number && a.measurementNames == b.measurementNames &&
                          a.primaryMeasurement == b.primaryMeasurement && a.traceSlots == b.traceSlots &&
                          a.markerInfoVisible == b.markerInfoVisible;
               });
}

DisplayResult SelectTrace(DisplayState &state, const std::string &name, const DisplayChange &change)
{
    if (!change.hasSlot && change.diagram == 0U) {
        for (auto &diagram : state.diagrams) {
            if (std::find(diagram.measurementNames.begin(), diagram.measurementNames.end(), name) !=
                diagram.measurementNames.end()) {
                diagram.primaryMeasurement = name;
                return DiagramCollection::Select(state, diagram.number);
            }
        }
    }
    return DiagramCollection::Feed(state, name, change.diagram, change.hasSlot ? change.slot : 0U);
}

DisplayResult ReplaceDisplayed(
    DisplayState &state, const std::vector<std::string> &names, const std::vector<VnaMeasurement::Measurement> &catalog)
{
    if (names.size() > MAX_DISPLAY_TRACES) {
        return Failure("Displayed trace capacity exhausted");
    }
    std::set<std::string> seen;
    for (const auto &name : names) {
        if (FindMeasurement(catalog, name) == nullptr || !seen.insert(name).second) {
            return Failure("Unknown or duplicate displayed measurement");
        }
    }
    const auto *previousDiagram = DiagramCollection::Find(state, state.selectedDiagram);
    const bool markerInfoVisible = previousDiagram == nullptr || previousDiagram->markerInfoVisible;
    state.diagrams = {{1U, names, state.selectedMeasurement}};
    state.gridColumns = 0U;
    state.diagrams.front().markerInfoVisible = markerInfoVisible;
    state.selectedDiagram = 1U;
    if (!names.empty() && seen.count(state.selectedMeasurement) == 0U) {
        state.selectedMeasurement = names.front();
    }
    return Success();
}

DisplayResult ApplyTrace(TracePresentation &presentation, const DisplayChange &change,
    const VnaMeasurement::Measurement &measurement, const FrameContext &context)
{
    std::string error;
    switch (change.kind) {
        case DisplayChangeKind::FORMAT:
            if (DisplayFormatName(change.format) == nullptr) {
                return Failure("Display format is not supported");
            }
            if (presentation.format != change.format) {
                presentation.format = change.format;
                if (!TraceProjection::IsCircular(change.format)) {
                    presentation.cartesianFormat = change.format;
                }
                // 格式修改只替换本迹线默认尺度，不改变归属或扫描计划。
                presentation.scale = TraceProjection::DefaultScale(change.format);
                presentation.smithRadius = 1.0;
            }
            return Success();
        case DisplayChangeKind::SCALE:
            return Detail::ScaleCalculator::Apply(presentation.scale, change.scale, error, presentation.format)
                       ? Success()
                       : Failure(error);
        case DisplayChangeKind::AUTOSCALE: {
            if (!context.current || !context.frame || context.frame->configRevision != context.acquisitionRevision ||
                context.frame->runId != context.runId) {
                return Failure("Autoscale requires a current complete frame");
            }
            const auto trace = VnaMeasurement::MeasurementProcessor::Calculate(*context.frame, measurement);
            return TraceProjection::Autoscale(trace, presentation.format, presentation.scale, presentation.smithRadius,
                       context.frame->plan.frequencyHz)
                       ? Success()
                       : Failure("Autoscale does not have a finite representable range");
        }
        case DisplayChangeKind::SMITH_RADIUS:
            if (!std::isfinite(change.smithRadius) || change.smithRadius < MIN_CIRCULAR_RADIUS) {
                return Failure("Circular radius must be finite and at least 0.01");
            }
            presentation.smithRadius = change.smithRadius;
            return Success();
        case DisplayChangeKind::VISIBLE:
            presentation.visible = change.visible;
            return Success();
        case DisplayChangeKind::MARKER:
            return presentation.markers.Apply(change.marker, context, error) ? Success() : Failure(error);
        case DisplayChangeKind::MARKER_INFO_LOCATION:
            if (MarkerInfoLocationName(change.markerInfoLocation) == nullptr) {
                return Failure("Marker info location is not supported");
            }
            presentation.markerInfoLocation = change.markerInfoLocation;
            return Success();
        default:
            return Failure("Display operation is not supported");
    }
}

DisplayResult ApplyInfoTable(DisplayState &state, const InfoTableChange &change)
{
    if (!change.hasVisible && !change.hasPosition) {
        return Failure("Info table change requires visibility or position");
    }
    if (change.hasPosition && InfoTablePositionName(change.settings.position) == nullptr) {
        return Failure("Info table position is not supported");
    }
    if (change.hasVisible) {
        state.infoTable.visible = change.settings.visible;
    }
    if (change.hasPosition) {
        state.infoTable.position = change.settings.position;
    }
    return Success();
}

DisplayResult ApplyAnnotations(DisplayState &state, const AnnotationChange &change)
{
    if (!change.hasActiveTraceOnly && !change.hasStimulusInfoVisible && !change.hasFontScale) {
        return Failure("Annotation change requires at least one setting");
    }
    if (change.hasFontScale &&
        (!std::isfinite(change.settings.fontScale) || change.settings.fontScale < MIN_ANNOTATION_FONT_SCALE ||
            change.settings.fontScale > MAX_ANNOTATION_FONT_SCALE)) {
        return Failure("Annotation font scale must be between 80 and 170 percent");
    }
    if (change.hasActiveTraceOnly) {
        state.annotations.activeTraceOnly = change.settings.activeTraceOnly;
    }
    if (change.hasStimulusInfoVisible) {
        state.annotations.stimulusInfoVisible = change.settings.stimulusInfoVisible;
    }
    if (change.hasFontScale) {
        state.annotations.fontScale = change.settings.fontScale;
    }
    return Success();
}

DisplayResult ReconcileCandidate(DisplayState &candidate, const std::vector<VnaMeasurement::Measurement> &catalog)
{
    if (catalog.size() > MAX_DISPLAY_TRACES) {
        return Failure("Measurement catalog capacity exhausted");
    }
    const bool initial = candidate.traces.empty();
    std::set<std::string> seen;
    std::vector<std::string> names;
    std::vector<TracePresentation> traces;
    for (const auto &measurement : catalog) {
        if (measurement.name.empty() || !measurement.IsSupported() || !seen.insert(measurement.name).second) {
            return Failure("Measurement catalog contains an invalid or duplicate entry");
        }
        const auto previous = FindPresentation(candidate, measurement.name);
        TracePresentation presentation = previous == nullptr ? TracePresentation{} : *previous;
        presentation.name = measurement.name;
        names.push_back(measurement.name);
        traces.push_back(std::move(presentation));
    }
    candidate.traces = std::move(traces);
    DiagramCollection::Reconcile(candidate, names, initial);
    if (seen.count(candidate.selectedMeasurement) == 0U) {
        auto *selected = DiagramCollection::Find(candidate, candidate.selectedDiagram);
        // 优先当前或重编号后的图窗记忆，避免目录首成员覆盖其他图窗的主迹线。
        if (selected == nullptr || seen.count(selected->primaryMeasurement) == 0U) {
            const auto fallback = std::find_if(
                candidate.diagrams.begin(), candidate.diagrams.end(), [&seen](const DisplayDiagram &diagram) {
                    return seen.count(diagram.primaryMeasurement) != 0U;
                });
            selected = fallback == candidate.diagrams.end() ? nullptr : &*fallback;
        }
        if (selected != nullptr) {
            candidate.selectedMeasurement = selected->primaryMeasurement;
            candidate.selectedDiagram = selected->number;
        } else {
            candidate.selectedMeasurement = names.empty() ? "" : names.front();
        }
    }
    return Success();
}
}  // 匿名命名空间

const char *LayoutModeName(LayoutMode layout) noexcept
{
    switch (layout) {
        case LayoutMode::SINGLE:
            return "single";
        case LayoutMode::GRID:
            return "grid";
        case LayoutMode::MAXIMIZED:
            return "maximized";
        default:
            return nullptr;
    }
}

bool ParseLayoutMode(const std::string &value, LayoutMode &layout) noexcept
{
    for (const auto candidate : {LayoutMode::SINGLE, LayoutMode::GRID, LayoutMode::MAXIMIZED}) {
        if (value == LayoutModeName(candidate)) {
            layout = candidate;
            return true;
        }
    }
    return false;
}

const char *InfoTablePositionName(InfoTablePosition position) noexcept
{
    switch (position) {
        case InfoTablePosition::BOTTOM:
            return "bottom";
        case InfoTablePosition::LEFT:
            return "left";
        case InfoTablePosition::RIGHT:
            return "right";
        default:
            return nullptr;
    }
}

bool ParseInfoTablePosition(const std::string &value, InfoTablePosition &position) noexcept
{
    for (const auto candidate : {InfoTablePosition::BOTTOM, InfoTablePosition::LEFT, InfoTablePosition::RIGHT}) {
        if (value == InfoTablePositionName(candidate)) {
            position = candidate;
            return true;
        }
    }
    return false;
}

const char *MarkerInfoLocationName(MarkerInfoLocation location) noexcept
{
    switch (location) {
        case MarkerInfoLocation::DIAGRAM:
            return "diagram";
        case MarkerInfoLocation::TABLE:
            return "table";
        default:
            return nullptr;
    }
}

bool ParseMarkerInfoLocation(const std::string &value, MarkerInfoLocation &location) noexcept
{
    for (const auto candidate : {MarkerInfoLocation::DIAGRAM, MarkerInfoLocation::TABLE}) {
        if (value == MarkerInfoLocationName(candidate)) {
            location = candidate;
            return true;
        }
    }
    return false;
}

DisplayWorkspace::DisplayWorkspace(DisplayState state) : state_(std::move(state))
{
    if (state_.diagrams.empty()) {
        state_.diagrams = {{1U, state_.displayedNames, state_.selectedMeasurement}};
    }
    DiagramCollection::Refresh(state_);
}

const DisplayState &DisplayWorkspace::State() const noexcept
{
    return state_;
}

DisplayState DisplayWorkspace::Snapshot() const
{
    return state_;
}

std::vector<DisplayDiagram> DisplayWorkspace::ArrangeDiagrams(const DisplayState &state)
{
    return state.diagrams.empty() ? std::vector<DisplayDiagram>{{1U, state.displayedNames, state.selectedMeasurement}}
                                  : state.diagrams;
}

DiagramGrid DisplayWorkspace::GridGeometry(const DisplayState &state) noexcept
{
    const auto count = static_cast<unsigned>(std::max<std::size_t>(1U, state.diagrams.size()));
    DiagramGrid grid;
    grid.columns = state.gridColumns == 0U ? std::min(2U, count) : std::min(state.gridColumns, count);
    grid.rows = (count + grid.columns - 1U) / grid.columns;
    return grid;
}

DisplayResult DisplayWorkspace::ReplaceSParameterMatrix(
    const std::vector<VnaMeasurement::Measurement> &catalog, unsigned portCount)
{
    if (portCount == 0U || portCount > VnaCore::PHYSICAL_PORT_COUNT || catalog.size() != portCount * portCount) {
        return Failure("S parameter matrix requires every available port pair");
    }
    std::set<std::string> names;
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        const auto &measurement = catalog[index];
        if (measurement.name.empty() || !names.insert(measurement.name).second || !measurement.IsSupported() ||
            measurement.kind != VnaMeasurement::MeasurementKind::S_PARAMETER ||
            measurement.receiverPort != index / portCount + 1U || measurement.sourcePort != index % portCount + 1U) {
            return Failure("S parameter matrix order must be receiver row and source column");
        }
    }
    auto candidate = state_;
    // 同名也代表新定义，不能继承被确认删除的旧格式、刻度或 Marker。
    candidate.traces.clear();
    candidate.diagrams.clear();
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        const auto &name = catalog[index].name;
        TracePresentation presentation;
        presentation.name = name;
        candidate.traces.push_back(std::move(presentation));
        DisplayDiagram diagram;
        diagram.number = static_cast<unsigned>(index + 1U);
        diagram.measurementNames = {name};
        diagram.primaryMeasurement = name;
        diagram.traceSlots = {1U};
        candidate.diagrams.push_back(std::move(diagram));
    }
    candidate.layout = LayoutMode::GRID;
    candidate.gridColumns = portCount;
    candidate.selectedMeasurement = catalog.front().name;
    candidate.selectedDiagram = 1U;
    return Commit(std::move(candidate));
}

DisplayResult DisplayWorkspace::Commit(DisplayState candidate)
{
    DiagramCollection::Refresh(candidate);
    if (EqualState(state_, candidate)) {
        return Success();
    }
    if (state_.revision == std::numeric_limits<std::uint64_t>::max()) {
        return Failure("Display revision exhausted");
    }
    candidate.revision = state_.revision + 1U;
    state_ = std::move(candidate);
    return Success(true);
}

DisplayResult DisplayWorkspace::Reconcile(const std::vector<VnaMeasurement::Measurement> &catalog)
{
    DisplayState candidate = state_;
    const auto result = ReconcileCandidate(candidate, catalog);
    return result.ok ? Commit(std::move(candidate)) : result;
}

DisplayResult DisplayWorkspace::ClonePresentation(const std::string &sourceName, const std::string &targetName,
    const std::vector<VnaMeasurement::Measurement> &catalog, unsigned diagram, bool newDiagram)
{
    if (sourceName == targetName || FindMeasurement(catalog, sourceName) == nullptr) {
        return Failure("Trace clone requires a distinct catalog source and destination");
    }
    DisplayState candidate = state_;
    const auto source = FindPresentation(candidate, sourceName);
    const auto target = FindPresentation(candidate, targetName);
    if (source == nullptr || target == nullptr || FindMeasurement(catalog, targetName) == nullptr) {
        return Failure("Trace clone source or destination does not exist");
    }
    *target = *source;
    target->name = targetName;
    if (newDiagram) {
        const auto added = DiagramCollection::Add(candidate);
        if (!added.ok) {
            return added;
        }
        diagram = candidate.selectedDiagram;
    } else if (diagram == 0U) {
        // 同图复制跟随源迹线归属；活动空图窗不改变源迹线所在图窗。
        const auto sourceDiagram = std::find_if(
            candidate.diagrams.begin(), candidate.diagrams.end(), [&sourceName](const DisplayDiagram &value) {
                return std::find(value.measurementNames.begin(), value.measurementNames.end(), sourceName) !=
                       value.measurementNames.end();
            });
        if (sourceDiagram != candidate.diagrams.end()) {
            diagram = sourceDiagram->number;
        }
    }
    const auto fed = DiagramCollection::Feed(candidate, targetName, diagram);
    return fed.ok ? Commit(std::move(candidate)) : fed;
}

DisplayResult DisplayWorkspace::AddPresentation(
    const std::string &name, const std::vector<VnaMeasurement::Measurement> &catalog, unsigned diagram, bool newDiagram)
{
    DisplayState candidate = state_;
    if (FindPresentation(candidate, name) == nullptr || FindMeasurement(catalog, name) == nullptr) {
        return Failure("New measurement is missing from display catalog");
    }
    if (newDiagram) {
        const auto added = DiagramCollection::Add(candidate);
        if (!added.ok) {
            return added;
        }
        diagram = candidate.selectedDiagram;
    }
    const auto fed = DiagramCollection::Feed(candidate, name, diagram);
    return fed.ok ? Commit(std::move(candidate)) : fed;
}

DisplayResult DisplayWorkspace::RemoveDiagram(unsigned diagram, bool renumber)
{
    DisplayState candidate = state_;
    const auto result = DiagramCollection::Remove(candidate, diagram, renumber);
    return result.ok ? Commit(std::move(candidate)) : result;
}

DisplayResult DisplayWorkspace::ReconcileAxis(const std::vector<double> &frequencyHz, bool preserveSampleIndexes)
{
    if (!Detail::MarkerResolver::ValidAxis(frequencyHz)) {
        return Failure("Marker reconciliation requires a valid physical frequency axis");
    }
    DisplayState candidate = state_;
    for (auto &trace : candidate.traces) {
        trace.markers.ReconcileAxis(frequencyHz, preserveSampleIndexes);
    }
    return Commit(std::move(candidate));
}

void DisplayWorkspace::BindAcquisitionRevision(std::uint64_t acquisitionRevision) noexcept
{
    for (auto &trace : state_.traces) {
        trace.markers.BindAcquisitionRevision(acquisitionRevision);
    }
}

DisplayResult DisplayWorkspace::ApplyCandidate(DisplayState &candidate, const DisplayChange &change,
    const std::vector<VnaMeasurement::Measurement> &catalog, const FrameContext &context)
{
    if (change.checkAcquisitionRevision && change.expectedAcquisitionRevision != context.acquisitionRevision) {
        return Failure("Acquisition revision changed");
    }
    switch (change.kind) {
        case DisplayChangeKind::INFO_TABLE:
            return ApplyInfoTable(candidate, change.infoTable);
        case DisplayChangeKind::DISPLAY_ANNOTATIONS:
            return ApplyAnnotations(candidate, change.annotations);
        case DisplayChangeKind::MARKER_INFO: {
            auto *diagram =
                DiagramCollection::Find(candidate, change.diagram == 0U ? candidate.selectedDiagram : change.diagram);
            if (diagram == nullptr) {
                return Failure("Display diagram does not exist");
            }
            diagram->markerInfoVisible = change.enabled;
            return Success();
        }
        case DisplayChangeKind::DIAGRAM_ADD:
            return DiagramCollection::Add(candidate, change.diagram);
        case DisplayChangeKind::DIAGRAM_REMOVE:
            return DiagramCollection::Remove(candidate, change.diagram, false);
        case DisplayChangeKind::DIAGRAM_SELECT:
            return DiagramCollection::Select(candidate, change.diagram);
        case DisplayChangeKind::OVERLAY:
            if (change.diagram != 0U) {
                const auto selected = DiagramCollection::Select(candidate, change.diagram);
                if (!selected.ok) {
                    return selected;
                }
            }
            DiagramCollection::Overlay(candidate);
            candidate.gridColumns = 0U;
            return Success();
        case DisplayChangeKind::SPLIT:
            DiagramCollection::Split(candidate);
            candidate.gridColumns = 0U;
            return Success();
        case DisplayChangeKind::LAYOUT:
            if (LayoutModeName(change.layout) == nullptr) {
                return Failure("Layout mode is not supported");
            }
            if (change.diagram != 0U) {
                const auto selected = DiagramCollection::Select(candidate, change.diagram);
                if (!selected.ok) {
                    return selected;
                }
            }
            candidate.layout = change.layout;
            return Success();
        default:
            break;
    }
    if (change.kind == DisplayChangeKind::DISPLAYED && change.hasDisplayedNames) {
        return ReplaceDisplayed(candidate, change.displayedNames, catalog);
    }
    std::string name = change.name.empty() ? candidate.selectedMeasurement : change.name;
    if (change.hasSlot && change.name.empty()) {
        const auto *diagram =
            DiagramCollection::Find(candidate, change.diagram == 0U ? candidate.selectedDiagram : change.diagram);
        const auto *slotName = diagram == nullptr ? nullptr : DiagramCollection::NameAtSlot(*diagram, change.slot);
        if (slotName == nullptr) {
            return Failure("Display trace slot does not exist");
        }
        name = *slotName;
    }
    const auto measurement = FindMeasurement(catalog, name);
    auto presentation = FindPresentation(candidate, name);
    if (measurement == nullptr || presentation == nullptr) {
        return Failure("Measurement does not exist in the display workspace");
    }
    if (change.kind == DisplayChangeKind::MEASUREMENT_SELECT) {
        // SCPI 测量选择与显示 FEED 独立；隐藏定义不能因数据查询被重新显示。
        const auto existing = std::find(candidate.displayedNames.begin(), candidate.displayedNames.end(), name);
        if (existing != candidate.displayedNames.end()) {
            return SelectTrace(candidate, name, change);
        }
        candidate.selectedMeasurement = name;
        return Success();
    }
    if (change.kind == DisplayChangeKind::SELECT || (change.kind == DisplayChangeKind::DISPLAYED && change.enabled)) {
        return SelectTrace(candidate, name, change);
    }
    if (change.kind == DisplayChangeKind::DISPLAYED) {
        if (change.diagram != 0U && DiagramCollection::Find(candidate, change.diagram) == nullptr) {
            return Failure("Display diagram does not exist");
        }
        DiagramCollection::Unfeed(candidate, name, change.diagram);
        return Success();
    }
    const auto applied = ApplyTrace(*presentation, change, *measurement, context);
    if (applied.ok && change.kind == DisplayChangeKind::MARKER && change.marker.action == MarkerAction::SELECT) {
        // 标记选择也是所属迹线导航；任一失败都会丢弃包含标记和图窗的整个候选。
        return SelectTrace(candidate, name, change);
    }
    return applied;
}

DisplayResult DisplayWorkspace::Apply(
    const DisplayChange &change, const std::vector<VnaMeasurement::Measurement> &catalog, const FrameContext &context)
{
    DisplayState candidate = state_;
    const auto result = ApplyCandidate(candidate, change, catalog, context);
    return result.ok ? Commit(std::move(candidate)) : result;
}

}  // 命名空间 VnaDisplay
