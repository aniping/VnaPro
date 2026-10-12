/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/display_json.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace VnaWeb {
namespace {
constexpr double MAX_ORIGINAL_POINTS = 10001.0;

const char *ImpedanceState(VnaDisplay::ImpedanceState state) noexcept
{
    switch (state) {
        case VnaDisplay::ImpedanceState::FINITE:
            return "finite";
        case VnaDisplay::ImpedanceState::OPEN:
            return "open";
        case VnaDisplay::ImpedanceState::OVERFLOW:
            return "overflow";
        default:
            return "invalid";
    }
}

const char *ValueState(VnaDisplay::DisplayValueState state) noexcept
{
    if (state == VnaDisplay::DisplayValueState::FINITE) {
        return "finite";
    }
    if (state == VnaDisplay::DisplayValueState::NEGATIVE_INFINITY) {
        return "negativeInfinity";
    }
    return "undefined";
}

Json Value(double value, VnaDisplay::DisplayValueState state)
{
    return state == VnaDisplay::DisplayValueState::FINITE && std::isfinite(value) ? Json(value) : Json();
}

std::string Text(const Json &body, const std::string &field)
{
    const auto item = body.Find(field);
    if (!item || item->type != Json::Type::STRING) {
        throw std::invalid_argument(field + " must be a string");
    }
    return item->string;
}

double Number(const Json &body, const std::string &field)
{
    const auto item = body.Find(field);
    if (!item || item->type != Json::Type::NUMBER || !std::isfinite(item->number)) {
        throw std::invalid_argument(field + " must be finite numeric data");
    }
    return item->number;
}

bool Boolean(const Json &body, const std::string &field)
{
    const auto item = body.Find(field);
    if (!item || item->type != Json::Type::BOOLEAN) {
        throw std::invalid_argument(field + " must be a boolean");
    }
    return item->boolean;
}

void Members(const Json &body, std::initializer_list<const char *> fields)
{
    if (body.type != Json::Type::OBJECT) {
        throw std::invalid_argument("Display body must be an object");
    }
    for (const auto &member : body.object) {
        const bool common = member.first == "requestId" || member.first == "expectedDisplayRevision" ||
                            member.first == "action" || member.first == "traceName";
        if (!common && std::find(fields.begin(), fields.end(), member.first) == fields.end()) {
            throw std::invalid_argument("Unknown display member: " + member.first);
        }
    }
}

VnaDisplay::ScaleChange Scale(const Json &body)
{
    VnaDisplay::ScaleChange scale;
    scale.hasMin = body.Find("min") != nullptr;
    scale.hasMax = body.Find("max") != nullptr;
    scale.hasReferencePosition = body.Find("referencePosition") != nullptr;
    scale.hasPerDivision = body.Find("perDivision") != nullptr;
    scale.hasReference = body.Find("referenceLevel") != nullptr;
    if (scale.hasMin) {
        scale.min = Number(body, "min");
    }
    if (scale.hasMax) {
        scale.max = Number(body, "max");
    }
    if (scale.hasReferencePosition) {
        scale.referencePosition = Number(body, "referencePosition");
    }
    if (scale.hasPerDivision) {
        scale.perDivision = Number(body, "perDivision");
    }
    if (scale.hasReference) {
        scale.reference = Number(body, "referenceLevel");
    }
    if (!scale.hasMin && !scale.hasMax && !scale.hasPerDivision && !scale.hasReference && !scale.hasReferencePosition) {
        throw std::invalid_argument("Scale requires at least one scale field");
    }
    return scale;
}

VnaDisplay::MarkerChange MarkerChange(const Json &body)
{
    VnaDisplay::MarkerChange marker;
    marker.hasEnabled = body.Find("enabled") != nullptr;
    marker.hasFrequency = body.Find("frequencyHz") != nullptr;
    marker.hasSampleIndex = body.Find("sampleIndex") != nullptr;
    marker.hasStep = body.Find("step") != nullptr;
    marker.hasInterpolation = body.Find("discrete") != nullptr;
    marker.checkAcquisitionRevision = body.Find("acquisitionRevision") != nullptr;
    if (marker.hasEnabled) {
        marker.enabled = Boolean(body, "enabled");
    }
    if (marker.hasFrequency) {
        marker.frequencyHz = Number(body, "frequencyHz");
    }
    if (marker.hasSampleIndex) {
        const auto index = Number(body, "sampleIndex");
        if (index < 0.0 || index >= MAX_ORIGINAL_POINTS || std::floor(index) != index) {
            throw std::invalid_argument("Marker sampleIndex must be an original sample index");
        }
        marker.sampleIndex = static_cast<std::size_t>(index);
    }
    if (marker.hasStep) {
        const auto step = Number(body, "step");
        if (step < -MAX_ORIGINAL_POINTS || step > MAX_ORIGINAL_POINTS || std::floor(step) != step) {
            throw std::invalid_argument("Marker step must be an integer within the sweep capacity");
        }
        marker.step = static_cast<int>(step);
    }
    if (marker.hasInterpolation) {
        marker.interpolate = !Boolean(body, "discrete");
    }
    if (marker.checkAcquisitionRevision) {
        marker.expectedAcquisitionRevision = DisplayJson::Revision(body, "acquisitionRevision");
    }
    if ((marker.hasFrequency || marker.hasSampleIndex || marker.hasStep) && !marker.checkAcquisitionRevision) {
        throw std::invalid_argument("Marker position requires acquisitionRevision");
    }
    return marker;
}
}  // 匿名命名空间

std::uint64_t DisplayJson::Revision(const Json &body, const std::string &field)
{
    const auto string = Text(body, field);
    if (string.empty()) {
        throw std::invalid_argument(field + " is empty");
    }
    std::uint64_t result = 0U;
    for (const char digit : string) {
        if (digit < '0' || digit > '9' ||
            result > ((std::numeric_limits<std::uint64_t>::max)() - static_cast<unsigned>(digit - '0')) / 10U) {
            throw std::invalid_argument(field + " must be a decimal uint64 string");
        }
        result = result * 10U + static_cast<unsigned>(digit - '0');
    }
    return result;
}

Json DisplayJson::Presentation(
    const VnaDisplay::TracePresentation &trace, const VnaMeasurement::Measurement &measurement)
{
    Json result = Json::MakeObject();
    result["format"] = Json(VnaDisplay::DisplayFormatName(trace.format));
    result["cartesianFormat"] = Json(VnaDisplay::DisplayFormatName(trace.cartesianFormat));
    Json scale = Json::MakeObject();
    scale["min"] = Json(trace.scale.min);
    scale["max"] = Json(trace.scale.max);
    scale["referencePosition"] = Json(trace.scale.referencePosition);
    result["scale"] = std::move(scale);
    result["smithRadius"] = Json(trace.smithRadius);
    result["visible"] = Json(trace.visible);
    Json marker = Json::MakeObject();
    marker["enabled"] = Json(trace.marker.enabled);
    marker["frequencyHz"] = trace.marker.hasFrequency ? Json(trace.marker.frequencyHz) : Json();
    marker["sampleIndex"] =
        trace.marker.hasSampleIndex ? Json(static_cast<unsigned>(trace.marker.sampleIndex)) : Json();
    marker["discrete"] = Json(!trace.marker.interpolate);
    result["marker"] = std::move(marker);
    Json formats = Json::MakeArray();
    for (auto format : VnaDisplay::TraceProjection::AllowedFormats(measurement)) {
        formats.array.emplace_back(VnaDisplay::DisplayFormatName(format));
    }
    result["allowedFormats"] = std::move(formats);
    return result;
}

Json DisplayJson::State(const VnaDisplay::DisplayState &state, const std::vector<VnaMeasurement::Measurement> &catalog)
{
    Json result = Json::MakeObject();
    result["revision"] = Json(std::to_string(state.revision));
    result["selectedMeasurement"] = Json(state.selectedMeasurement);
    result["layout"] = Json(VnaDisplay::LayoutModeName(state.layout));
    Json names = Json::MakeArray();
    for (const auto &name : state.displayedNames) {
        names.array.emplace_back(name);
    }
    result["displayedNames"] = std::move(names);
    Json presentations = Json::MakeObject();
    for (const auto &trace : state.traces) {
        const auto measurement =
            std::find_if(catalog.begin(), catalog.end(), [&trace](const VnaMeasurement::Measurement &item) {
                return item.name == trace.name;
            });
        if (measurement != catalog.end()) {
            presentations[trace.name] = Presentation(trace, *measurement);
        }
    }
    result["presentations"] = std::move(presentations);
    Json diagrams = Json::MakeArray();
    for (const auto &diagram : VnaDisplay::DisplayWorkspace::ArrangeDiagrams(state)) {
        Json item = Json::MakeObject();
        item["number"] = Json(diagram.number);
        item["primaryMeasurement"] = Json(diagram.primaryMeasurement);
        Json measurements = Json::MakeArray();
        for (const auto &name : diagram.measurementNames) {
            measurements.array.emplace_back(name);
        }
        item["measurementNames"] = std::move(measurements);
        diagrams.array.push_back(std::move(item));
    }
    result["diagrams"] = std::move(diagrams);
    return result;
}

Json DisplayJson::Marker(const VnaDisplay::MarkerReadout &marker)
{
    Json result = Json::MakeObject();
    result["available"] = Json(marker.available);
    result["valid"] = Json(marker.valid);
    result["interpolated"] = Json(marker.interpolated);
    result["reason"] = Json(marker.available ? "" : "Marker disabled or data unavailable");
    result["sampleIndex"] = marker.available ? Json(static_cast<unsigned>(marker.sampleIndex)) : Json();
    result["frequencyHz"] = marker.available ? Json(marker.frequencyHz) : Json();
    result["real"] = marker.valid ? Json(marker.real) : Json();
    result["imag"] = marker.valid ? Json(marker.imag) : Json();
    result["value"] = Value(marker.value.primary, marker.value.state);
    result["secondaryValue"] = marker.value.hasSecondary ? Value(marker.value.secondary, marker.value.state) : Json();
    result["valueState"] = Json(ValueState(marker.value.state));
    result["secondaryState"] = Json(marker.value.hasSecondary ? ValueState(marker.value.state) : "undefined");
    const bool impedance =
        marker.available && marker.valid && marker.impedance.state == VnaDisplay::ImpedanceState::FINITE;
    result["impedanceReal"] = impedance ? Json(marker.impedance.real) : Json();
    result["impedanceImag"] = impedance ? Json(marker.impedance.imag) : Json();
    result["impedanceState"] = Json(ImpedanceState(marker.impedance.state));
    return result;
}

Json DisplayJson::Projection(
    const VnaDisplay::ProjectedTrace &trace, VnaDisplay::DisplayFormat format, const std::vector<std::size_t> &indexes)
{
    Json result = Json::MakeObject();
    result["format"] = Json(VnaDisplay::DisplayFormatName(format));
    Json primary = Json::MakeArray();
    Json secondary = Json::MakeArray();
    Json states = Json::MakeArray();
    Json secondaryStates = Json::MakeArray();
    for (const auto index : indexes) {
        const auto point =
            trace.available && index < trace.points.size() ? trace.points[index] : VnaDisplay::FormattedPoint{};
        primary.array.push_back(Value(point.primary, point.state));
        secondary.array.push_back(point.hasSecondary ? Value(point.secondary, point.state) : Json());
        states.array.emplace_back(ValueState(point.state));
        secondaryStates.array.emplace_back(point.hasSecondary ? ValueState(point.state) : "undefined");
    }
    result["values"] = std::move(primary);
    result["secondaryValues"] = std::move(secondary);
    result["valueStates"] = std::move(states);
    result["secondaryStates"] = std::move(secondaryStates);
    PointImpedance(result, trace, format, indexes);
    result["marker"] = Marker(trace.marker);
    return result;
}

void DisplayJson::PointImpedance(Json &projection, const VnaDisplay::ProjectedTrace &trace,
    VnaDisplay::DisplayFormat format, const std::vector<std::size_t> &indexes)
{
    Json real = Json::MakeArray();
    Json imaginary = Json::MakeArray();
    Json states = Json::MakeArray();
    if (format == VnaDisplay::DisplayFormat::SMITH) {
        for (const auto index : indexes) {
            const auto point = trace.available && index < trace.impedancePoints.size()
                                   ? trace.impedancePoints[index]
                                   : VnaDisplay::NormalizedImpedance{};
            const bool finite = point.state == VnaDisplay::ImpedanceState::FINITE;
            real.array.push_back(finite ? Json(point.real) : Json());
            imaginary.array.push_back(finite ? Json(point.imag) : Json());
            states.array.emplace_back(ImpedanceState(point.state));
        }
    }
    projection["impedanceRealValues"] = std::move(real);
    projection["impedanceImagValues"] = std::move(imaginary);
    projection["impedanceStates"] = std::move(states);
}

void DisplayJson::WorkspaceChange(VnaDisplay::DisplayChange &change, const Json &body, const std::string &action)
{
    if (action == "select") {
        Members(body, {});
        change.name = Text(body, "traceName");
    } else if (action == "displayed" || action == "visible") {
        Members(body, {"enabled"});
        change.kind =
            action == "displayed" ? VnaDisplay::DisplayChangeKind::DISPLAYED : VnaDisplay::DisplayChangeKind::VISIBLE;
        change.enabled = Boolean(body, "enabled");
        change.visible = change.enabled;
    } else if (action == "layout") {
        Members(body, {"layout"});
        change.kind = VnaDisplay::DisplayChangeKind::LAYOUT;
        if (!VnaDisplay::ParseLayoutMode(Text(body, "layout"), change.layout)) {
            throw std::invalid_argument("Unknown display layout");
        }
    }
}

void DisplayJson::TraceChange(VnaDisplay::DisplayChange &change, const Json &body, const std::string &action)
{
    if (action == "format") {
        Members(body, {"format"});
        change.kind = VnaDisplay::DisplayChangeKind::FORMAT;
        if (!VnaDisplay::ParseDisplayFormat(Text(body, "format"), change.format)) {
            throw std::invalid_argument("Unknown display format");
        }
    } else if (action == "scale") {
        Members(body, {"min", "max", "perDivision", "referenceLevel", "referencePosition"});
        change.kind = VnaDisplay::DisplayChangeKind::SCALE;
        change.scale = Scale(body);
    } else if (action == "autoscale") {
        Members(body, {"acquisitionRevision"});
        change.kind = VnaDisplay::DisplayChangeKind::AUTOSCALE;
        change.checkAcquisitionRevision = true;
        change.expectedAcquisitionRevision = Revision(body, "acquisitionRevision");
    } else if (action == "smithRadius") {
        Members(body, {"radius"});
        change.kind = VnaDisplay::DisplayChangeKind::SMITH_RADIUS;
        change.smithRadius = Number(body, "radius");
    } else if (action == "marker") {
        Members(body, {"enabled", "frequencyHz", "sampleIndex", "step", "discrete", "acquisitionRevision"});
        change.kind = VnaDisplay::DisplayChangeKind::MARKER;
        change.marker = MarkerChange(body);
    } else {
        throw std::invalid_argument("Unknown display action");
    }
}

VnaDisplay::DisplayChange DisplayJson::Change(const Json &body)
{
    VnaDisplay::DisplayChange change;
    const auto action = Text(body, "action");
    if (body.Find("traceName")) {
        change.name = Text(body, "traceName");
    }
    if (action == "select" || action == "displayed" || action == "visible" || action == "layout") {
        WorkspaceChange(change, body, action);
    } else {
        TraceChange(change, body, action);
    }
    return change;
}

}  // 命名空间 VnaWeb
