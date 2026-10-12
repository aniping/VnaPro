/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_display_adapter.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_parser.h"
#include "vna_display/display_workspace.h"
#include "vna_display/marker_collection.h"
#include "vna_display/trace_projection.h"

namespace VnaScpi {
namespace {

ScpiDisplayResult Handled()
{
    ScpiDisplayResult result;
    result.matched = true;
    return result;
}

ScpiDisplayResult Failure(int code, const std::string &message)
{
    auto result = Handled();
    result.errorCode = code;
    result.error = message;
    return result;
}

bool Arguments(const ParsedCommand &command, std::size_t count, ScpiDisplayResult &result)
{
    if (command.arguments.size() == count) {
        return true;
    }
    result = Failure(
        command.arguments.size() < count ? MISSING_PARAMETER : PARAMETER_NOT_ALLOWED, "Parameter count mismatch");
    return false;
}

std::string Number(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::scientific << std::setprecision(ASCII_FRACTIONAL_DIGITS) << value;
    return stream.str();
}

bool Boolean(const std::string &text, bool &value)
{
    const auto argument = Upper(text);
    if (argument == "ON" || argument == "1") {
        value = true;
        return true;
    }
    if (argument == "OFF" || argument == "0") {
        value = false;
        return true;
    }
    return false;
}

const VnaDisplay::TracePresentation *Presentation(const VnaDisplay::DisplayState &state, const std::string &name)
{
    const auto found = std::find_if(state.traces.begin(), state.traces.end(), [&name](const auto &trace) {
        return trace.name == name;
    });
    return found == state.traces.end() ? nullptr : &*found;
}

const char *FormatName(VnaDisplay::DisplayFormat format)
{
    switch (format) {
        case VnaDisplay::DisplayFormat::DB:
            return "MLOG";
        case VnaDisplay::DisplayFormat::MAGNITUDE:
            return "MLIN";
        case VnaDisplay::DisplayFormat::PHASE:
            return "PHAS";
        case VnaDisplay::DisplayFormat::REAL:
            return "REAL";
        case VnaDisplay::DisplayFormat::IMAG:
            return "IMAG";
        case VnaDisplay::DisplayFormat::SMITH:
            return "SMIT";
        default:
            return nullptr;
    }
}

bool FormatArgument(const std::string &text, VnaDisplay::DisplayFormat &format)
{
    const auto parameter = Upper(text);
    const std::pair<const char *, VnaDisplay::DisplayFormat> names[] = {{"MLOG", VnaDisplay::DisplayFormat::DB},
        {"MLOGARITHMIC", VnaDisplay::DisplayFormat::DB}, {"MLIN", VnaDisplay::DisplayFormat::MAGNITUDE},
        {"MLINEAR", VnaDisplay::DisplayFormat::MAGNITUDE}, {"PHAS", VnaDisplay::DisplayFormat::PHASE},
        {"PHASE", VnaDisplay::DisplayFormat::PHASE}, {"REAL", VnaDisplay::DisplayFormat::REAL},
        {"IMAG", VnaDisplay::DisplayFormat::IMAG}, {"IMAGINARY", VnaDisplay::DisplayFormat::IMAG},
        {"SMIT", VnaDisplay::DisplayFormat::SMITH}, {"SMITH", VnaDisplay::DisplayFormat::SMITH}};
    for (const auto &entry : names) {
        if (parameter == entry.first) {
            format = entry.second;
            return true;
        }
    }
    return false;
}

bool IndexedRoute(const std::string &path, const std::string &prefix, unsigned &slot, std::string &leaf)
{
    if (path.compare(0U, prefix.size(), prefix) != 0) {
        return false;
    }
    const auto separator = path.find(':', prefix.size());
    const auto number = path.substr(prefix.size(), separator - prefix.size());
    if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos ||
        !Integer(number, slot, (std::numeric_limits<unsigned>::max)())) {
        return false;
    }
    leaf = separator == std::string::npos ? std::string() : path.substr(separator + 1U);
    return true;
}

}  // 匿名命名空间

ScpiDisplayAdapter::ScpiDisplayAdapter(VnaService::InstrumentService &service) : service_(service)
{
}

ScpiDisplayResult ScpiDisplayAdapter::Execute(const ParsedCommand &command) const
{
    if (command.path == "CALC1:PAR:SEL") {
        return Selection(command);
    }
    if (command.path == "CALC1:FORM") {
        return Format(command);
    }
    if (command.path == "CALC1:MARK:AOFF") {
        return AllMarkersOff(command);
    }
    unsigned slot = 0U;
    std::string leaf;
    if (IndexedRoute(command.path, "CALC1:MARK", slot, leaf)) {
        return Marker(command, slot, leaf.empty() ? "STAT" : leaf);
    }
    if (IndexedRoute(command.path, "DISP:WIND1:TRAC", slot, leaf)) {
        if (leaf.compare(0U, 7U, "Y:SCAL:") == 0) {
            leaf.erase(2U, 5U);
        }
        return Trace(command, slot, leaf);
    }
    return ScpiDisplayResult();
}

ScpiDisplayResult ScpiDisplayAdapter::Selection(const ParsedCommand &command) const
{
    auto result = Handled();
    if (!Arguments(command, command.query ? 0U : 1U, result)) {
        return result;
    }
    if (command.query) {
        result.response = Quote(service_.Display().selectedMeasurement);
        return result;
    }
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::SELECT;
    if (!StringArgument(command.arguments.front(), change.name)) {
        return Failure(DATA_TYPE_ERROR, "Measurement name requires a string");
    }
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::Format(const ParsedCommand &command) const
{
    auto result = Handled();
    if (!Arguments(command, command.query ? 0U : 1U, result)) {
        return result;
    }
    if (command.query) {
        const auto &state = service_.Display();
        const auto *trace = Presentation(state, state.selectedMeasurement);
        if (trace == nullptr) {
            return Failure(SETTINGS_CONFLICT, "No measurement selected");
        }
        const auto *name = FormatName(trace->format);
        if (name == nullptr) {
            return Failure(SETTINGS_CONFLICT, "Current format is outside the supported PNA subset");
        }
        result.response = name;
        return result;
    }
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::FORMAT;
    if (!FormatArgument(command.arguments.front(), change.format)) {
        return Failure(ILLEGAL_PARAMETER_VALUE, "Display format is outside the supported PNA subset");
    }
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::Marker(const ParsedCommand &command, unsigned id, const std::string &leaf) const
{
    if (id == 0U || id > VnaDisplay::MAX_MARKERS) {
        return Failure(UNDEFINED_HEADER, "Marker suffix is outside the supported range");
    }
    if (leaf != "STAT" && leaf != "DISC" && leaf != "X" && leaf != "Y") {
        return ScpiDisplayResult();
    }
    if (leaf == "Y" && !command.query) {
        return Failure(UNDEFINED_HEADER, "Marker Y is query-only");
    }
    auto result = Handled();
    if (!Arguments(command, command.query ? 0U : 1U, result)) {
        return result;
    }
    const auto &state = service_.Display();
    const auto *trace = Presentation(state, state.selectedMeasurement);
    if (trace == nullptr) {
        return Failure(SETTINGS_CONFLICT, "No measurement selected");
    }
    if (command.query) {
        return ReadMarker(*trace, id, leaf);
    }
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::MARKER;
    change.marker.id = id;
    if (leaf == "X") {
        return SetMarkerFrequency(command, std::move(change));
    }
    bool value = false;
    if (!Boolean(command.arguments.front(), value)) {
        return Failure(ILLEGAL_PARAMETER_VALUE, "Marker setting requires ON or OFF");
    }
    change.marker.hasEnabled = leaf == "STAT";
    change.marker.enabled = value;
    change.marker.hasInterpolation = leaf == "DISC";
    change.marker.interpolate = !value;
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::AllMarkersOff(const ParsedCommand &command) const
{
    if (command.query) {
        return Failure(UNDEFINED_HEADER, "All markers off is write-only");
    }
    auto result = Handled();
    if (!Arguments(command, 0U, result)) {
        return result;
    }
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::MARKER;
    change.marker.action = VnaDisplay::MarkerAction::ALL_OFF;
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::ReadMarker(
    const VnaDisplay::TracePresentation &trace, unsigned id, const std::string &leaf) const
{
    auto result = Handled();
    const auto *settings = trace.markers.Find(id);
    if (settings == nullptr) {
        return Failure(DATA_OUT_OF_RANGE, "Marker does not exist");
    }
    if (leaf == "STAT" || leaf == "DISC") {
        const bool value = leaf == "STAT" ? settings->enabled : !settings->interpolate;
        result.response = value ? "1" : "0";
        return result;
    }
    if (!settings->enabled) {
        return Failure(SETTINGS_CONFLICT, "Marker is disabled");
    }
    const auto data = service_.DisplayData(trace.name);
    // 显式编号查询读取各自投影，不改变 Web 当前活动标记或其他编号的设置。
    const auto found = std::find_if(data.markers.begin(), data.markers.end(), [id](const auto &marker) {
        return marker.id == id;
    });
    const auto *marker = found == data.markers.end() ? nullptr : &*found;
    if (leaf == "X") {
        if (marker != nullptr && marker->available) {
            result.response = Number(marker->frequencyHz);
        } else if (settings->hasFrequency) {
            result.response = Number(settings->frequencyHz);
        } else {
            return Failure(DATA_CORRUPT_OR_STALE, "Marker position requires a current frequency axis");
        }
        return result;
    }
    if (!data.available || marker == nullptr || !marker->available || !marker->valid ||
        marker->value.state != VnaDisplay::DisplayValueState::FINITE) {
        return Failure(DATA_CORRUPT_OR_STALE, "Marker value is not available for the current measurement");
    }
    if (trace.format == VnaDisplay::DisplayFormat::REALIMAG) {
        return Failure(SETTINGS_CONFLICT, "Current format is outside the supported PNA subset");
    }
    result.response = trace.format == VnaDisplay::DisplayFormat::SMITH
                          ? Number(marker->real) + ',' + Number(marker->imag)
                          : Number(marker->value.primary) + ",0";
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::SetMarkerFrequency(
    const ParsedCommand &command, VnaDisplay::DisplayChange change) const
{
    const auto parameter = Upper(command.arguments.front());
    double frequency = 0.0;
    if (parameter == "MIN" || parameter == "MINIMUM") {
        frequency = service_.Settings().startHz;
    } else if (parameter == "MAX" || parameter == "MAXIMUM") {
        frequency = service_.Settings().stopHz;
    } else if (!Numeric(command.arguments.front(), frequency, true)) {
        return Failure(DATA_TYPE_ERROR, "Marker frequency requires a finite value in Hz");
    }
    change.marker.hasFrequency = true;
    change.marker.frequencyHz = frequency;
    auto result = Handled();
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::Trace(const ParsedCommand &command, unsigned slot, const std::string &leaf) const
{
    if (leaf != "SEL" && !leaf.empty() && leaf != "STAT" && leaf != "Y:AUTO" && leaf != "Y:PDIV" && leaf != "Y:RLEV" &&
        leaf != "Y:RPOS") {
        return ScpiDisplayResult();
    }
    if (command.query && (leaf == "SEL" || leaf == "Y:AUTO")) {
        return Failure(UNDEFINED_HEADER, "Display action is write-only");
    }
    auto result = Handled();
    const bool action = leaf == "SEL" || leaf == "Y:AUTO";
    if (!Arguments(command, command.query || action ? 0U : 1U, result)) {
        return result;
    }
    const auto &state = service_.Display();
    if (slot == 0U || slot > VnaDisplay::MAX_DISPLAY_TRACES || slot > state.displayedNames.size()) {
        return Failure(DATA_OUT_OF_RANGE, "Display trace slot does not exist");
    }
    const auto *trace = Presentation(state, state.displayedNames[slot - 1U]);
    if (trace == nullptr) {
        return Failure(SETTINGS_CONFLICT, "Displayed measurement does not exist");
    }
    if (command.query) {
        return ReadTrace(*trace, leaf);
    }
    VnaDisplay::DisplayChange change;
    change.hasSlot = true;
    change.slot = slot;
    return SetTrace(command, *trace, std::move(change), leaf);
}

ScpiDisplayResult ScpiDisplayAdapter::ReadTrace(
    const VnaDisplay::TracePresentation &trace, const std::string &leaf) const
{
    auto result = Handled();
    const double division = (trace.scale.max - trace.scale.min) / 10.0;
    if (leaf.empty() || leaf == "STAT") {
        result.response = trace.visible ? "1" : "0";
    } else if (leaf == "Y:PDIV") {
        result.response = Number(trace.format == VnaDisplay::DisplayFormat::SMITH ? trace.smithRadius : division);
    } else if (trace.format == VnaDisplay::DisplayFormat::SMITH) {
        return Failure(SETTINGS_CONFLICT, "Smith format does not use reference level or position");
    } else if (leaf == "Y:RLEV") {
        result.response = Number(trace.scale.min + trace.scale.referencePosition * division);
    } else {
        result.response = Number(trace.scale.referencePosition);
    }
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::SetTrace(const ParsedCommand &command, const VnaDisplay::TracePresentation &trace,
    VnaDisplay::DisplayChange change, const std::string &leaf) const
{
    auto result = Handled();
    if (leaf == "SEL" || leaf == "Y:AUTO") {
        change.kind = leaf == "SEL" ? VnaDisplay::DisplayChangeKind::SELECT : VnaDisplay::DisplayChangeKind::AUTOSCALE;
    } else if (leaf.empty() || leaf == "STAT") {
        change.kind = VnaDisplay::DisplayChangeKind::VISIBLE;
        if (!Boolean(command.arguments.front(), change.visible)) {
            return Failure(ILLEGAL_PARAMETER_VALUE, "Trace visibility requires ON or OFF");
        }
    } else {
        return SetScale(command, trace, std::move(change), leaf);
    }
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

ScpiDisplayResult ScpiDisplayAdapter::SetScale(const ParsedCommand &command, const VnaDisplay::TracePresentation &trace,
    VnaDisplay::DisplayChange change, const std::string &leaf) const
{
    if (trace.format == VnaDisplay::DisplayFormat::SMITH && leaf != "Y:PDIV") {
        return Failure(SETTINGS_CONFLICT, "Smith format does not use reference level or position");
    }
    double value = 0.0;
    if (!Numeric(command.arguments.front(), value)) {
        return Failure(DATA_TYPE_ERROR, "Scale requires a finite numeric value");
    }
    change.kind = VnaDisplay::DisplayChangeKind::SCALE;
    if (trace.format == VnaDisplay::DisplayFormat::SMITH) {
        change.kind = VnaDisplay::DisplayChangeKind::SMITH_RADIUS;
        change.smithRadius = value;
    } else if (leaf == "Y:PDIV") {
        change.scale.hasPerDivision = true;
        change.scale.perDivision = value;
    } else if (leaf == "Y:RLEV") {
        change.scale.hasReference = true;
        change.scale.reference = value;
    } else {
        change.scale.hasReferencePosition = true;
        change.scale.referencePosition = value;
    }
    auto result = Handled();
    result.operation = service_.ApplyDisplay(std::move(change));
    return result;
}

}  // 命名空间 VnaScpi
