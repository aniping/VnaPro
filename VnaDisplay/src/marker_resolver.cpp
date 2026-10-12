/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "marker_resolver.h"

#include <algorithm>
#include <cmath>

namespace VnaDisplay {
namespace Detail {
namespace {

bool CurrentFrame(const FrameContext &context) noexcept
{
    return context.current && context.frame && context.frame->configRevision == context.acquisitionRevision &&
           context.frame->runId == context.runId && MarkerResolver::ValidAxis(context.frame->plan.frequencyHz);
}

std::size_t NearestIndex(const std::vector<double> &axis, double requested)
{
    if (axis.front() == axis.back()) {
        return (axis.size() - 1U) / 2U;
    }
    const auto upper = std::lower_bound(axis.begin(), axis.end(), requested);
    if (upper == axis.end()) {
        return axis.size() - 1U;
    }
    const auto index = static_cast<std::size_t>(upper - axis.begin());
    return index > 0U && requested - axis[index - 1U] <= axis[index] - requested ? index - 1U : index;
}

const std::vector<double> *StimulusAxis(const FrameContext &context) noexcept
{
    if (context.frequencyHz != nullptr) {
        return MarkerResolver::ValidAxis(*context.frequencyHz) ? context.frequencyHz : nullptr;
    }
    return CurrentFrame(context) ? &context.frame->plan.frequencyHz : nullptr;
}

bool ValidateFrequency(const MarkerChange &change, const std::vector<double> *axis, std::string &error)
{
    if (!change.hasFrequency) {
        return true;
    }
    if (!std::isfinite(change.frequencyHz) || change.frequencyHz < 0.0) {
        error = "Marker frequency must be finite and nonnegative";
        return false;
    }
    if (axis == nullptr) {
        error = "Marker frequency requires a verified committed stimulus axis";
        return false;
    }
    if (change.frequencyHz < axis->front() || change.frequencyHz > axis->back()) {
        error = "Marker frequency is outside the measurement span";
        return false;
    }
    return true;
}

void StoreIndex(
    MarkerSettings &settings, std::size_t index, const FrameContext &context, const std::vector<double> &axis)
{
    settings.hasFrequency = true;
    settings.frequencyHz = axis[index];
    settings.hasSampleIndex = true;
    settings.sampleIndex = index;
    settings.interpolate = false;
    settings.acquisitionRevision = context.acquisitionRevision;
}

void CanonicalPosition(MarkerSettings &settings, const FrameContext &context, const std::vector<double> &axis)
{
    if (!settings.hasFrequency) {
        settings.hasFrequency = true;
        settings.frequencyHz = axis.front() / 2.0 + axis.back() / 2.0;
    }
    settings.frequencyHz = std::max(axis.front(), std::min(axis.back(), settings.frequencyHz));
    if (!settings.interpolate) {
        const auto index = settings.hasSampleIndex && settings.acquisitionRevision == context.acquisitionRevision &&
                                   settings.sampleIndex < axis.size()
                               ? settings.sampleIndex
                               : NearestIndex(axis, settings.frequencyHz);
        StoreIndex(settings, index, context, axis);
    }
}

bool ApplySamplePosition(
    MarkerSettings &settings, const MarkerChange &change, const FrameContext &context, std::string &error)
{
    if (!CurrentFrame(context)) {
        error = "Marker sample movement requires a current complete frame";
        return false;
    }
    const auto &axis = context.frame->plan.frequencyHz;
    std::size_t index = change.sampleIndex;
    if (change.hasStep) {
        index = settings.hasSampleIndex && settings.acquisitionRevision == context.acquisitionRevision &&
                        settings.sampleIndex < axis.size()
                    ? settings.sampleIndex
                    : NearestIndex(
                          axis, settings.hasFrequency ? settings.frequencyHz : axis.front() / 2.0 + axis.back() / 2.0);
        if (change.step < 0) {
            const auto distance = static_cast<std::size_t>(-static_cast<long long>(change.step));
            index = distance > index ? 0U : index - distance;
        } else {
            index += std::min(static_cast<std::size_t>(change.step), axis.size() - 1U - index);
        }
    }
    StoreIndex(settings, std::min(index, axis.size() - 1U), context, axis);
    return true;
}

MarkerReadout ReadPoint(
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &axis, std::size_t index, DisplayFormat format)
{
    MarkerReadout result;
    result.available = true;
    result.sampleIndex = index;
    result.frequencyHz = axis[index];
    const auto &point = trace.points[index];
    result.valid = point.valid && std::isfinite(point.real) && std::isfinite(point.imag);
    result.real = point.real;
    result.imag = point.imag;
    result.value = TraceProjection::Format(point, format);
    if (result.valid) {
        result.impedance = TraceProjection::Impedance(point.real, point.imag);
    }
    return result;
}

MarkerReadout Interpolate(
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &axis, double requested, DisplayFormat format)
{
    const auto upper = std::lower_bound(axis.begin(), axis.end(), requested);
    const auto index = upper == axis.end() ? axis.size() - 1U : static_cast<std::size_t>(upper - axis.begin());
    if (index == 0U || axis[index] == requested) {
        return ReadPoint(trace, axis, index, format);
    }
    MarkerReadout result = ReadPoint(trace, axis, index - 1U, format);
    const auto &left = trace.points[index - 1U];
    const auto &right = trace.points[index];
    const double weight = (requested - axis[index - 1U]) / (axis[index] - axis[index - 1U]);
    VnaMeasurement::ComplexPoint point;
    point.real = (1.0 - weight) * left.real + weight * right.real;
    point.imag = (1.0 - weight) * left.imag + weight * right.imag;
    point.valid = left.valid && right.valid && std::isfinite(point.real) && std::isfinite(point.imag);
    result.interpolated = true;
    result.frequencyHz = requested;
    result.valid = point.valid;
    result.real = point.real;
    result.imag = point.imag;
    result.value = TraceProjection::Format(point, format);
    result.impedance = point.valid ? TraceProjection::Impedance(point.real, point.imag) : NormalizedImpedance{};
    return result;
}

}  // 匿名命名空间

bool MarkerResolver::ValidAxis(const std::vector<double> &frequencyHz) noexcept
{
    if (frequencyHz.empty()) {
        return false;
    }
    double previous = frequencyHz.front();
    for (const double frequency : frequencyHz) {
        if (!std::isfinite(frequency) || frequency < 0.0 || frequency < previous) {
            return false;
        }
        previous = frequency;
    }
    return true;
}

void MarkerResolver::Reconcile(
    MarkerSettings &settings, const std::vector<double> &axis, bool preserveSampleIndexes) noexcept
{
    if (!settings.hasFrequency && !settings.enabled) {
        return;
    }
    if (!settings.hasFrequency) {
        settings.hasFrequency = true;
        settings.frequencyHz = axis.front() / 2.0 + axis.back() / 2.0;
    }
    settings.frequencyHz = std::max(axis.front(), std::min(axis.back(), settings.frequencyHz));
    if (settings.interpolate) {
        settings.hasSampleIndex = false;
        return;
    }
    const auto index = preserveSampleIndexes && settings.hasSampleIndex && settings.sampleIndex < axis.size()
                           ? settings.sampleIndex
                           : NearestIndex(axis, settings.frequencyHz);
    settings.sampleIndex = index;
    settings.hasSampleIndex = true;
    settings.frequencyHz = axis[index];
}

bool MarkerResolver::Apply(
    MarkerSettings &settings, const MarkerChange &change, const FrameContext &context, std::string &error)
{
    if (change.checkAcquisitionRevision && change.expectedAcquisitionRevision != context.acquisitionRevision) {
        error = "Acquisition revision changed";
        return false;
    }
    const auto axis = StimulusAxis(context);
    if (!ValidateFrequency(change, axis, error)) {
        return false;
    }
    if ((change.hasFrequency && (change.hasSampleIndex || change.hasStep)) ||
        (change.hasSampleIndex && change.hasStep)) {
        error = "Marker position modes cannot be combined";
        return false;
    }
    if (change.hasEnabled) {
        settings.enabled = change.enabled;
    }
    if (change.hasInterpolation) {
        settings.interpolate = change.interpolate;
        if (settings.interpolate) {
            settings.hasSampleIndex = false;
        }
    }
    if (change.hasFrequency) {
        settings.hasFrequency = true;
        settings.frequencyHz = change.frequencyHz;
        settings.hasSampleIndex = false;
    }
    if (change.hasSampleIndex || change.hasStep) {
        return ApplySamplePosition(settings, change, context, error);
    }
    if (axis != nullptr && (settings.enabled || change.hasFrequency || change.hasInterpolation)) {
        CanonicalPosition(settings, context, *axis);
    }
    return true;
}

MarkerReadout MarkerResolver::Resolve(const MarkerSettings &settings, DisplayFormat format,
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &axis, std::uint64_t acquisitionRevision)
{
    if (!settings.enabled || !trace.available || trace.points.size() != axis.size() || !ValidAxis(axis)) {
        return {};
    }
    if (settings.hasSampleIndex && settings.sampleIndex < axis.size() &&
        (acquisitionRevision == 0U || settings.acquisitionRevision == acquisitionRevision)) {
        return ReadPoint(trace, axis, settings.sampleIndex, format);
    }
    const double requested = settings.hasFrequency ? settings.frequencyHz : axis.front() / 2.0 + axis.back() / 2.0;
    const double frequency = std::max(axis.front(), std::min(axis.back(), requested));
    if (!settings.interpolate || axis.front() == axis.back()) {
        return ReadPoint(trace, axis, NearestIndex(axis, frequency), format);
    }
    return Interpolate(trace, axis, frequency, format);
}

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay
