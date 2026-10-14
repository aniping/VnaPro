/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#include "vna_core/frame_layout.h"
#include "vna_measurement/measurement_processor.h"

namespace {

using VnaCalibration::CalibrationData;
using VnaCalibration::CalibrationMethod;
using VnaCalibration::Complex;
using VnaMeasurement::Measurement;
using VnaMeasurement::MeasurementKind;
using VnaMeasurement::MeasurementProcessor;
using VnaMeasurement::PointError;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(const VnaMeasurement::ComplexPoint &actual, const Complex &expected)
{
    Require(actual.valid && actual.error == PointError::NONE &&
                std::abs(Complex(actual.real, actual.imag) - expected) <= 1e-11,
        "校正迹线与独立期望值不一致");
}

Measurement Parameter(const std::string &name)
{
    Measurement measurement;
    Require(measurement.SetParameter(name), "测试参数无法解析");
    return measurement;
}

void SetWave(VnaCore::PortFrame &port, std::size_t point, unsigned receiver, char kind, const Complex &value)
{
    const auto offset = point * VnaCore::IQ_VALUES_PER_POINT + (receiver - 1U) * VnaCore::IQ_VALUES_PER_RECEIVER +
                        (kind == 'b' ? VnaCore::COMPLEX_COMPONENT_COUNT : 0U);
    port.iq[offset] = value.real();
    port.iq[offset + 1U] = value.imag();
}

VnaCore::MeasurementFrame Frame()
{
    VnaCore::MeasurementFrame frame;
    frame.plan.frequencyHz = {1e9, 2e9, 3e9};
    frame.plan.frequencyRaw = {1000000000U, 2000000000U, 3000000000U};
    frame.plan.receiverMask = VnaCore::ALL_RECEIVERS_MASK;
    frame.plan.sourcePorts = {1U, 3U};
    for (unsigned source : {3U, 1U}) {
        VnaCore::PortFrame port;
        port.sourcePort = source;
        port.iq.resize(3U * VnaCore::IQ_VALUES_PER_POINT);
        const Complex reference = source == 1U ? Complex(2.0, -3.0) : Complex(3.0, 4.0);
        for (std::size_t point = 0U; point < 3U; ++point) {
            for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
                SetWave(port, point, receiver, 'a', {99.0, -88.0});
                SetWave(port, point, receiver, 'b', reference * Complex(0.3, -0.4));
            }
            SetWave(port, point, source, 'a', reference);
        }
        frame.ports.push_back(port);
    }
    return frame;
}

void SetRatio(VnaCore::MeasurementFrame &frame, unsigned receiver, unsigned source, const Complex &value)
{
    for (auto &port : frame.ports) {
        if (port.sourcePort == source) {
            const Complex reference = source == 1U ? Complex(2.0, -3.0) : Complex(3.0, 4.0);
            for (std::size_t point = 0U; point < 3U; ++point) {
                SetWave(port, point, receiver, 'b', reference * value);
            }
        }
    }
}

CalibrationData OnePort(const VnaCore::MeasurementFrame &frame)
{
    CalibrationData calibration;
    calibration.port1 = 3U;
    calibration.frequencyHz = frame.plan.frequencyHz;
    calibration.onePortTerms.assign(3U, {{0.02, 0.01}, {-0.1, 0.03}, {0.92, -0.08}});
    return calibration;
}

CalibrationData TwoPort(const VnaCore::MeasurementFrame &frame)
{
    CalibrationData calibration;
    calibration.method = CalibrationMethod::TWO_PORT;
    calibration.port1 = 1U;
    calibration.port2 = 3U;
    calibration.frequencyHz = frame.plan.frequencyHz;
    const VnaCalibration::TwoPortTerms terms = {{{0.02, 0.01}, {-0.1, 0.03}, {0.92, -0.08}},
        {{-0.015, 0.008}, {0.07, 0.02}, {0.95, 0.04}}, {{0.001, 0.0005}, {0.08, -0.02}, {0.8, 0.12}},
        {{-0.0008, 0.0003}, {-0.05, 0.025}, {0.85, -0.09}}};
    calibration.twoPortTerms.assign(3U, terms);
    return calibration;
}

void TestOnePortRawAndUncovered()
{
    auto frame = Frame();
    SetRatio(frame, 3U, 3U, {0.2776235288454596, -0.3626684156056705});
    const auto original = frame.ports;
    const auto calibration = OnePort(frame);
    const auto corrected = MeasurementProcessor::Calculate(frame, Parameter("S33"), &calibration);
    Require(corrected.available && corrected.calibrated, "单端口未应用校准");
    for (const auto &point : corrected.points) {
        Near(point, {0.3, -0.4});
    }
    const auto raw = MeasurementProcessor::Calculate(frame, Parameter("S33"));
    Require(raw.available && !raw.calibrated, "原始比值被错误标记已校准");
    Near(raw.points.front(), {0.2776235288454596, -0.3626684156056705});
    const auto uncovered = MeasurementProcessor::Calculate(frame, Parameter("S13"), &calibration);
    Require(uncovered.available && !uncovered.calibrated, "单端口校准错误覆盖传输");
    Near(uncovered.points.front(), {0.3, -0.4});
    const auto wave = MeasurementProcessor::Calculate(frame, Parameter("b1,1"), &calibration);
    Require(wave.available && !wave.calibrated, "原始接收波被校正");
    Near(wave.points.front(), Complex(2.0, -3.0) * Complex(0.3, -0.4));
    for (std::size_t index = 0U; index < original.size(); ++index) {
        Require(original[index].iq == frame.ports[index].iq, "校正修改了原始 IQ 帧");
    }
}

void TestPairUsesCompleteFrameAndPhysicalPorts()
{
    auto frame = Frame();
    SetRatio(frame, 1U, 1U, {0.22529775586066184, 0.06482568097554335});
    SetRatio(frame, 3U, 1U, {0.490698096351777, -0.07849691164611074});
    SetRatio(frame, 3U, 3U, {-0.14590664905298956, 0.15745613056288396});
    SetRatio(frame, 1U, 3U, {0.4475803836423335, -0.12691351725210306});
    const auto calibration = TwoPort(frame);
    const std::array<std::string, 4U> parameters = {"S11", "S31", "S33", "S13"};
    const std::array<Complex, 4U> expected = {{{0.2, 0.1}, {0.6, -0.2}, {-0.12, 0.15}, {0.55, -0.1}}};
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        const auto result = MeasurementProcessor::Calculate(frame, Parameter(parameters[index]), &calibration);
        Require(result.available && result.calibrated && result.points.size() == 3U, "物理端口对校正不可用");
        for (const auto &point : result.points) {
            Near(point, expected[index]);
        }
    }
    const std::vector<Measurement> requested = {
        Parameter("S13"), Parameter("b1,3"), Parameter("S23"), Parameter("S31"), Parameter("S13")};
    auto batch = MeasurementProcessor::CalculateMany(frame, requested, &calibration);
    Require(batch.size() == requested.size(), "批处理没有保持输入长度");
    Near(batch[0U].points.front(), expected[3U]);
    Require(batch[0U].calibrated && !batch[1U].calibrated && !batch[2U].calibrated && batch[3U].calibrated,
        "批处理混淆 raw、未覆盖和校正数据");
    Near(batch[1U].points.front(), Complex(3.0, 4.0) * Complex(0.4475803836423335, -0.12691351725210306));
    Near(batch[2U].points.front(), {0.3, -0.4});
    Near(batch[3U].points.front(), expected[1U]);
    batch[0U].points.front().real = 99.0;
    Near(batch[4U].points.front(), expected[3U]);
    Require(MeasurementProcessor::CalculateMany(frame, {}, &calibration).empty(), "空测量目录仍生成结果");
    auto reversed = calibration;
    std::swap(reversed.port1, reversed.port2);
    for (auto &terms : reversed.twoPortTerms) {
        std::swap(terms.forwardReflection, terms.reverseReflection);
        std::swap(terms.forwardTransmission, terms.reverseTransmission);
    }
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        const auto result = MeasurementProcessor::Calculate(frame, Parameter(parameters[index]), &reversed);
        Require(result.available && result.calibrated, "反序物理端口对不可用");
        Near(result.points.front(), expected[index]);
    }
    // 请求反向迹线本身仍有效，但正向参考损坏会使该频点的联合校正无效。
    SetWave(frame.ports[1U], 1U, 1U, 'a', {0.0, 0.0});
    const auto invalid = MeasurementProcessor::Calculate(frame, Parameter("S13"), &calibration);
    Require(invalid.available && invalid.calibrated && invalid.points[0U].valid && !invalid.points[1U].valid &&
                invalid.points[1U].error == PointError::REFERENCE_TOO_SMALL && invalid.points[2U].valid,
        "联合校正未保留原始参考错误或使好点失效");
    batch = MeasurementProcessor::CalculateMany(frame, requested, &calibration);
    Require(!batch[0U].points[1U].valid && batch[0U].points[1U].error == PointError::REFERENCE_TOO_SMALL &&
                !batch[3U].points[1U].valid && !batch[4U].points[1U].valid && batch[1U].points[1U].valid,
        "批处理跨帧复用了旧联合结果，或使独立 raw 波量失效");
    frame.ports.erase(frame.ports.begin());
    frame.plan.sourcePorts = {1U};
    const auto missing = MeasurementProcessor::Calculate(frame, Parameter("S11"), &calibration);
    Require(!missing.available && !missing.error.empty(), "双向源缺失被当作有效单条校正");
}

void TestCalibrationShapeAxisAndPointErrors()
{
    auto frame = Frame();
    auto calibration = OnePort(frame);
    calibration.frequencyHz[1U] += 1.0;
    Require(
        !MeasurementProcessor::Calculate(frame, Parameter("S33"), &calibration).available, "同点数不同频轴被隐式复用");
    calibration = OnePort(frame);
    calibration.onePortTerms.pop_back();
    Require(!MeasurementProcessor::Calculate(frame, Parameter("S33"), &calibration).available, "错误系数形状未拒绝");
    calibration = OnePort(frame);
    calibration.onePortTerms[0U].reflectionTracking = 0.0;
    calibration.onePortTerms[1U].sourceMatch = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    auto result = MeasurementProcessor::Calculate(frame, Parameter("S33"), &calibration);
    Require(result.available && result.calibrated && result.points[0U].error == PointError::CALIBRATION_SINGULAR &&
                result.points[1U].error == PointError::INVALID_CALIBRATION && result.points[2U].valid,
        "系数异常没有逐点标记");
    SetWave(frame.ports[0U], 2U, 3U, 'b', {std::numeric_limits<double>::infinity(), 0.0});
    result = MeasurementProcessor::Calculate(frame, Parameter("S33"), &calibration);
    Require(result.points[2U].error == PointError::INVALID_SAMPLE, "校正掩盖无效原始采样");
    for (const unsigned invalid : {0U, 5U}) {
        calibration = OnePort(frame);
        calibration.port1 = invalid;
        result = MeasurementProcessor::Calculate(frame, Parameter("S13"), &calibration);
        Require(!result.available && !result.error.empty(), "非法校准端口被当作未覆盖 raw 数据");
    }
    calibration = OnePort(frame);
    calibration.method = static_cast<CalibrationMethod>(99);
    result = MeasurementProcessor::Calculate(frame, Parameter("S13"), &calibration);
    Require(!result.available && !result.error.empty(), "未知校准模型被静默忽略");
    const auto batch = MeasurementProcessor::CalculateMany(frame, {Parameter("S13"), Parameter("b1,1")}, &calibration);
    Require(
        !batch[0U].available && batch[1U].available && !batch[1U].calibrated, "批处理非法模型错误污染 raw 接收波合同");
}

}  // 匿名命名空间

int main()
{
    try {
        TestOnePortRawAndUncovered();
        TestPairUsesCompleteFrameAndPhysicalPorts();
        TestCalibrationShapeAxisAndPointErrors();
        std::cout << "校准测量接入：单端口、双向完整帧、物理端口、原始数据和异常合同通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
