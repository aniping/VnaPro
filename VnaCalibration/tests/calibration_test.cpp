/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_calibration/calibration.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "calibration_golden.h"

namespace {

using namespace VnaCalibration;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(const Complex &actual, const Complex &expected)
{
    Require(std::isfinite(actual.real()) && std::isfinite(actual.imag()) && std::abs(actual - expected) <= 1e-11,
        "校准数值与独立黄金向量不一致");
}

void RequireTerms(const OnePortTerms &actual, const OnePortTerms &expected)
{
    RequireNear(actual.directivity, expected.directivity);
    RequireNear(actual.sourceMatch, expected.sourceMatch);
    RequireNear(actual.reflectionTracking, expected.reflectionTracking);
}

void RequireNetwork(const TwoPortNetwork &actual, const TwoPortNetwork &expected)
{
    RequireNear(actual.s11, expected.s11);
    RequireNear(actual.s21, expected.s21);
    RequireNear(actual.s22, expected.s22);
    RequireNear(actual.s12, expected.s12);
}

void TestOriginalInstructions()
{
    for (const auto &sample : Golden::REFLECTION) {
        OnePortTerms output;
        Require(SolveThreeStandards(sample.measured, sample.standards, output) == CalibrationError::NONE,
            "非理想三标准被拒绝");
        RequireTerms(output, sample.expected);
    }
    for (const auto &sample : Golden::THRU) {
        TwoPortTerms output;
        Require(SolveKnownThru(sample.measured, sample.defined, sample.forward, sample.reverse, sample.forwardIsolation,
                    sample.reverseIsolation, output) == CalibrationError::NONE,
            "非理想非互易 THRU 被拒绝");
        RequireTerms(output.forwardReflection, sample.forward);
        RequireTerms(output.reverseReflection, sample.reverse);
        RequireNear(output.forwardTransmission.loadMatch, sample.expected[0U]);
        RequireNear(output.forwardTransmission.transmissionTracking, sample.expected[1U]);
        RequireNear(output.reverseTransmission.loadMatch, sample.expected[2U]);
        RequireNear(output.reverseTransmission.transmissionTracking, sample.expected[3U]);
        RequireNear(output.forwardTransmission.isolation, sample.forwardIsolation);
        RequireNear(output.reverseTransmission.isolation, sample.reverseIsolation);
    }
    for (const auto &sample : Golden::CORRECTION) {
        TwoPortNetwork output;
        Require(
            CorrectTwoPort(sample.measured, sample.terms, output) == CalibrationError::NONE, "良态十二项校正被拒绝");
        RequireNetwork(output, sample.expected);
    }
}

void TestIndependentDotNetGolden()
{
    const std::array<Complex, 3U> measured = {
        {{0.857724006936989, -0.03988025435626393}, {-0.9981280059193489, 0.1328264890862005}, {0.02, 0.01}}};
    const std::array<Complex, 3U> standards = {{1.0, -1.0, 0.0}};
    const OnePortTerms expected = {{0.02, 0.01}, {-0.10, 0.03}, {0.92, -0.08}};
    OnePortTerms solved;
    Require(SolveThreeStandards(measured, standards, solved) == CalibrationError::NONE, "理想 OSL 求解失败");
    RequireTerms(solved, expected);
    Complex output;
    Require(CorrectOnePort({0.2776235288454596, -0.3626684156056705}, solved, output) == CalibrationError::NONE,
        "DUT 校正失败");
    RequireNear(output, {0.3, -0.4});
}

void TestIdentityAndCoverage()
{
    const OnePortTerms ideal = {0.0, 0.0, 1.0};
    const TwoPortNetwork thru = {0.0, 1.0, 0.0, 1.0};
    TwoPortTerms solved;
    Require(SolveKnownThru(thru, thru, ideal, ideal, 0.0, 0.0, solved) == CalibrationError::NONE, "理想 THRU 求解失败");
    auto output = Golden::CORRECTION.front().measured;
    Require(CorrectTwoPort(output, solved, output) == CalibrationError::NONE, "恒等校正失败");
    RequireNetwork(output, Golden::CORRECTION.front().measured);
    CalibrationData data;
    data.port1 = 3U;
    Require(data.Covers(3U, 3U) && !data.Covers(1U, 3U), "单端口覆盖错误");
    data.method = CalibrationMethod::TWO_PORT;
    data.port2 = 1U;
    Require(data.Covers(1U, 3U) && data.Covers(3U, 1U) && data.Covers(1U, 1U) && !data.Covers(2U, 1U),
        "物理端口对覆盖错误");
}

void TestTrackingBoundaryAndLargeFiniteIdentity()
{
    const double boundary = 1e-12;
    const double above = std::nextafter(boundary, std::numeric_limits<double>::infinity());
    Complex output(9.0, 8.0);
    Require(CorrectOnePort(boundary * 0.25, {0.0, 0.0, boundary}, output) == CalibrationError::SINGULAR,
        "跟踪项门槛边界被接受");
    RequireNear(output, {9.0, 8.0});
    Require(
        CorrectOnePort(above * 0.25, {0.0, 0.0, above}, output) == CalibrationError::NONE, "跟踪项门槛上方被错误拒绝");
    RequireNear(output, 0.25);
    const OnePortTerms one = {0.0, 0.0, 1.0};
    const Complex large(1e200, -1e200);
    Require(CorrectOnePort(large, one, output) == CalibrationError::NONE && output == large,
        "有限大幅值的恒等校正被中间平方溢出破坏");
    const TwoPortTerms ideal = {one, one, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}};
    const TwoPortNetwork network = {large, large, large, large};
    TwoPortNetwork corrected;
    Require(CorrectTwoPort(network, ideal, corrected) == CalibrationError::NONE && corrected.s11 == large &&
                corrected.s21 == large && corrected.s22 == large && corrected.s12 == large,
        "有限大幅值的十二项恒等校正失败");
}

void TestInvalidInputsDoNotCommitOutput()
{
    const OnePortTerms sentinel = {{9.0, 8.0}, {7.0, 6.0}, {5.0, 4.0}};
    OnePortTerms terms = sentinel;
    const std::array<Complex, 3U> duplicate = {{1.0, 1.0, 1.0}};
    Require(SolveThreeStandards(duplicate, duplicate, terms) == CalibrationError::SINGULAR, "重复标准未拒绝");
    RequireTerms(terms, sentinel);
    auto bad = duplicate;
    bad[0U] = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    Require(SolveThreeStandards(bad, duplicate, terms) == CalibrationError::INVALID_INPUT, "非有限标准未拒绝");
    RequireTerms(terms, sentinel);
    Complex output(9.0, 8.0);
    for (const double measured : {-1.0, -1.0 + 1e-14}) {
        Require(CorrectOnePort(measured, {0.0, 1.0, 1.0}, output) == CalibrationError::SINGULAR, "校正相消分母未拒绝");
        RequireNear(output, {9.0, 8.0});
    }
    Require(CorrectOnePort(0.0, {}, output) == CalibrationError::SINGULAR, "零跟踪未拒绝");
    auto twoTerms = Golden::CORRECTION.front().terms;
    const TwoPortNetwork sentinelNetwork = {9.0, 8.0, 7.0, 6.0};
    auto networkOutput = sentinelNetwork;
    twoTerms.reverseTransmission.transmissionTracking = 0.0;
    Require(CorrectTwoPort(Golden::CORRECTION.front().measured, twoTerms, networkOutput) == CalibrationError::SINGULAR,
        "反向零跟踪未拒绝");
    RequireNetwork(networkOutput, sentinelNetwork);
    TwoPortTerms thruOutput = twoTerms;
    Require(
        SolveKnownThru({}, {}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, thruOutput) == CalibrationError::SINGULAR,
        "零标准传输未拒绝");
    RequireTerms(thruOutput.forwardReflection, twoTerms.forwardReflection);
    RequireNear(thruOutput.forwardTransmission.loadMatch, twoTerms.forwardTransmission.loadMatch);
    const TwoPortTerms singular = {{0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, {0.0, 1.0, 1.0}};
    Require(CorrectTwoPort({0.0, 1.0, 0.0, 1.0}, singular, networkOutput) == CalibrationError::SINGULAR,
        "十二项共同分母相消未拒绝");
    RequireNetwork(networkOutput, sentinelNetwork);
}

}  // 匿名命名空间

int main()
{
    try {
        TestOriginalInstructions();
        TestIndependentDotNetGolden();
        TestIdentityAndCoverage();
        TestTrackingBoundaryAndLargeFiniteIdentity();
        TestInvalidInputsDoNotCommitOutput();
        std::cout << "20 组原指令黄金向量及独立 OSL、恒等、异常合同通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
