/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "trace_sequence_formatter.h"

#include <cmath>

#include "marker_resolver.h"

namespace VnaDisplay {
namespace Detail {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double FULL_TURN = 360.0;

std::vector<FormattedPoint> Unwrap(const VnaMeasurement::TraceResult &trace)
{
    std::vector<FormattedPoint> values;
    values.reserve(trace.points.size());
    bool havePrevious = false;
    double previous = 0.0;
    double unwrapped = 0.0;
    for (const auto &point : trace.points) {
        auto value = TraceProjection::Format(point, DisplayFormat::PHASE);
        if (value.state == DisplayValueState::FINITE) {
            const double wrapped = value.primary;
            unwrapped = havePrevious ? unwrapped + std::remainder(wrapped - previous, FULL_TURN) : wrapped;
            previous = wrapped;
            havePrevious = true;
            value.primary = unwrapped;
        } else {
            // 不跨无效点或零幅度接续相位，后续有效段重新选择自己的分支。
            havePrevious = false;
        }
        values.push_back(value);
    }
    return values;
}

// 一段连续有效相位及其频轴共同决定导数，边界不得跨到其他有效段。
struct PhaseSegment final {
    const std::vector<FormattedPoint> &phase;
    const std::vector<double> &frequencyHz;
    std::size_t first = 0U;
    std::size_t last = 0U;
};

FormattedPoint Derivative(const PhaseSegment &segment, std::size_t index, DisplayFormat format)
{
    const auto &phase = segment.phase;
    const auto &axis = segment.frequencyHz;
    const auto first = segment.first;
    const auto last = segment.last;
    FormattedPoint value;
    value.state = DisplayValueState::UNDEFINED;
    const std::size_t count = last - first;
    if (count < 2U || (format == DisplayFormat::DELAY_DERIVATION && count < 3U)) {
        return value;
    }
    std::size_t left = index == first ? first : index - 1U;
    if (count >= 3U) {
        left = index + 1U == last ? last - 3U : left;
    }
    const auto middle = left + 1U;
    const double leftStep = axis[middle] - axis[left];
    const double leftSlope = (phase[middle].primary - phase[left].primary) / leftStep;
    double derivative = leftSlope;
    if (count >= 3U) {
        const auto right = left + 2U;
        const double rightStep = axis[right] - axis[middle];
        const double rightSlope = (phase[right].primary - phase[middle].primary) / rightStep;
        const double totalStep = leftStep + rightStep;
        if (format == DisplayFormat::DELAY_DERIVATION) {
            // 相位单位为度；先求对 Hz 的二阶导数，再换为对角频率的导数，结果为 s²。
            derivative = 2.0 * (rightSlope - leftSlope) / totalStep;
            value.primary = -derivative / (FULL_TURN * 2.0 * PI);
        } else if (index == left) {
            derivative = leftSlope - (leftStep / totalStep) * (rightSlope - leftSlope);
        } else if (index == right) {
            derivative = rightSlope + (rightStep / totalStep) * (rightSlope - leftSlope);
        } else {
            // 非均匀频轴采用二次插值导数，均匀频轴自然退化为中心差分。
            derivative = (rightStep / totalStep) * leftSlope + (leftStep / totalStep) * rightSlope;
        }
    }
    if (format == DisplayFormat::DELAY) {
        value.primary = -derivative / FULL_TURN;
    }
    value.state = std::isfinite(value.primary) ? DisplayValueState::FINITE : DisplayValueState::OVERFLOW;
    return value;
}

std::vector<FormattedPoint> Differentiate(
    const std::vector<FormattedPoint> &phase, const std::vector<double> &axis, DisplayFormat format)
{
    auto values = phase;
    for (std::size_t first = 0U; first < phase.size();) {
        if (phase[first].state != DisplayValueState::FINITE) {
            ++first;
            continue;
        }
        std::size_t last = first + 1U;
        while (last < phase.size() && phase[last].state == DisplayValueState::FINITE && axis[last] > axis[last - 1U]) {
            ++last;
        }
        const PhaseSegment segment{phase, axis, first, last};
        for (std::size_t index = first; index < last; ++index) {
            values[index] = Derivative(segment, index, format);
        }
        first = last;
    }
    return values;
}

}  // 匿名命名空间

bool TraceSequenceFormatter::RequiresSequence(DisplayFormat format) noexcept
{
    return format == DisplayFormat::UNWRAPPED_PHASE || format == DisplayFormat::DELAY ||
           format == DisplayFormat::DELAY_DERIVATION;
}

std::vector<FormattedPoint> TraceSequenceFormatter::Calculate(
    const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz, DisplayFormat format)
{
    if (!RequiresSequence(format)) {
        std::vector<FormattedPoint> values;
        values.reserve(trace.points.size());
        for (const auto &point : trace.points) {
            values.push_back(TraceProjection::Format(point, format));
        }
        return values;
    }
    if (trace.points.size() != frequencyHz.size() || !MarkerResolver::ValidAxis(frequencyHz)) {
        return {};
    }
    const auto phase = Unwrap(trace);
    return format == DisplayFormat::UNWRAPPED_PHASE ? phase : Differentiate(phase, frequencyHz, format);
}

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay
