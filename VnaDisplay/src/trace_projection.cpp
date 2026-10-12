/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/trace_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "marker_resolver.h"
#include "scale_calculator.h"

namespace VnaDisplay {
namespace {

constexpr double PI = 3.14159265358979323846;

double LogMagnitude(double real, double imag) noexcept
{
    const double maximum = std::max(std::abs(real), std::abs(imag));
    if (maximum == 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    const double scaledReal = real / maximum;
    const double scaledImag = imag / maximum;
    return 20.0 * (std::log10(maximum) + 0.5 * std::log10(scaledReal * scaledReal + scaledImag * scaledImag));
}

FormattedPoint FormatFinite(const VnaMeasurement::ComplexPoint &point, DisplayFormat format) noexcept
{
    FormattedPoint value;
    value.state = DisplayValueState::FINITE;
    switch (format) {
        case DisplayFormat::DB:
            value.primary = LogMagnitude(point.real, point.imag);
            break;
        case DisplayFormat::MAGNITUDE:
            value.primary = std::hypot(point.real, point.imag);
            break;
        case DisplayFormat::PHASE:
            if (point.real == 0.0 && point.imag == 0.0) {
                value.state = DisplayValueState::UNDEFINED;
                return value;
            }
            value.primary = std::atan2(point.imag, point.real) * 180.0 / PI;
            break;
        case DisplayFormat::IMAG:
            value.primary = point.imag;
            break;
        case DisplayFormat::REALIMAG:
        case DisplayFormat::SMITH:
            value.secondary = point.imag;
            value.hasSecondary = true;
            value.primary = point.real;
            break;
        case DisplayFormat::REAL:
            value.primary = point.real;
            break;
        default:
            value.state = DisplayValueState::INVALID;
            break;
    }
    return value;
}

}  // 匿名命名空间

DisplayScale TraceProjection::DefaultScale(DisplayFormat format) noexcept
{
    switch (format) {
        case DisplayFormat::MAGNITUDE:
            return {0.0, 2.0, 8.0};
        case DisplayFormat::PHASE:
            return {-180.0, 180.0, 8.0};
        case DisplayFormat::REAL:
        case DisplayFormat::IMAG:
        case DisplayFormat::REALIMAG:
        case DisplayFormat::SMITH:
            return {-1.0, 1.0, 8.0};
        default:
            return {-90.0, 10.0, 9.0};
    }
}

bool TraceProjection::SupportsSmith(const VnaMeasurement::Measurement &measurement) noexcept
{
    return measurement.kind == VnaMeasurement::MeasurementKind::S_PARAMETER &&
           measurement.receiverPort == measurement.sourcePort && measurement.IsSupported();
}

std::vector<DisplayFormat> TraceProjection::AllowedFormats(const VnaMeasurement::Measurement &measurement)
{
    std::vector<DisplayFormat> formats{DisplayFormat::DB, DisplayFormat::MAGNITUDE, DisplayFormat::PHASE,
        DisplayFormat::REAL, DisplayFormat::IMAG, DisplayFormat::REALIMAG};
    if (SupportsSmith(measurement)) {
        formats.push_back(DisplayFormat::SMITH);
    }
    return formats;
}

FormattedPoint TraceProjection::Format(const VnaMeasurement::ComplexPoint &point, DisplayFormat format) noexcept
{
    if (!point.valid || !std::isfinite(point.real) || !std::isfinite(point.imag)) {
        return {};
    }
    auto value = FormatFinite(point, format);
    if (!std::isfinite(value.primary)) {
        value.state = value.primary == -std::numeric_limits<double>::infinity() ? DisplayValueState::NEGATIVE_INFINITY
                                                                                : DisplayValueState::OVERFLOW;
    }
    return value;
}

NormalizedImpedance TraceProjection::Impedance(double real, double imag) noexcept
{
    NormalizedImpedance value;
    if (!std::isfinite(real) || !std::isfinite(imag)) {
        return value;
    }
    if (real == 1.0 && imag == 0.0) {
        value.state = ImpedanceState::OPEN;
        return value;
    }
    // 同时缩放分子与分母，避免大 Γ 的中间平方溢出。
    const double scale = std::max(std::abs(1.0 - real), std::abs(imag));
    const double numeratorReal = real / scale + 1.0 / scale;
    const double numeratorImag = imag / scale;
    const double denominatorReal = (1.0 - real) / scale;
    const double denominatorImag = -imag / scale;
    const double denominator = denominatorReal * denominatorReal + denominatorImag * denominatorImag;
    value.real = (numeratorReal * denominatorReal + numeratorImag * denominatorImag) / denominator;
    value.imag = (numeratorImag * denominatorReal - numeratorReal * denominatorImag) / denominator;
    value.state =
        std::isfinite(value.real) && std::isfinite(value.imag) ? ImpedanceState::FINITE : ImpedanceState::OVERFLOW;
    return value;
}

ProjectedTrace TraceProjection::Calculate(const TracePresentation &presentation,
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz, std::uint64_t acquisitionRevision)
{
    ProjectedTrace projected;
    if (!trace.available || trace.points.size() != frequencyHz.size() ||
        !Detail::MarkerResolver::ValidAxis(frequencyHz)) {
        projected.error = trace.error.empty() ? "No complete trace with a valid physical frequency axis" : trace.error;
        return projected;
    }
    projected.available = true;
    projected.points.reserve(trace.points.size());
    const bool smith = presentation.format == DisplayFormat::SMITH;
    if (smith) {
        projected.impedancePoints.reserve(trace.points.size());
    }
    for (const auto &point : trace.points) {
        projected.points.push_back(Format(point, presentation.format));
        if (smith) {
            projected.impedancePoints.push_back(
                point.valid ? Impedance(point.real, point.imag) : NormalizedImpedance{});
        }
    }
    projected.marker = Detail::MarkerResolver::Resolve(
        presentation.marker, presentation.format, trace, frequencyHz, acquisitionRevision);
    return projected;
}

bool TraceProjection::Autoscale(
    const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale, double &smithRadius)
{
    return Detail::ScaleCalculator::Autoscale(trace, format, scale, smithRadius);
}

}  // 命名空间 VnaDisplay
