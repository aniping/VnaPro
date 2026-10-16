/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_service/calibration_applicability.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "calibration_applicability_detail.h"
#include "calibration_set_validation.h"
#include "vna_service/calibration.h"

namespace VnaService {
namespace {
using Reason = CalibrationApplicabilityReason;

bool ValidAxis(const std::vector<double> &axis)
{
    if (axis.size() < 2U || axis.size() > std::numeric_limits<unsigned>::max()) {
        return false;
    }
    double previous = 0.0;
    for (double frequency : axis) {
        if (!std::isfinite(frequency) || frequency <= previous || std::floor(frequency) != frequency) {
            return false;
        }
        previous = frequency;
    }
    return true;
}

bool ValidCoefficientShape(const VnaCalibration::CalibrationData &data)
{
    const auto count = data.frequencyHz.size();
    switch (data.method) {
        case VnaCalibration::CalibrationMethod::ONE_PORT:
            return data.onePortTerms.size() == count && data.twoPortTerms.empty() && data.fourPortTerms.empty();
        case VnaCalibration::CalibrationMethod::TWO_PORT:
            return data.twoPortTerms.size() == count && data.onePortTerms.empty() && data.fourPortTerms.empty();
        case VnaCalibration::CalibrationMethod::FOUR_PORT:
            return data.fourPortTerms.size() == count && data.onePortTerms.empty() && data.twoPortTerms.empty();
        default:
            return false;
    }
}

bool ValidPower(const std::vector<double> &power, std::size_t count)
{
    return power.size() == count && std::all_of(power.begin(), power.end(), [](double value) {
        return std::isfinite(value) && value >= VnaCore::MIN_POWER_DBM && value <= VnaCore::MAX_POWER_DBM;
    });
}

void TargetContext(const CalibrationSet &set, const VnaCore::SweepPlan &plan, CalibrationApplicability &result)
{
    result.sourcePoints = set.data ? static_cast<unsigned>(set.data->frequencyHz.size()) : 0U;
    result.targetPoints = static_cast<unsigned>(plan.frequencyHz.size());
    result.targetIfBandwidthHz = plan.ifBandwidthRaw;
    if (!plan.frequencyHz.empty()) {
        result.targetStartHz = std::isfinite(plan.frequencyHz.front()) ? plan.frequencyHz.front() : 0.0;
        result.targetStopHz = std::isfinite(plan.frequencyHz.back()) ? plan.frequencyHz.back() : 0.0;
    }
    const bool finitePower = std::all_of(plan.powerDbm.begin(), plan.powerDbm.end(), [](double power) {
        return std::isfinite(power);
    });
    if (!plan.powerDbm.empty() && finitePower) {
        const auto bounds = std::minmax_element(plan.powerDbm.begin(), plan.powerDbm.end());
        result.targetMinPowerDbm = *bounds.first;
        result.targetMaxPowerDbm = *bounds.second;
    }
}

bool ValidSourceShape(const CalibrationSet &set)
{
    return set.data && ValidCoefficientShape(*set.data) && std::isfinite(set.ifBandwidthHz) &&
           set.ifBandwidthHz > 0.0 &&
           Detail::CalibrationPortsValid(set.data->method, set.data->port1, set.data->port2, 4U) &&
           !(set.data->method == VnaCalibration::CalibrationMethod::ONE_PORT && set.includeIsolation);
}

bool MetadataReady(const CalibrationSet &set, const std::string &identity, const VnaCore::SweepPlan &plan,
    CalibrationApplicability &result)
{
    if (identity.empty() || plan.frequencyHz.empty() || plan.ifBandwidthRaw == 0U) {
        Detail::RejectCalibration(result, Reason::CONFIGURATION_NOT_READY, "Measurement configuration is not ready");
        return false;
    }
    if (!ValidSourceShape(set)) {
        Detail::RejectCalibration(result, Reason::INVALID_COEFFICIENTS, "Calibration coefficient shape is invalid");
        return false;
    }
    if (set.deviceIdentity.empty() || set.deviceIdentity != identity) {
        Detail::RejectCalibration(result, Reason::DEVICE_MISMATCH, "Calibration device identity does not match");
        return false;
    }
    return true;
}

bool AxesReady(const CalibrationSet &set, const VnaCore::SweepPlan &plan, CalibrationApplicability &result)
{
    const auto &axis = set.data->frequencyHz;
    if (!ValidAxis(axis) || !ValidAxis(plan.frequencyHz)) {
        Detail::RejectCalibration(
            result, Reason::INVALID_AXIS, "Calibration requires at least two increasing integer-Hz frequencies");
        return false;
    }
    if (plan.frequencyHz.front() < axis.front() || plan.frequencyHz.back() > axis.back()) {
        Detail::RejectCalibration(result, Reason::OUT_OF_RANGE,
            "Measurement frequency range exceeds the calibrated range; extrapolation is disabled");
        return false;
    }
    if (!ValidPower(set.powerDbm, axis.size()) || !ValidPower(plan.powerDbm, plan.frequencyHz.size())) {
        Detail::RejectCalibration(result, Reason::POWER_MISMATCH, "Calibration or measurement power axis is invalid");
        return false;
    }
    return true;
}

bool AssessPoints(const CalibrationSet &set, const VnaCore::SweepPlan &plan, CalibrationApplicability &result)
{
    std::size_t cursor = 0U;
    for (std::size_t target = 0U; target < plan.frequencyHz.size(); ++target) {
        const auto point = Detail::LocateCalibrationPoint(set.data->frequencyHz, plan.frequencyHz[target], cursor);
        if (set.powerDbm[point.lower] != plan.powerDbm[target] || set.powerDbm[point.upper] != plan.powerDbm[target]) {
            Detail::RejectCalibration(result, Reason::POWER_MISMATCH,
                "Measurement power must match the calibration at both bounding frequencies");
            return false;
        }
        if (point.lower == point.upper) {
            ++result.exactPoints;
        } else {
            ++result.interpolatedPoints;
        }
    }
    return true;
}

void FinishAssessment(const CalibrationSet &set, const VnaCore::SweepPlan &plan, CalibrationApplicability &result)
{
    result.frequencyMode =
        result.targetPoints == result.sourcePoints ? CalibrationFrequencyMode::EXACT : CalibrationFrequencyMode::SUBSET;
    if (result.interpolatedPoints > 0U) {
        result.frequencyMode = CalibrationFrequencyMode::INTERPOLATED;
        result.warnings.push_back(CalibrationWarning::INTERPOLATION);
    }
    if (set.ifBandwidthHz != plan.ifBandwidthRaw) {
        result.warnings.push_back(CalibrationWarning::IF_BANDWIDTH_CHANGED);
    }
    result.applicable = true;
    result.reasonCode = Reason::NONE;
    result.reason.clear();
}
}  // 匿名命名空间

namespace Detail {
CalibrationPoint LocateCalibrationPoint(const std::vector<double> &axis, double frequency, std::size_t &cursor) noexcept
{
    while (cursor + 1U < axis.size() && axis[cursor] < frequency) {
        ++cursor;
    }
    CalibrationPoint point;
    point.upper = cursor;
    point.lower = axis[cursor] == frequency ? cursor : cursor - 1U;
    if (point.lower != point.upper) {
        point.fraction = (frequency - axis[point.lower]) / (axis[point.upper] - axis[point.lower]);
    }
    return point;
}

void RejectCalibration(CalibrationApplicability &result, Reason code, const std::string &reason)
{
    result.applicable = false;
    result.prepared = false;
    result.frequencyMode = CalibrationFrequencyMode::NONE;
    result.reasonCode = code;
    result.reason = reason;
}
}  // 命名空间 Detail

CalibrationApplicability AssessCalibration(
    const CalibrationSet &set, const std::string &identity, const VnaCore::SweepPlan &plan)
{
    CalibrationApplicability result;
    TargetContext(set, plan, result);
    if (MetadataReady(set, identity, plan, result) && AxesReady(set, plan, result) && AssessPoints(set, plan, result)) {
        FinishAssessment(set, plan, result);
    }
    return result;
}
}  // 命名空间 VnaService
