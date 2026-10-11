/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_measurement/measurement_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "vna_core/frame_layout.h"

namespace VnaMeasurement {
namespace {

// 用尾数和二进制指数保存中间积，避免有限输入在平方或相乘时溢出。
struct ScaledValue final {
    double mantissa = 0.0;
    int exponent = 0;
};

ScaledValue Multiply(double left, double right) noexcept
{
    if (left == 0.0 || right == 0.0) {
        return {};
    }
    int leftExponent = 0;
    int rightExponent = 0;
    const double leftMantissa = std::frexp(left, &leftExponent);
    const double rightMantissa = std::frexp(right, &rightExponent);
    return {leftMantissa * rightMantissa, leftExponent + rightExponent};
}

ScaledValue Add(const ScaledValue &left, const ScaledValue &right) noexcept
{
    if (left.mantissa == 0.0) {
        return right;
    }
    if (right.mantissa == 0.0) {
        return left;
    }
    const int exponent = std::max(left.exponent, right.exponent);
    return {std::ldexp(left.mantissa, left.exponent - exponent) + std::ldexp(right.mantissa, right.exponent - exponent),
        exponent};
}

double Divide(const ScaledValue &numerator, const ScaledValue &denominator) noexcept
{
    // 分母在参考波检查后保证为正；只有最终结果才恢复到 double 的指数范围。
    return std::ldexp(numerator.mantissa / denominator.mantissa, numerator.exponent - denominator.exponent);
}

std::string ValidateFrame(const VnaCore::MeasurementFrame &frame)
{
    const auto points = frame.plan.frequencyRaw.size();
    if (points == 0U || (!frame.plan.frequencyHz.empty() && frame.plan.frequencyHz.size() != points) ||
        points > std::numeric_limits<std::size_t>::max() / VnaCore::IQ_VALUES_PER_POINT) {
        return "frame frequency axis is incomplete";
    }
    for (const double frequency : frame.plan.frequencyHz) {
        if (!std::isfinite(frequency)) {
            return "frame frequency axis contains a non-finite value";
        }
    }
    if ((frame.plan.receiverMask & ~VnaCore::ALL_RECEIVERS_MASK) != 0U) {
        return "frame receiver mask contains an unsupported port";
    }
    if (frame.plan.sourcePorts.empty() || frame.plan.sourcePorts.size() > VnaCore::PHYSICAL_PORT_COUNT ||
        frame.ports.size() != frame.plan.sourcePorts.size()) {
        return "frame source ports are incomplete";
    }
    std::array<bool, VnaCore::PHYSICAL_PORT_COUNT + 1U> expected{};
    for (const auto sourcePort : frame.plan.sourcePorts) {
        if (sourcePort == 0U || sourcePort > VnaCore::PHYSICAL_PORT_COUNT || expected[sourcePort]) {
            return "frame source plan contains an invalid or duplicate port";
        }
        expected[sourcePort] = true;
    }
    for (const auto &port : frame.ports) {
        if (port.sourcePort == 0U || port.sourcePort > VnaCore::PHYSICAL_PORT_COUNT || !expected[port.sourcePort]) {
            return "frame source ports do not match the source plan";
        }
        if (port.iq.size() != points * VnaCore::IQ_VALUES_PER_POINT) {
            return "frame source IQ length does not match the frequency axis";
        }
        expected[port.sourcePort] = false;
    }
    return {};
}

const VnaCore::PortFrame *FindSource(const VnaCore::MeasurementFrame &frame, unsigned sourcePort) noexcept
{
    for (const auto &port : frame.ports) {
        if (port.sourcePort == sourcePort) {
            return &port;
        }
    }
    return nullptr;
}

ComplexPoint ReadPoint(
    const VnaCore::PortFrame &port, std::size_t pointIndex, unsigned receiverPort, char receiverKind) noexcept
{
    const auto offset = pointIndex * VnaCore::IQ_VALUES_PER_POINT +
                        (receiverPort - 1U) * VnaCore::IQ_VALUES_PER_RECEIVER +
                        (receiverKind == 'b' ? VnaCore::COMPLEX_COMPONENT_COUNT : 0U);
    ComplexPoint point;
    point.real = port.iq[offset];
    point.imag = port.iq[offset + 1U];
    point.valid = std::isfinite(point.real) && std::isfinite(point.imag);
    point.error = point.valid ? PointError::NONE : PointError::INVALID_SAMPLE;
    return point;
}

ComplexPoint Normalize(const ComplexPoint &numerator, const ComplexPoint &reference) noexcept
{
    ComplexPoint point;
    if (!numerator.valid || !reference.valid) {
        point.error = PointError::INVALID_SAMPLE;
        return point;
    }
    if (std::hypot(reference.real, reference.imag) <= MIN_REFERENCE_MAGNITUDE) {
        point.error = PointError::REFERENCE_TOO_SMALL;
        return point;
    }
    const auto denominator = Add(Multiply(reference.real, reference.real), Multiply(reference.imag, reference.imag));
    const auto real = Add(Multiply(numerator.real, reference.real), Multiply(numerator.imag, reference.imag));
    const auto imag = Add(Multiply(numerator.imag, reference.real), Multiply(-numerator.real, reference.imag));
    point.real = Divide(real, denominator);
    point.imag = Divide(imag, denominator);
    point.valid = std::isfinite(point.real) && std::isfinite(point.imag);
    point.error = point.valid ? PointError::NONE : PointError::NONFINITE_RESULT;
    return point;
}

std::string ValidateMeasurement(const VnaCore::MeasurementFrame &frame, const Measurement &measurement)
{
    if (!measurement.IsSupported()) {
        return "measurement parameter is not supported";
    }
    const auto receiverBit = 1U << (measurement.receiverPort - 1U);
    if ((frame.plan.receiverMask & receiverBit) == 0U) {
        return "measurement receiver is not enabled in this frame";
    }
    const auto referenceBit = 1U << (measurement.sourcePort - 1U);
    if (measurement.kind == MeasurementKind::S_PARAMETER && (frame.plan.receiverMask & referenceBit) == 0U) {
        return "measurement reference receiver is not enabled in this frame";
    }
    if (FindSource(frame, measurement.sourcePort) == nullptr) {
        return "measurement source is missing from this frame";
    }
    return {};
}

}  // 匿名命名空间

TraceResult MeasurementProcessor::Calculate(const VnaCore::MeasurementFrame &frame, const Measurement &measurement)
{
    TraceResult result;
    result.error = ValidateFrame(frame);
    if (!result.error.empty()) {
        return result;
    }
    result.error = ValidateMeasurement(frame, measurement);
    if (!result.error.empty()) {
        return result;
    }
    const auto *port = FindSource(frame, measurement.sourcePort);
    result.points.reserve(frame.plan.frequencyRaw.size());
    for (std::size_t index = 0U; index < frame.plan.frequencyRaw.size(); ++index) {
        const auto numerator = ReadPoint(*port, index, measurement.receiverPort, measurement.receiverKind);
        if (measurement.kind == MeasurementKind::S_PARAMETER) {
            // 分子与源端入射参考均来自同一激励源的完整端口帧。
            result.points.push_back(Normalize(numerator, ReadPoint(*port, index, measurement.sourcePort, 'a')));
        } else {
            result.points.push_back(numerator);
        }
    }
    result.available = true;
    return result;
}

}  // 命名空间 VnaMeasurement
