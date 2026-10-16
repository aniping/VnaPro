/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>

#include "calibration_applicability_detail.h"
#include "calibration_coefficient_slots.h"
#include "calibration_set_validation.h"
#include "vna_service/calibration.h"
#include "vna_service/calibration_applicability.h"

namespace VnaService {
namespace {
using VnaCalibration::Complex;
constexpr double TWO_PI = 6.283185307179586476925286766559;

struct PolarSegment final {
    Complex lower;
    Complex upper;
    double lowerMagnitude = 0.0;
    double upperMagnitude = 0.0;
    double lowerPhase = 0.0;
    double phaseDelta = 0.0;
};

PolarSegment PrepareSegment(const Complex &lower, const Complex &upper)
{
    PolarSegment result;
    result.lower = lower;
    result.upper = upper;
    result.lowerMagnitude = std::hypot(lower.real(), lower.imag());
    result.upperMagnitude = std::hypot(upper.real(), upper.imag());
    result.lowerPhase = std::atan2(lower.imag(), lower.real());
    result.phaseDelta = std::remainder(std::atan2(upper.imag(), upper.real()) - result.lowerPhase, TWO_PI);
    return result;
}

Complex Interpolate(const PolarSegment &segment, double fraction)
{
    if (!std::isfinite(segment.lowerMagnitude) || !std::isfinite(segment.upperMagnitude)) {
        return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    }
    if (segment.lowerMagnitude == 0.0 || segment.upperMagnitude == 0.0) {
        // 零幅相位无定义；PolarLinearV1 在这一种情形回退实虚部线性。
        return segment.lower * (1.0 - fraction) + segment.upper * fraction;
    }
    const double magnitude = segment.lowerMagnitude * (1.0 - fraction) + segment.upperMagnitude * fraction;
    const double phase = segment.lowerPhase + segment.phaseDelta * fraction;
    return {magnitude * std::cos(phase), magnitude * std::sin(phase)};
}

template <typename Slots, typename Segments>
void PrepareSegments(const Slots &lower, const Slots &upper, Segments &segments)
{
    for (std::size_t slot = 0U; slot < segments.size(); ++slot) {
        segments[slot] = PrepareSegment(lower[slot], upper[slot]);
    }
}

template <typename Terms>
std::vector<Terms> GatherTerms(
    const std::vector<double> &axis, const std::vector<Terms> &terms, const std::vector<double> &target)
{
    using Slots = decltype(Detail::CoefficientSlots(terms.front()));
    std::array<PolarSegment, std::tuple_size<Slots>::value> segments;
    std::size_t cachedLower = axis.size();
    std::size_t cursor = 0U;
    std::vector<Terms> result;
    result.reserve(target.size());
    for (double frequency : target) {
        const auto point = Detail::LocateCalibrationPoint(axis, frequency, cursor);
        if (point.lower == point.upper) {
            result.push_back(terms[point.lower]);  // 保持所有精确点及负零的原始位。
            continue;
        }
        if (point.lower != cachedLower) {
            PrepareSegments(
                Detail::CoefficientSlots(terms[point.lower]), Detail::CoefficientSlots(terms[point.upper]), segments);
            cachedLower = point.lower;
        }
        Slots values;
        for (std::size_t slot = 0U; slot < values.size(); ++slot) {
            values[slot] = Interpolate(segments[slot], point.fraction);
        }
        Terms interpolated;
        Detail::SetCoefficientSlots(values, interpolated);
        result.push_back(interpolated);
    }
    return result;
}

std::shared_ptr<VnaCalibration::CalibrationData> PrepareTerms(
    const VnaCalibration::CalibrationData &source, const VnaCore::SweepPlan &plan)
{
    auto result = std::make_shared<VnaCalibration::CalibrationData>();
    result->method = source.method;
    result->port1 = source.port1;
    result->port2 = source.port2;
    result->frequencyHz = plan.frequencyHz;
    switch (source.method) {
        case VnaCalibration::CalibrationMethod::ONE_PORT:
            result->onePortTerms = GatherTerms(source.frequencyHz, source.onePortTerms, plan.frequencyHz);
            break;
        case VnaCalibration::CalibrationMethod::TWO_PORT:
            result->twoPortTerms = GatherTerms(source.frequencyHz, source.twoPortTerms, plan.frequencyHz);
            break;
        case VnaCalibration::CalibrationMethod::FOUR_PORT:
            result->fourPortTerms = GatherTerms(source.frequencyHz, source.fourPortTerms, plan.frequencyHz);
            break;
    }
    return result;
}

bool ValidatePrepared(
    const CalibrationSet &source, const VnaCalibration::CalibrationPtr &data, CalibrationApplicability &applicability)
{
    CalibrationSet derived;
    derived.includeIsolation = source.includeIsolation;
    derived.data = data;
    std::string error;
    if (!Detail::ValidateCalibrationCoefficients(derived, error)) {
        Detail::RejectCalibration(applicability, CalibrationApplicabilityReason::INVALID_COEFFICIENTS,
            "Interpolated calibration coefficients are invalid: " + error);
        return false;
    }
    return true;
}
}  // 匿名命名空间

CalibrationPreparation PrepareCalibration(
    const std::shared_ptr<const CalibrationSet> &set, const std::string &identity, const VnaCore::SweepPlan &plan)
{
    CalibrationPreparation result;
    if (!set) {
        result.applicability.reason = "No calibration set is selected";
        return result;
    }
    result.applicability = AssessCalibration(*set, identity, plan);
    if (!result.applicability.applicable) {
        return result;
    }
    if (result.applicability.frequencyMode == CalibrationFrequencyMode::EXACT) {
        result.correction = set->data;
    } else {
        const auto data = PrepareTerms(*set->data, plan);
        if (result.applicability.frequencyMode == CalibrationFrequencyMode::INTERPOLATED &&
            !ValidatePrepared(*set, data, result.applicability)) {
            return result;
        }
        result.correction = data;
    }
    result.applicability.prepared = true;
    return result;
}
}  // 命名空间 VnaService
