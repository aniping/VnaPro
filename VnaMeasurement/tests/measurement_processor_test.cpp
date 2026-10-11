/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_measurement/measurement_processor.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "vna_core/frame_layout.h"

namespace {

using VnaMeasurement::Measurement;
using VnaMeasurement::MeasurementKind;
using VnaMeasurement::MeasurementProcessor;
using VnaMeasurement::PointError;

constexpr double PI = 3.14159265358979323846;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(double actual, double expected)
{
    const double tolerance = std::abs(expected) * 1e-12 + std::numeric_limits<double>::denorm_min() * 8.0;
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, "复数分量与独立期望值不一致");
}

void RequirePoint(const VnaMeasurement::ComplexPoint &point, const std::complex<double> &expected)
{
    Require(point.valid && point.error == PointError::NONE, "有效采样点被错误拒绝");
    RequireNear(point.real, expected.real());
    RequireNear(point.imag, expected.imag());
}

VnaCore::MeasurementFrame Frame(const std::vector<std::uint32_t> &sourcePorts, std::size_t pointCount = 3U)
{
    VnaCore::MeasurementFrame frame;
    frame.plan.sourcePorts = sourcePorts;
    frame.plan.receiverMask = VnaCore::ALL_RECEIVERS_MASK;
    for (std::size_t index = 0U; index < pointCount; ++index) {
        frame.plan.frequencyHz.push_back(static_cast<double>(index + 1U) * 1e6);
        frame.plan.frequencyRaw.push_back(index + 1U);
    }
    for (const auto sourcePort : sourcePorts) {
        VnaCore::PortFrame port;
        port.sourcePort = sourcePort;
        port.iq.resize(pointCount * VnaCore::IQ_VALUES_PER_POINT, 0.0);
        frame.ports.push_back(port);
    }
    return frame;
}

void SetIq(VnaCore::PortFrame &port, std::size_t pointIndex, unsigned receiverPort, char receiverKind,
    const std::complex<double> &value)
{
    const auto offset = pointIndex * VnaCore::IQ_VALUES_PER_POINT +
                        (receiverPort - 1U) * VnaCore::IQ_VALUES_PER_RECEIVER +
                        (receiverKind == 'b' ? VnaCore::COMPLEX_COMPONENT_COUNT : 0U);
    port.iq[offset] = value.real();
    port.iq[offset + 1U] = value.imag();
}

Measurement SParameter(unsigned receiverPort)
{
    Measurement measurement;
    measurement.kind = MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiverPort;
    return measurement;
}

void TestSupportedParameters()
{
    Measurement raw;
    for (const char receiverKind : {'a', 'b'}) {
        for (unsigned sourcePort = 1U; sourcePort <= 4U; ++sourcePort) {
            for (unsigned receiverPort = 1U; receiverPort <= 4U; ++receiverPort) {
                raw.receiverKind = receiverKind;
                raw.receiverPort = receiverPort;
                raw.sourcePort = sourcePort;
                Require(raw.IsSupported(), "原始接收波参数被拒绝");
                Require(raw.Parameter() == std::string(1U, receiverKind) + std::to_string(receiverPort) + "," +
                                               std::to_string(sourcePort),
                    "原始接收波参数名称错误");
            }
        }
    }
    Require(SParameter(1U).Parameter() == "S11" && SParameter(2U).Parameter() == "S21", "S 参数名称错误");
    auto unsupported = SParameter(1U);
    unsupported.sourcePort = 2U;
    Require(!unsupported.IsSupported(), "当前范围外的 S12 被接受");
    unsupported = SParameter(3U);
    Require(!unsupported.IsSupported(), "当前范围外的 S31 被接受");
    unsupported = SParameter(1U);
    unsupported.receiverKind = 'a';
    Require(!unsupported.IsSupported(), "使用入射波作为 S 参数分子被接受");
    raw.receiverPort = 0U;
    Require(!raw.IsSupported(), "零号接收端口被接受");
    raw.kind = static_cast<MeasurementKind>(99);
    Require(!raw.IsSupported(), "非法测量类型被接受");
}

void TestKnownReflectionAndTransmission()
{
    auto frame = Frame({1U});
    const std::complex<double> reference(2.0, -3.0);
    const std::complex<double> reflection(0.3, 0.4);
    const std::complex<double> through(1.0, 0.0);
    const std::complex<double> attenuation(0.1, -0.2);
    for (std::size_t index = 0U; index < 3U; ++index) {
        SetIq(frame.ports[0U], index, 1U, 'a', reference);
        SetIq(frame.ports[0U], index, 1U, 'b', reference * reflection);
        SetIq(frame.ports[0U], index, 2U, 'b', reference * (index == 0U ? through : attenuation));
    }
    const auto s11 = MeasurementProcessor::Calculate(frame, SParameter(1U));
    const auto s21 = MeasurementProcessor::Calculate(frame, SParameter(2U));
    Require(s11.available && s21.available && s11.points.size() == 3U, "已知网络结果不可用");
    for (const auto &point : s11.points) {
        RequirePoint(point, reflection);
    }
    RequirePoint(s21.points[0U], through);
    RequirePoint(s21.points[1U], attenuation);
    RequirePoint(s21.points[2U], attenuation);
    const auto original = frame.ports[0U].iq;
    Measurement raw;
    const auto rawResult = MeasurementProcessor::Calculate(frame, raw);
    Require(rawResult.available, "原始测量不可用");
    RequirePoint(rawResult.points[0U], reference);
    Require(frame.ports[0U].iq == original, "计算修改了不可变输入帧");
}

void TestDelayPhase()
{
    auto frame = Frame({1U});
    frame.plan.frequencyHz = {1e6, 25e6, 100e6};
    const double delay = 7e-9;
    const double amplitude = 0.1;
    const std::complex<double> reference(-0.7, 0.3);
    std::vector<std::complex<double>> expected;
    for (std::size_t index = 0U; index < frame.plan.frequencyHz.size(); ++index) {
        const double phase = -2.0 * PI * frame.plan.frequencyHz[index] * delay;
        expected.emplace_back(amplitude * std::cos(phase), amplitude * std::sin(phase));
        SetIq(frame.ports[0U], index, 1U, 'a', reference);
        SetIq(frame.ports[0U], index, 2U, 'b', reference * expected.back());
    }
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(2U));
    Require(result.available, "延迟网络结果不可用");
    for (std::size_t index = 0U; index < result.points.size(); ++index) {
        RequirePoint(result.points[index], expected[index]);
        RequireNear(20.0 * std::log10(std::hypot(result.points[index].real, result.points[index].imag)), -20.0);
        RequireNear(std::atan2(result.points[index].imag, result.points[index].real),
            std::atan2(expected[index].imag(), expected[index].real()));
    }
}

void TestSameSourceOnly()
{
    auto frame = Frame({2U, 1U}, 1U);
    SetIq(frame.ports[0U], 0U, 1U, 'a', {99.0, -88.0});
    SetIq(frame.ports[0U], 0U, 2U, 'b', {77.0, 66.0});
    SetIq(frame.ports[1U], 0U, 1U, 'a', {2.0, 0.0});
    SetIq(frame.ports[1U], 0U, 2U, 'b', {0.5, -1.0});
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(2U));
    Require(result.available, "完整多源帧不可用");
    RequirePoint(result.points[0U], {0.25, -0.5});
    Measurement raw;
    raw.receiverKind = 'b';
    raw.receiverPort = 2U;
    raw.sourcePort = 2U;
    RequirePoint(MeasurementProcessor::Calculate(frame, raw).points[0U], {77.0, 66.0});
}

void TestOptionalPhysicalAxis()
{
    auto frame = Frame({1U}, 1U);
    frame.plan.frequencyHz.clear();
    SetIq(frame.ports[0U], 0U, 1U, 'a', {2.0, 0.0});
    SetIq(frame.ports[0U], 0U, 1U, 'b', {0.5, -0.5});
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(1U));
    Require(result.available && result.points.size() == 1U, "核心允许的无物理单位频率轴帧被错误拒绝");
    RequirePoint(result.points[0U], {0.25, -0.25});
}

void TestSmallReferenceBoundary()
{
    auto frame = Frame({1U}, 4U);
    const double minimum = VnaMeasurement::MIN_REFERENCE_MAGNITUDE;
    SetIq(frame.ports[0U], 0U, 1U, 'a', {0.0, 0.0});
    SetIq(frame.ports[0U], 1U, 1U, 'a', {minimum * 0.5, 0.0});
    SetIq(frame.ports[0U], 2U, 1U, 'a', {minimum, 0.0});
    SetIq(frame.ports[0U], 3U, 1U, 'a', {std::nextafter(minimum, 1.0), 0.0});
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(1U));
    Require(result.available && result.points.size() == 4U, "低参考波应按点拒绝，不能丢弃整条迹线");
    for (std::size_t index = 0U; index < 3U; ++index) {
        Require(!result.points[index].valid && result.points[index].error == PointError::REFERENCE_TOO_SMALL,
            "零、低于下限或等于下限的参考波被接受");
    }
    RequirePoint(result.points[3U], {0.0, 0.0});
}

void TestNonfiniteSamplesAndResults()
{
    auto frame = Frame({1U}, 4U);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < 4U; ++index) {
        SetIq(frame.ports[0U], index, 1U, 'a', {1.0, 0.0});
    }
    SetIq(frame.ports[0U], 0U, 1U, 'b', {nan, 0.0});
    SetIq(frame.ports[0U], 1U, 1U, 'b', {0.0, -infinity});
    SetIq(frame.ports[0U], 2U, 1U, 'a', {infinity, 0.0});
    SetIq(frame.ports[0U], 3U, 1U, 'a', {2e-12, 0.0});
    SetIq(frame.ports[0U], 3U, 1U, 'b', {std::numeric_limits<double>::max(), 0.0});
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(1U));
    Require(result.available, "采样点无效不能伪装成帧结构错误");
    for (std::size_t index = 0U; index < 3U; ++index) {
        Require(!result.points[index].valid && result.points[index].error == PointError::INVALID_SAMPLE,
            "非有限分子或参考波被接受");
    }
    Require(!result.points[3U].valid && result.points[3U].error == PointError::NONFINITE_RESULT,
        "不可表示的归一化结果被接受");
    Measurement raw;
    raw.receiverKind = 'b';
    const auto rawResult = MeasurementProcessor::Calculate(frame, raw);
    Require(rawResult.available && !rawResult.points[0U].valid && !rawResult.points[1U].valid &&
                rawResult.points[0U].error == PointError::INVALID_SAMPLE,
        "原始波量未保留逐点非有限标记");
}

void TestExtremeFiniteValues()
{
    auto frame = Frame({1U}, 4U);
    const double maximum = std::numeric_limits<double>::max();
    SetIq(frame.ports[0U], 0U, 1U, 'a', {maximum, maximum});
    SetIq(frame.ports[0U], 0U, 1U, 'b', {maximum, maximum});
    SetIq(frame.ports[0U], 1U, 1U, 'a', {1.0, 1.0});
    SetIq(frame.ports[0U], 1U, 1U, 'b', {maximum, maximum});
    SetIq(frame.ports[0U], 2U, 1U, 'a', {1.0, 0.0});
    SetIq(frame.ports[0U], 2U, 1U, 'b', {maximum, 1e-300});
    SetIq(frame.ports[0U], 3U, 1U, 'a', {2.0, 0.0});
    SetIq(
        frame.ports[0U], 3U, 1U, 'b', {std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min()});
    const auto result = MeasurementProcessor::Calculate(frame, SParameter(1U));
    Require(result.available, "极端有限帧不可用");
    RequirePoint(result.points[0U], {1.0, 0.0});
    RequirePoint(result.points[1U], {maximum, 0.0});
    RequirePoint(result.points[2U], {maximum, 1e-300});
    RequirePoint(result.points[3U], {std::numeric_limits<double>::min() * 0.5, 0.0});
}

void RequireUnavailable(const VnaCore::MeasurementFrame &frame, const Measurement &measurement)
{
    const auto result = MeasurementProcessor::Calculate(frame, measurement);
    Require(!result.available && !result.error.empty() && result.points.empty(), "畸形帧或非法测量未被结构校验拒绝");
}

void TestMissingAndDisabledReceivers()
{
    RequireUnavailable(Frame({2U}), SParameter(1U));
    auto frame = Frame({1U});
    frame.plan.receiverMask = 2U;
    RequireUnavailable(frame, SParameter(2U));
    frame.plan.receiverMask = 1U;
    RequireUnavailable(frame, SParameter(2U));
    frame.plan.receiverMask = 0U;
    RequireUnavailable(frame, Measurement{});
    frame.plan.receiverMask = VnaCore::ALL_RECEIVERS_MASK;
    auto invalid = SParameter(1U);
    invalid.receiverPort = 0U;
    RequireUnavailable(frame, invalid);
}

void TestMalformedFrameLayouts()
{
    RequireUnavailable(Frame({1U}, 0U), SParameter(1U));
    RequireUnavailable(Frame({}), SParameter(1U));
    RequireUnavailable(Frame({1U, 1U}), SParameter(1U));
    RequireUnavailable(Frame({0U}), SParameter(1U));
    RequireUnavailable(Frame({5U}), SParameter(1U));
    auto frame = Frame({1U, 2U});
    frame.ports.pop_back();
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U, 2U});
    frame.ports[1U].sourcePort = 1U;
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U, 2U});
    frame.ports[1U].sourcePort = 3U;
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U, 2U});
    frame.ports[1U].iq.pop_back();
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U});
    frame.ports[0U].iq.push_back(0.0);
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U});
    frame.plan.frequencyRaw.pop_back();
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U});
    frame.plan.frequencyHz[0U] = std::numeric_limits<double>::quiet_NaN();
    RequireUnavailable(frame, SParameter(1U));
    frame = Frame({1U});
    frame.plan.receiverMask = 0x1FU;
    RequireUnavailable(frame, SParameter(1U));
}

}  // 匿名命名空间

int main()
{
    try {
        TestSupportedParameters();
        TestKnownReflectionAndTransmission();
        TestDelayPhase();
        TestSameSourceOnly();
        TestOptionalPhysicalAxis();
        TestSmallReferenceBoundary();
        TestNonfiniteSamplesAndResults();
        TestExtremeFiniteValues();
        TestMissingAndDisabledReceivers();
        TestMalformedFrameLayouts();
        std::cout << "测量计算：10 组场景通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
