/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/trace_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "marker_resolver.h"
#include "scale_calculator.h"
#include "trace_sequence_formatter.h"

namespace VnaDisplay {
namespace {

constexpr double PI = 3.14159265358979323846;

double DbMagnitude(double real, double imag) noexcept
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
            value.primary = DbMagnitude(point.real, point.imag);
            break;
        case DisplayFormat::MAGNITUDE:
        case DisplayFormat::LOG_MAGNITUDE:
            value.primary = std::hypot(point.real, point.imag);
            break;
        case DisplayFormat::SWR: {
            const double magnitude = std::hypot(point.real, point.imag);
            // 大幅值使用倒数形式，避免可表示的 SWR 被中间 1 + 幅值溢出破坏。
            value.primary = magnitude > 1.0 ? -(1.0 + 1.0 / magnitude) / (1.0 - 1.0 / magnitude)
                                            : (1.0 + magnitude) / (1.0 - magnitude);
            break;
        }
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
        case DisplayFormat::POLAR:
        case DisplayFormat::SMITH:
        case DisplayFormat::INV_SMITH:
            value.secondary = point.imag;
            value.hasSecondary = true;
            value.primary = point.real;
            break;
        case DisplayFormat::REAL:
            value.primary = point.real;
            break;
        case DisplayFormat::UNWRAPPED_PHASE:
        case DisplayFormat::DELAY:
        case DisplayFormat::DELAY_DERIVATION:
            // 序列格式缺少完整频轴时不能伪造单点相位或零延迟。
            value.state = DisplayValueState::UNDEFINED;
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
        case DisplayFormat::LOG_MAGNITUDE:
            return {0.001, 10.0, 8.0};
        case DisplayFormat::SWR:
            return {1.0, 11.0, 8.0};
        case DisplayFormat::PHASE:
        case DisplayFormat::UNWRAPPED_PHASE:
            return {-180.0, 180.0, 8.0};
        case DisplayFormat::DELAY:
            return {-1e-9, 9e-9, 8.0};
        case DisplayFormat::DELAY_DERIVATION:
            return {-1e-18, 9e-18, 8.0};
        case DisplayFormat::REAL:
        case DisplayFormat::IMAG:
        case DisplayFormat::REALIMAG:
        case DisplayFormat::POLAR:
        case DisplayFormat::SMITH:
        case DisplayFormat::INV_SMITH:
            return {-1.0, 1.0, 8.0};
        default:
            return {-90.0, 10.0, 9.0};
    }
}

bool TraceProjection::IsCircular(DisplayFormat format) noexcept
{
    return format == DisplayFormat::POLAR || format == DisplayFormat::SMITH || format == DisplayFormat::INV_SMITH;
}

bool TraceProjection::SupportsSmith(const VnaMeasurement::Measurement &measurement) noexcept
{
    return measurement.IsSupported();
}

bool TraceProjection::SupportsReflectionInterpretation(const VnaMeasurement::Measurement &measurement) noexcept
{
    return measurement.kind == VnaMeasurement::MeasurementKind::S_PARAMETER &&
           measurement.receiverPort == measurement.sourcePort && measurement.IsSupported();
}

std::vector<DisplayFormat> TraceProjection::AllowedFormats(const VnaMeasurement::Measurement &measurement)
{
    if (!measurement.IsSupported()) {
        return {};
    }
    return {DisplayFormat::DB, DisplayFormat::PHASE, DisplayFormat::SMITH, DisplayFormat::POLAR, DisplayFormat::SWR,
        DisplayFormat::UNWRAPPED_PHASE, DisplayFormat::MAGNITUDE, DisplayFormat::LOG_MAGNITUDE, DisplayFormat::REAL,
        DisplayFormat::IMAG, DisplayFormat::INV_SMITH, DisplayFormat::DELAY, DisplayFormat::DELAY_DERIVATION};
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

FormattedPoint TraceProjection::MarkerValue(const VnaMeasurement::ComplexPoint &point, DisplayFormat format) noexcept
{
    if (format != DisplayFormat::POLAR) {
        return Format(point, format);
    }
    // 官方 Polar 示例的默认读数是幅值与相位；实虚数仍由圆图坐标保存。
    auto value = Format(point, DisplayFormat::MAGNITUDE);
    const auto phase = Format(point, DisplayFormat::PHASE);
    value.hasSecondary = true;
    value.secondary = phase.primary;
    value.secondaryState = phase.state;
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

NormalizedAdmittance TraceProjection::Admittance(double real, double imag) noexcept
{
    // y(Γ) = z(-Γ)；仅计算导纳值，Inv Smith 的迹线坐标仍使用原 Γ。
    const auto impedance = Impedance(-real, -imag);
    NormalizedAdmittance value;
    value.real = impedance.real;
    value.imag = impedance.imag;
    switch (impedance.state) {
        case ImpedanceState::FINITE:
            value.state = AdmittanceState::FINITE;
            break;
        case ImpedanceState::OPEN:
            value.state = AdmittanceState::SHORT;
            break;
        case ImpedanceState::OVERFLOW:
            value.state = AdmittanceState::OVERFLOW;
            break;
        default:
            break;
    }
    return value;
}

namespace {

ProjectedTrace ProjectTrace(const TracePresentation &presentation, const VnaMeasurement::TraceResult &trace,
    const std::vector<double> &frequencyHz, std::uint64_t acquisitionRevision, bool reflection)
{
    ProjectedTrace projected;
    projected.activeMarkerId = presentation.markers.Selected();
    if (!trace.available || trace.points.size() != frequencyHz.size() ||
        !Detail::MarkerResolver::ValidAxis(frequencyHz)) {
        projected.error = trace.error.empty() ? "No complete trace with a valid physical frequency axis" : trace.error;
    } else {
        projected.available = true;
        projected.points = Detail::TraceSequenceFormatter::Calculate(trace, frequencyHz, presentation.format);
        if (presentation.format == DisplayFormat::POLAR) {
            projected.markerPoints.reserve(trace.points.size());
            for (const auto &point : trace.points) {
                projected.markerPoints.push_back(TraceProjection::MarkerValue(point, presentation.format));
            }
        }
        if (TraceProjection::IsCircular(presentation.format) && reflection) {
            projected.impedancePoints.reserve(trace.points.size());
            projected.admittancePoints.reserve(trace.points.size());
            for (const auto &point : trace.points) {
                projected.impedancePoints.push_back(
                    point.valid ? TraceProjection::Impedance(point.real, point.imag) : NormalizedImpedance{});
                projected.admittancePoints.push_back(
                    point.valid ? TraceProjection::Admittance(point.real, point.imag) : NormalizedAdmittance{});
            }
        }
    }
    const Detail::MarkerProjectionContext context{
        trace, frequencyHz, projected.points, presentation.format, acquisitionRevision, reflection};
    for (unsigned id = 1U; id <= MAX_MARKERS; ++id) {
        auto readout = Detail::MarkerResolver::Resolve(*presentation.markers.Find(id), context);
        readout.id = id;
        projected.markers.push_back(std::move(readout));
    }
    projected.marker = projected.markers[projected.activeMarkerId - 1U];
    return projected;
}

}  // 匿名命名空间

ProjectedTrace TraceProjection::Calculate(const TracePresentation &presentation,
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz, std::uint64_t acquisitionRevision)
{
    return ProjectTrace(presentation, trace, frequencyHz, acquisitionRevision, false);
}

ProjectedTrace TraceProjection::Calculate(const TracePresentation &presentation,
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz,
    const VnaMeasurement::Measurement &measurement, std::uint64_t acquisitionRevision)
{
    return ProjectTrace(
        presentation, trace, frequencyHz, acquisitionRevision, SupportsReflectionInterpretation(measurement));
}

bool TraceProjection::Autoscale(const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale,
    double &smithRadius, const std::vector<double> &frequencyHz)
{
    return Detail::ScaleCalculator::Autoscale(trace, format, scale, smithRadius, frequencyHz);
}

}  // 命名空间 VnaDisplay
