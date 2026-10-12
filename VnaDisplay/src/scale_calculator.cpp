/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "scale_calculator.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "trace_sequence_formatter.h"
#include "vna_display/trace_projection.h"

namespace VnaDisplay {
namespace Detail {
namespace {

constexpr double DIVISION_COUNT = 10.0;
constexpr double MIN_SMITH_RADIUS = 0.01;

bool ValidScale(const DisplayScale &scale) noexcept
{
    return std::isfinite(scale.min) && std::isfinite(scale.max) && std::isfinite(scale.max - scale.min) &&
           scale.max > scale.min && std::isfinite(scale.referencePosition) && scale.referencePosition >= 0.0 &&
           scale.referencePosition <= DIVISION_COUNT;
}

double NiceCeiling(double value) noexcept
{
    const double power = std::pow(10.0, std::floor(std::log10(value)));
    const double normalized = value / power;
    for (const double step : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (normalized <= step) {
            return step * power;
        }
    }
    return 10.0 * power;
}

void Include(const FormattedPoint &point, double &minimum, double &maximum)
{
    if (point.state != DisplayValueState::FINITE) {
        return;
    }
    minimum = std::min(minimum, point.primary);
    maximum = std::max(maximum, point.primary);
    if (point.hasSecondary) {
        minimum = std::min(minimum, point.secondary);
        maximum = std::max(maximum, point.secondary);
    }
}

bool FitSmith(const VnaMeasurement::TraceResult &trace, double &radius)
{
    double maximum = -1.0;
    for (const auto &point : trace.points) {
        if (point.valid && std::isfinite(point.real) && std::isfinite(point.imag)) {
            maximum = std::max(maximum, std::hypot(point.real, point.imag));
        }
    }
    const double candidate = std::max(MIN_SMITH_RADIUS, maximum * 1.2);
    if (maximum < 0.0 || !std::isfinite(candidate)) {
        return false;
    }
    radius = candidate;
    return true;
}

bool FitCartesian(double minimum, double maximum, DisplayFormat format, DisplayScale &scale)
{
    DisplayScale candidate = scale;
    if (maximum == minimum) {
        double span = 0.2;
        if (format == DisplayFormat::DB || format == DisplayFormat::PHASE || format == DisplayFormat::UNWRAPPED_PHASE) {
            span = 10.0;
        } else if (format == DisplayFormat::DELAY) {
            span = 1e-9;
        } else if (format == DisplayFormat::DELAY_DERIVATION) {
            span = 1e-18;
        }
        candidate.min = minimum - span / 2.0;
        candidate.max = maximum + span / 2.0;
    } else {
        const double span = (maximum - minimum) * 1.25;
        if (!std::isfinite(span) || span <= 0.0) {
            return false;
        }
        const double division = NiceCeiling(span / DIVISION_COUNT);
        const double center = minimum / 2.0 + maximum / 2.0;
        candidate.min = std::floor((center - division * 5.0) / division) * division;
        candidate.max = candidate.min + DIVISION_COUNT * division;
    }
    if (!ValidScale(candidate)) {
        return false;
    }
    scale = candidate;
    return true;
}

bool PositiveRange(const DisplayScale &scale) noexcept
{
    return ValidScale(scale) && scale.min > 0.0;
}

bool StoreLogRange(DisplayScale &scale, const DisplayScale &logScale)
{
    DisplayScale candidate = logScale;
    candidate.min = std::pow(10.0, logScale.min);
    candidate.max = std::pow(10.0, logScale.max);
    if (!PositiveRange(candidate)) {
        return false;
    }
    scale = candidate;
    return true;
}

bool ApplyLogRange(DisplayScale &scale, const ScaleChange &change, std::string &error)
{
    if (!PositiveRange(scale) || (change.hasMin && (!std::isfinite(change.min) || change.min <= 0.0)) ||
        (change.hasMax && (!std::isfinite(change.max) || change.max <= 0.0)) ||
        (change.hasReference && (!std::isfinite(change.reference) || change.reference <= 0.0))) {
        error = "Log magnitude scale requires positive finite limits and reference";
        return false;
    }
    DisplayScale logScale{std::log10(scale.min), std::log10(scale.max), scale.referencePosition};
    auto logChange = change;
    if (change.hasMin) {
        logChange.min = std::log10(change.min);
    }
    if (change.hasMax) {
        logChange.max = std::log10(change.max);
    }
    if (change.hasReference) {
        logChange.reference = std::log10(change.reference);
    }
    // 每格为 decade，参考值为正幅值；候选转换后复用线性尺度的同一联动规则。
    if (!ScaleCalculator::Apply(logScale, logChange, error) || !StoreLogRange(scale, logScale)) {
        if (error.empty()) {
            error = "Log magnitude scale is outside the finite positive range";
        }
        return false;
    }
    return true;
}

}  // 匿名命名空间

bool ScaleCalculator::Apply(DisplayScale &scale, const ScaleChange &change, std::string &error, DisplayFormat format)
{
    if (!change.hasMin && !change.hasMax && !change.hasPerDivision && !change.hasReference &&
        !change.hasReferencePosition) {
        return true;
    }
    if (format == DisplayFormat::LOG_MAGNITUDE) {
        return ApplyLogRange(scale, change, error);
    }
    DisplayScale candidate = scale;
    if ((change.hasMin || change.hasMax) && (change.hasPerDivision || change.hasReference)) {
        error = "Range and reference/division cannot be changed together";
        return false;
    }
    const double division = change.hasPerDivision ? change.perDivision : (scale.max - scale.min) / DIVISION_COUNT;
    const double reference = change.hasReference
                                 ? change.reference
                                 : scale.min + scale.referencePosition * (scale.max - scale.min) / DIVISION_COUNT;
    candidate.referencePosition = change.hasReferencePosition ? change.referencePosition : scale.referencePosition;
    if (change.hasMin || change.hasMax) {
        candidate.min = change.hasMin ? change.min : scale.min;
        candidate.max = change.hasMax ? change.max : scale.max;
    } else {
        candidate.min = reference - candidate.referencePosition * division;
        candidate.max = reference + (DIVISION_COUNT - candidate.referencePosition) * division;
    }
    if (!std::isfinite(division) || division <= 0.0 || !std::isfinite(reference) || !ValidScale(candidate)) {
        error = "Scale requires finite min < max, positive division and a reference position in 0..10";
        return false;
    }
    scale = candidate;
    return true;
}

bool ScaleCalculator::Autoscale(const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale,
    double &smithRadius, const std::vector<double> &frequencyHz)
{
    if (!trace.available) {
        return false;
    }
    if (TraceProjection::IsCircular(format)) {
        return FitSmith(trace, smithRadius);
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (auto point : TraceSequenceFormatter::Calculate(trace, frequencyHz, format)) {
        if (format == DisplayFormat::LOG_MAGNITUDE) {
            if (point.state != DisplayValueState::FINITE || point.primary <= 0.0) {
                continue;
            }
            point.primary = std::log10(point.primary);
        }
        Include(point, minimum, maximum);
    }
    if (format == DisplayFormat::LOG_MAGNITUDE) {
        DisplayScale logScale{0.0, 1.0, scale.referencePosition};
        return std::isfinite(minimum) && FitCartesian(minimum, maximum, format, logScale) &&
               StoreLogRange(scale, logScale);
    }
    return std::isfinite(minimum) && FitCartesian(minimum, maximum, format, scale);
}

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay
