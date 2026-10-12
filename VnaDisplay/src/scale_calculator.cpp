/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "scale_calculator.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
        const double span = format == DisplayFormat::DB || format == DisplayFormat::PHASE ? 10.0 : 0.2;
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

}  // 匿名命名空间

bool ScaleCalculator::Apply(DisplayScale &scale, const ScaleChange &change, std::string &error)
{
    if (!change.hasMin && !change.hasMax && !change.hasPerDivision && !change.hasReference &&
        !change.hasReferencePosition) {
        return true;
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

bool ScaleCalculator::Autoscale(
    const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale, double &smithRadius)
{
    if (!trace.available) {
        return false;
    }
    if (format == DisplayFormat::SMITH) {
        return FitSmith(trace, smithRadius);
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const auto &point : trace.points) {
        Include(TraceProjection::Format(point, format), minimum, maximum);
    }
    return std::isfinite(minimum) && FitCartesian(minimum, maximum, format, scale);
}

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay
