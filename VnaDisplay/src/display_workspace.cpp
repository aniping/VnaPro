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
#include "vna_display/trace_projection.h"
#include "vna_measurement/measurement_processor.h"

namespace VnaDisplay {
namespace {

constexpr double MIN_SMITH_RADIUS = 0.01;

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
           left.visible == right.visible && left.markers.SameSettings(right.markers);
}

bool EqualState(const DisplayState &left, const DisplayState &right)
{
    return left.selectedMeasurement == right.selectedMeasurement && left.displayedNames == right.displayedNames &&
           left.layout == right.layout && left.traces.size() == right.traces.size() &&
           std::equal(left.traces.begin(), left.traces.end(), right.traces.begin(), EqualTrace);
}

DisplayDiagram Diagram(unsigned number, const std::vector<std::string> &names, const std::string &selected)
{
    DisplayDiagram diagram;
    diagram.number = number;
    diagram.measurementNames = names;
    if (!names.empty()) {
        diagram.primaryMeasurement =
            std::find(names.begin(), names.end(), selected) != names.end() ? selected : names.front();
    }
    return diagram;
}

bool UsesSmith(const DisplayState &state, const std::string &name)
{
    const auto trace = std::find_if(state.traces.begin(), state.traces.end(), [&name](const TracePresentation &item) {
        return item.name == name;
    });
    return trace != state.traces.end() && trace->format == DisplayFormat::SMITH;
}

void AppendDiagramGroups(std::vector<DisplayDiagram> &diagrams, const std::vector<std::string> &names,
    std::size_t groupCount, const std::string &selected)
{
    for (std::size_t group = 0U; group < groupCount; ++group) {
        std::vector<std::string> grouped;
        for (std::size_t index = group; index < names.size(); index += groupCount) {
            grouped.push_back(names[index]);
        }
        diagrams.push_back(Diagram(static_cast<unsigned>(diagrams.size() + 1U), grouped, selected));
    }
}

std::vector<DisplayDiagram> GridDiagrams(const DisplayState &state, const std::vector<std::string> &names)
{
    std::vector<DisplayDiagram> diagrams;
    if (names.size() <= MAX_DISPLAY_DIAGRAMS) {
        for (const auto &name : names) {
            diagrams.push_back(Diagram(static_cast<unsigned>(diagrams.size() + 1U), {name}, state.selectedMeasurement));
        }
        return diagrams;
    }
    std::vector<std::string> smith;
    std::vector<std::string> cartesian;
    for (const auto &name : names) {
        (UsesSmith(state, name) ? smith : cartesian).push_back(name);
    }
    // 坐标类型不混合；两种类型同时存在时，各类尽量获得两个图窗。
    std::size_t cartesianCount = std::min(cartesian.size(), MAX_DISPLAY_DIAGRAMS / 2U);
    const std::size_t smithCount = std::min(smith.size(), MAX_DISPLAY_DIAGRAMS - cartesianCount);
    cartesianCount = std::min(cartesian.size(), MAX_DISPLAY_DIAGRAMS - smithCount);
    AppendDiagramGroups(diagrams, cartesian, cartesianCount, state.selectedMeasurement);
    AppendDiagramGroups(diagrams, smith, smithCount, state.selectedMeasurement);
    return diagrams;
}

DisplayResult SelectTrace(DisplayState &state, const std::string &name, const DisplayChange &change)
{
    auto &names = state.displayedNames;
    const auto existing = std::find(names.begin(), names.end(), name);
    if (change.hasSlot) {
        if (change.slot == 0U || change.slot > MAX_DISPLAY_TRACES || change.slot > names.size() + 1U) {
            return Failure("Display slot does not exist");
        }
        const auto target = static_cast<std::size_t>(change.slot - 1U);
        if (target == names.size()) {
            if (existing == names.end()) {
                names.push_back(name);
            }
        } else {
            names[target] = name;
            for (std::size_t index = names.size(); index-- > 0U;) {
                if (index != target && names[index] == name) {
                    names.erase(names.begin() + static_cast<std::ptrdiff_t>(index));
                }
            }
        }
    } else if (existing == names.end()) {
        if (names.size() < MAX_DISPLAY_TRACES) {
            names.push_back(name);
        } else {
            return Failure("Displayed trace capacity exhausted");
        }
    }
    state.selectedMeasurement = name;
    return Success();
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
    state.displayedNames = names;
    if (!names.empty() && std::find(names.begin(), names.end(), state.selectedMeasurement) == names.end()) {
        state.selectedMeasurement = names.front();
    }
    return Success();
}

DisplayResult SetDisplayed(DisplayState &state, const std::string &name, const DisplayChange &change)
{
    auto &names = state.displayedNames;
    const auto found = std::find(names.begin(), names.end(), name);
    if (change.enabled) {
        if (found == names.end() && names.size() == MAX_DISPLAY_TRACES) {
            return Failure("Displayed trace capacity exhausted");
        }
        return SelectTrace(state, name, change);
    }
    if (found != names.end()) {
        names.erase(found);
    }
    if (!names.empty() && state.selectedMeasurement == name) {
        state.selectedMeasurement = names.front();
    }
    return Success();
}

DisplayResult ChangeFormat(
    TracePresentation &presentation, DisplayFormat format, const VnaMeasurement::Measurement &measurement)
{
    if (DisplayFormatName(format) == nullptr) {
        return Failure("Display format is not supported");
    }
    if (format == DisplayFormat::SMITH) {
        if (!TraceProjection::SupportsSmith(measurement)) {
            return Failure("Smith format requires a supported reflection measurement");
        }
        presentation.format = format;
        return Success();
    }
    if (presentation.format == format) {
        return Success();
    }
    const bool restoring = presentation.format == DisplayFormat::SMITH && presentation.cartesianFormat == format;
    presentation.format = format;
    presentation.cartesianFormat = format;
    if (!restoring) {
        presentation.scale = TraceProjection::DefaultScale(format);
    }
    return Success();
}

bool CurrentFrame(const FrameContext &context)
{
    return context.current && context.frame && context.frame->configRevision == context.acquisitionRevision &&
           context.frame->runId == context.runId;
}

DisplayResult Autoscale(
    TracePresentation &presentation, const VnaMeasurement::Measurement &measurement, const FrameContext &context)
{
    if (!CurrentFrame(context)) {
        return Failure("Autoscale requires a current complete frame");
    }
    const auto trace = VnaMeasurement::MeasurementProcessor::Calculate(*context.frame, measurement);
    if (!TraceProjection::Autoscale(trace, presentation.format, presentation.scale, presentation.smithRadius,
            context.frame->plan.frequencyHz)) {
        return Failure("Autoscale does not have a finite representable range");
    }
    return Success();
}

DisplayResult ApplyTrace(TracePresentation &presentation, const DisplayChange &change,
    const VnaMeasurement::Measurement &measurement, const FrameContext &context)
{
    std::string error;
    switch (change.kind) {
        case DisplayChangeKind::FORMAT:
            return ChangeFormat(presentation, change.format, measurement);
        case DisplayChangeKind::SCALE:
            return Detail::ScaleCalculator::Apply(presentation.scale, change.scale, error, presentation.format)
                       ? Success()
                       : Failure(error);
        case DisplayChangeKind::AUTOSCALE:
            return Autoscale(presentation, measurement, context);
        case DisplayChangeKind::SMITH_RADIUS:
            if (!std::isfinite(change.smithRadius) || change.smithRadius < MIN_SMITH_RADIUS) {
                return Failure("Smith radius must be finite and at least 0.01");
            }
            presentation.smithRadius = change.smithRadius;
            return Success();
        case DisplayChangeKind::VISIBLE:
            presentation.visible = change.visible;
            return Success();
        case DisplayChangeKind::MARKER:
            return presentation.markers.Apply(change.marker, context, error) ? Success() : Failure(error);
        default:
            return Failure("Display operation is not supported");
    }
}

DisplayResult ReconcileCandidate(DisplayState &candidate, const std::vector<VnaMeasurement::Measurement> &catalog)
{
    if (catalog.size() > MAX_DISPLAY_TRACES) {
        return Failure("Measurement catalog capacity exhausted");
    }
    const bool initial = candidate.traces.empty();
    std::set<std::string> names;
    std::vector<TracePresentation> traces;
    for (const auto &measurement : catalog) {
        if (measurement.name.empty() || !measurement.IsSupported() || !names.insert(measurement.name).second) {
            return Failure("Measurement catalog contains an invalid or duplicate entry");
        }
        const auto previous = FindPresentation(candidate, measurement.name);
        TracePresentation presentation = previous == nullptr ? TracePresentation{} : *previous;
        presentation.name = measurement.name;
        if (presentation.format == DisplayFormat::SMITH && !TraceProjection::SupportsSmith(measurement)) {
            presentation.format = presentation.cartesianFormat;
        }
        traces.push_back(std::move(presentation));
    }
    candidate.traces = std::move(traces);
    auto &displayed = candidate.displayedNames;
    displayed.erase(std::remove_if(displayed.begin(), displayed.end(),
                        [&names](const std::string &name) {
                            return names.count(name) == 0U;
                        }),
        displayed.end());
    if (initial) {
        for (std::size_t index = 0U; index < catalog.size(); ++index) {
            displayed.push_back(catalog[index].name);
        }
    }
    if (names.count(candidate.selectedMeasurement) == 0U) {
        candidate.selectedMeasurement = !displayed.empty() ? displayed.front()
                                        : catalog.empty()  ? ""
                                                           : catalog.front().name;
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

DisplayWorkspace::DisplayWorkspace(DisplayState state) : state_(std::move(state))
{
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
    const std::vector<std::string> names(state.displayedNames.begin(),
        state.displayedNames.begin() +
            static_cast<std::ptrdiff_t>(std::min(state.displayedNames.size(), MAX_DISPLAY_TRACES)));
    if (state.layout == LayoutMode::GRID && !names.empty()) {
        return GridDiagrams(state, names);
    }
    if (state.layout == LayoutMode::MAXIMIZED && !names.empty()) {
        const auto diagrams = GridDiagrams(state, names);
        const auto selected = std::find_if(diagrams.begin(), diagrams.end(), [&state](const DisplayDiagram &diagram) {
            return std::find(diagram.measurementNames.begin(), diagram.measurementNames.end(),
                       state.selectedMeasurement) != diagram.measurementNames.end();
        });
        return {selected == diagrams.end() ? diagrams.front() : *selected};
    }
    std::vector<std::string> smith;
    std::vector<std::string> cartesian;
    for (const auto &name : names) {
        (UsesSmith(state, name) ? smith : cartesian).push_back(name);
    }
    if (!smith.empty() && !cartesian.empty()) {
        return {Diagram(1U, cartesian, state.selectedMeasurement), Diagram(2U, smith, state.selectedMeasurement)};
    }
    return {Diagram(1U, names, state.selectedMeasurement)};
}

DisplayResult DisplayWorkspace::Commit(DisplayState candidate)
{
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
    const std::vector<VnaMeasurement::Measurement> &catalog)
{
    if (sourceName == targetName || FindMeasurement(catalog, sourceName) == nullptr) {
        return Failure("Trace clone requires a distinct catalog source and destination");
    }
    DisplayState candidate = state_;
    const auto source = FindPresentation(candidate, sourceName);
    const auto target = FindPresentation(candidate, targetName);
    const auto measurement = FindMeasurement(catalog, targetName);
    if (source == nullptr || target == nullptr || measurement == nullptr) {
        return Failure("Trace clone source or destination does not exist");
    }
    if (source->format == DisplayFormat::SMITH && !TraceProjection::SupportsSmith(*measurement)) {
        return Failure("Cloned Smith settings require a reflection measurement");
    }
    // 全部成员均为值语义；复制后移动或关闭目标标记，不会改变源迹线。
    *target = *source;
    target->name = targetName;
    const auto selected = SelectTrace(candidate, targetName, DisplayChange{});
    return selected.ok ? Commit(std::move(candidate)) : selected;
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
    if (change.kind == DisplayChangeKind::LAYOUT) {
        if (LayoutModeName(change.layout) == nullptr) {
            return Failure("Layout mode is not supported");
        }
        candidate.layout = change.layout;
        return Success();
    }
    if (change.kind == DisplayChangeKind::DISPLAYED && change.hasDisplayedNames) {
        return ReplaceDisplayed(candidate, change.displayedNames, catalog);
    }
    std::string name = change.name.empty() ? candidate.selectedMeasurement : change.name;
    if (change.hasSlot && change.name.empty()) {
        if (change.slot == 0U || change.slot > candidate.displayedNames.size()) {
            return Failure("Display slot does not exist");
        }
        name = candidate.displayedNames[change.slot - 1U];
    }
    const auto measurement = FindMeasurement(catalog, name);
    auto presentation = FindPresentation(candidate, name);
    if (measurement == nullptr || presentation == nullptr) {
        return Failure("Measurement does not exist in the display workspace");
    }
    if (change.kind == DisplayChangeKind::SELECT) {
        return SelectTrace(candidate, name, change);
    }
    if (change.kind == DisplayChangeKind::DISPLAYED) {
        return SetDisplayed(candidate, name, change);
    }
    return ApplyTrace(*presentation, change, *measurement, context);
}

DisplayResult DisplayWorkspace::Apply(
    const DisplayChange &change, const std::vector<VnaMeasurement::Measurement> &catalog, const FrameContext &context)
{
    DisplayState candidate = state_;
    const auto result = ApplyCandidate(candidate, change, catalog, context);
    return result.ok ? Commit(std::move(candidate)) : result;
}

}  // 命名空间 VnaDisplay
