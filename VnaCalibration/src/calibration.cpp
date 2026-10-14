/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_calibration/calibration.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VnaCalibration {
namespace {

// 生产合同拒绝病态点，不复刻目标程序的 1e-30 替代或 FLT_MAX 输出。
constexpr double DENOMINATOR_RELATIVE_LIMIT = 1e-12;

bool IsFinite(const Complex &value) noexcept
{
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

bool IsFinite(const OnePortTerms &terms) noexcept
{
    return IsFinite(terms.directivity) && IsFinite(terms.sourceMatch) && IsFinite(terms.reflectionTracking);
}

bool IsFinite(const TransmissionTerms &terms) noexcept
{
    return IsFinite(terms.isolation) && IsFinite(terms.loadMatch) && IsFinite(terms.transmissionTracking);
}

bool IsFinite(const TwoPortNetwork &values) noexcept
{
    return IsFinite(values.s11) && IsFinite(values.s21) && IsFinite(values.s22) && IsFinite(values.s12);
}

CalibrationError Divide(const Complex &numerator, const Complex &denominator, double scale, Complex &output) noexcept
{
    if (!IsFinite(numerator) || !IsFinite(denominator) || !std::isfinite(scale)) {
        return CalibrationError::NONFINITE_RESULT;
    }
    const double magnitude = std::abs(denominator);
    if (magnitude <= DENOMINATOR_RELATIVE_LIMIT * std::max(scale, std::numeric_limits<double>::min())) {
        return CalibrationError::SINGULAR;
    }
    const auto quotient = numerator / denominator;
    if (!IsFinite(quotient)) {
        return CalibrationError::NONFINITE_RESULT;
    }
    output = quotient;
    return CalibrationError::NONE;
}

CalibrationError CheckTracking(const Complex &tracking) noexcept
{
    Complex ignored;
    return Divide(Complex(1.0, 0.0), tracking, 1.0, ignored);
}

CalibrationError SolveDirection(const Complex &measuredReflection, const Complex &measuredTransmission,
    const Complex &standardReflection, const Complex &standardOtherReflection, const Complex &standardTransmission,
    const Complex &transmissionProduct, const Complex &determinant, const OnePortTerms &reflection,
    const Complex &isolation, TransmissionTerms &output) noexcept
{
    // 0x180010BE0 的直接负载匹配表达式，保留非理想和非互易标准响应。
    const auto delta = measuredReflection - reflection.directivity;
    const auto numerator = delta * (1.0 - standardReflection * reflection.sourceMatch) -
                           reflection.reflectionTracking * standardReflection;
    const auto first = delta * (standardOtherReflection - determinant * reflection.sourceMatch);
    const auto second = reflection.reflectionTracking * determinant;
    TransmissionTerms candidate;
    auto error = Divide(numerator, first - second, std::abs(first) + std::abs(second), candidate.loadMatch);
    if (error != CalibrationError::NONE) {
        return error;
    }
    const auto response =
        (1.0 - standardReflection * reflection.sourceMatch) * (1.0 - standardOtherReflection * candidate.loadMatch) -
        transmissionProduct * reflection.sourceMatch * candidate.loadMatch;
    error = Divide(
        (measuredTransmission - isolation) * response, standardTransmission, 1.0, candidate.transmissionTracking);
    if (error != CalibrationError::NONE) {
        return error;
    }
    error = CheckTracking(candidate.transmissionTracking);
    if (error != CalibrationError::NONE) {
        return error;
    }
    candidate.isolation = isolation;
    output = candidate;
    return CalibrationError::NONE;
}

}  // 匿名命名空间

bool CalibrationData::Covers(unsigned receiverPort, unsigned sourcePort) const noexcept
{
    if (method == CalibrationMethod::ONE_PORT) {
        return receiverPort == port1 && sourcePort == port1;
    }
    return method == CalibrationMethod::TWO_PORT && (receiverPort == port1 || receiverPort == port2) &&
           (sourcePort == port1 || sourcePort == port2);
}

CalibrationError SolveThreeStandards(
    const std::array<Complex, 3U> &measured, const std::array<Complex, 3U> &standards, OnePortTerms &output) noexcept
{
    for (std::size_t index = 0U; index < measured.size(); ++index) {
        if (!IsFinite(measured[index]) || !IsFinite(standards[index])) {
            return CalibrationError::INVALID_INPUT;
        }
    }
    const auto &g0 = standards[0U];
    const auto &g1 = standards[1U];
    const auto &g2 = standards[2U];
    const auto &m0 = measured[0U];
    const auto &m1 = measured[1U];
    const auto &m2 = measured[2U];
    // 0x140035DC0 的一般三标准闭式，结果顺序为 [D,S,R]。
    const auto t0 = g1 * g0 * (m1 - m0);
    const auto t1 = g1 * g2 * (m2 - m1);
    const auto t2 = g2 * g0 * (m0 - m2);
    const auto denominator = t0 + t1 + t2;
    const double scale = std::abs(t0) + std::abs(t1) + std::abs(t2);
    OnePortTerms candidate;
    auto error = Divide(t0 * m2 + t1 * m0 + t2 * m1, denominator, scale, candidate.directivity);
    if (error != CalibrationError::NONE) {
        return error;
    }
    const auto u0 = g2 * (m1 - m0);
    const auto u1 = g0 * (m2 - m1);
    const auto u2 = g1 * (m0 - m2);
    error = Divide(-(u0 + u1 + u2), denominator, scale, candidate.sourceMatch);
    if (error != CalibrationError::NONE) {
        return error;
    }
    Complex affine;
    error = Divide(u0 * m2 + u1 * m0 + u2 * m1, denominator, scale, affine);
    if (error != CalibrationError::NONE) {
        return error;
    }
    candidate.reflectionTracking = affine + candidate.directivity * candidate.sourceMatch;
    error = CheckTracking(candidate.reflectionTracking);
    if (error != CalibrationError::NONE) {
        return error;
    }
    output = candidate;
    return CalibrationError::NONE;
}

CalibrationError SolveKnownThru(const TwoPortNetwork &measured, const TwoPortNetwork &defined,
    const OnePortTerms &forward, const OnePortTerms &reverse, const Complex &forwardIsolation,
    const Complex &reverseIsolation, TwoPortTerms &output) noexcept
{
    if (!IsFinite(measured) || !IsFinite(defined) || !IsFinite(forward) || !IsFinite(reverse) ||
        !IsFinite(forwardIsolation) || !IsFinite(reverseIsolation)) {
        return CalibrationError::INVALID_INPUT;
    }
    for (const auto &tracking : {forward.reflectionTracking, reverse.reflectionTracking, defined.s21, defined.s12}) {
        const auto error = CheckTracking(tracking);
        if (error != CalibrationError::NONE) {
            return error;
        }
    }
    const auto transmissionProduct = defined.s12 * defined.s21;
    const auto determinant = defined.s11 * defined.s22 - transmissionProduct;
    TwoPortTerms candidate;
    candidate.forwardReflection = forward;
    candidate.reverseReflection = reverse;
    auto error = SolveDirection(measured.s11, measured.s21, defined.s11, defined.s22, defined.s21, transmissionProduct,
        determinant, forward, forwardIsolation, candidate.forwardTransmission);
    if (error != CalibrationError::NONE) {
        return error;
    }
    error = SolveDirection(measured.s22, measured.s12, defined.s22, defined.s11, defined.s12, transmissionProduct,
        determinant, reverse, reverseIsolation, candidate.reverseTransmission);
    if (error != CalibrationError::NONE) {
        return error;
    }
    output = candidate;
    return CalibrationError::NONE;
}

CalibrationError CorrectOnePort(const Complex &measured, const OnePortTerms &terms, Complex &output) noexcept
{
    if (!IsFinite(measured) || !IsFinite(terms)) {
        return CalibrationError::INVALID_INPUT;
    }
    const auto error = CheckTracking(terms.reflectionTracking);
    if (error != CalibrationError::NONE) {
        return error;
    }
    const auto delta = measured - terms.directivity;
    const auto product = terms.sourceMatch * delta;
    return Divide(
        delta, terms.reflectionTracking + product, std::abs(terms.reflectionTracking) + std::abs(product), output);
}

CalibrationError CorrectTwoPort(
    const TwoPortNetwork &measured, const TwoPortTerms &terms, TwoPortNetwork &output) noexcept
{
    if (!IsFinite(measured) || !IsFinite(terms.forwardReflection) || !IsFinite(terms.reverseReflection) ||
        !IsFinite(terms.forwardTransmission) || !IsFinite(terms.reverseTransmission)) {
        return CalibrationError::INVALID_INPUT;
    }
    const auto &forward = terms.forwardReflection;
    const auto &reverse = terms.reverseReflection;
    const auto &forwardTransmission = terms.forwardTransmission;
    const auto &reverseTransmission = terms.reverseTransmission;
    TwoPortNetwork normalized;
    const std::array<Complex, 4U> numerator = {measured.s11 - forward.directivity,
        measured.s21 - forwardTransmission.isolation, measured.s22 - reverse.directivity,
        measured.s12 - reverseTransmission.isolation};
    const std::array<Complex, 4U> tracking = {forward.reflectionTracking, forwardTransmission.transmissionTracking,
        reverse.reflectionTracking, reverseTransmission.transmissionTracking};
    std::array<Complex *, 4U> destinations = {&normalized.s11, &normalized.s21, &normalized.s22, &normalized.s12};
    for (std::size_t index = 0U; index < numerator.size(); ++index) {
        const auto error = Divide(numerator[index], tracking[index], 1.0, *destinations[index]);
        if (error != CalibrationError::NONE) {
            return error;
        }
    }
    const auto &a = normalized.s11;
    const auto &b = normalized.s22;
    const auto &c = normalized.s21;
    const auto &d = normalized.s12;
    const auto &sf = forward.sourceMatch;
    const auto &sr = reverse.sourceMatch;
    const auto &lf = forwardTransmission.loadMatch;
    const auto &lr = reverseTransmission.loadMatch;
    // 0x14003DF90：完整十二项共同分母，四个参数一次联合求逆。
    const auto first = (1.0 + sf * a) * (1.0 + sr * b);
    const auto second = lf * lr * c * d;
    const auto denominator = first - second;
    const double scale = std::abs(first) + std::abs(second);
    const std::array<Complex, 4U> corrected = {a * (1.0 + sr * b) - lf * c * d, c * (1.0 + (sr - lf) * b),
        b * (1.0 + sf * a) - lr * c * d, d * (1.0 + (sf - lr) * a)};
    TwoPortNetwork candidate;
    destinations = {&candidate.s11, &candidate.s21, &candidate.s22, &candidate.s12};
    for (std::size_t index = 0U; index < corrected.size(); ++index) {
        const auto error = Divide(corrected[index], denominator, scale, *destinations[index]);
        if (error != CalibrationError::NONE) {
            return error;
        }
    }
    output = candidate;
    return CalibrationError::NONE;
}

}  // 命名空间 VnaCalibration
