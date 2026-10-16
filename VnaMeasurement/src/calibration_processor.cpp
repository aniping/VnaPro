/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_processor.h"

#include <array>
#include <cmath>
#include <utility>

#include "vna_core/frame_layout.h"

namespace VnaMeasurement {
namespace Detail {
namespace {

std::string Validate(const VnaCore::MeasurementFrame &frame, const VnaCalibration::CalibrationData &calibration)
{
    const auto error = ValidateCalibrationMetadata(calibration);
    if (!error.empty()) {
        return error;
    }
    const auto points = frame.plan.frequencyRaw.size();
    if (calibration.frequencyHz.size() != points || calibration.frequencyHz != frame.plan.frequencyHz) {
        return "calibration frequency axis does not match the frame";
    }
    for (std::size_t index = 0U; index < points; ++index) {
        const double frequency = calibration.frequencyHz[index];
        if (!std::isfinite(frequency) || frequency < 0.0 ||
            (index != 0U && frequency <= calibration.frequencyHz[index - 1U])) {
            return "calibration frequency axis is invalid";
        }
    }
    if ((calibration.method == VnaCalibration::CalibrationMethod::ONE_PORT &&
            (calibration.onePortTerms.size() != points || !calibration.twoPortTerms.empty() ||
                !calibration.fourPortTerms.empty())) ||
        (calibration.method == VnaCalibration::CalibrationMethod::TWO_PORT &&
            (calibration.twoPortTerms.size() != points || !calibration.onePortTerms.empty() ||
                !calibration.fourPortTerms.empty())) ||
        (calibration.method == VnaCalibration::CalibrationMethod::FOUR_PORT &&
            (calibration.fourPortTerms.size() != points || !calibration.onePortTerms.empty() ||
                !calibration.twoPortTerms.empty()))) {
        return "calibration coefficient shape does not match the frame";
    }
    return {};
}

ComplexPoint Corrected(VnaCalibration::CalibrationError error, const VnaCalibration::Complex &value) noexcept
{
    ComplexPoint point;
    switch (error) {
        case VnaCalibration::CalibrationError::NONE:
            point.real = value.real();
            point.imag = value.imag();
            point.valid = true;
            break;
        case VnaCalibration::CalibrationError::SINGULAR:
            point.error = PointError::CALIBRATION_SINGULAR;
            break;
        case VnaCalibration::CalibrationError::INVALID_INPUT:
            point.error = PointError::INVALID_CALIBRATION;
            break;
        case VnaCalibration::CalibrationError::NONFINITE_RESULT:
            point.error = PointError::NONFINITE_RESULT;
            break;
    }
    return point;
}

VnaCalibration::Complex Value(const ComplexPoint &point) noexcept
{
    return {point.real, point.imag};
}

TraceResult Ratio(const VnaCore::MeasurementFrame &frame, unsigned receiver, unsigned source)
{
    Measurement measurement;
    measurement.kind = MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiver;
    measurement.sourcePort = source;
    return MeasurementProcessor::Calculate(frame, measurement);
}

template <std::size_t COUNT>
std::array<TraceResult, COUNT> Unavailable(const std::string &error)
{
    std::array<TraceResult, COUNT> results;
    for (auto &result : results) {
        result.error = error;
    }
    return results;
}

}  // 匿名命名空间

std::string ValidateCalibrationMetadata(const VnaCalibration::CalibrationData &calibration)
{
    if (calibration.method != VnaCalibration::CalibrationMethod::ONE_PORT &&
        calibration.method != VnaCalibration::CalibrationMethod::TWO_PORT &&
        calibration.method != VnaCalibration::CalibrationMethod::FOUR_PORT) {
        return "calibration method is not supported";
    }
    if (calibration.port1 == 0U || calibration.port1 > VnaCore::PHYSICAL_PORT_COUNT ||
        (calibration.method == VnaCalibration::CalibrationMethod::ONE_PORT && calibration.port2 != 0U) ||
        (calibration.method == VnaCalibration::CalibrationMethod::TWO_PORT &&
            (calibration.port2 == 0U || calibration.port2 > VnaCore::PHYSICAL_PORT_COUNT ||
                calibration.port1 == calibration.port2)) ||
        (calibration.method == VnaCalibration::CalibrationMethod::FOUR_PORT &&
            (calibration.port1 != 1U || calibration.port2 != 4U))) {
        return "calibration contains invalid physical ports";
    }
    return {};
}

std::size_t PairIndex(const Measurement &measurement, const VnaCalibration::CalibrationData &calibration) noexcept
{
    return measurement.sourcePort == calibration.port1 ? (measurement.receiverPort == calibration.port1 ? 0U : 1U)
                                                       : (measurement.receiverPort == calibration.port2 ? 2U : 3U);
}

std::array<TraceResult, 4U> CalculatePair(
    const VnaCore::MeasurementFrame &frame, const VnaCalibration::CalibrationData &calibration)
{
    const auto error = Validate(frame, calibration);
    if (!error.empty()) {
        return Unavailable<4U>(error);
    }
    const auto first = calibration.port1;
    const auto second = calibration.port2;
    std::array<TraceResult, 4U> results = {Ratio(frame, first, first), Ratio(frame, second, first),
        Ratio(frame, second, second), Ratio(frame, first, second)};
    for (const auto &trace : results) {
        if (!trace.available) {
            return Unavailable<4U>(trace.error);
        }
    }
    for (auto &trace : results) {
        trace.calibrated = true;
    }
    for (std::size_t index = 0U; index < frame.plan.frequencyRaw.size(); ++index) {
        bool valid = true;
        ComplexPoint invalid;
        for (const auto &trace : results) {
            if (!trace.points[index].valid) {
                invalid = trace.points[index];
                valid = false;
                break;
            }
        }
        if (!valid) {
            for (auto &trace : results) {
                trace.points[index] = invalid;
            }
            continue;
        }
        const VnaCalibration::TwoPortNetwork measured = {Value(results[0U].points[index]),
            Value(results[1U].points[index]), Value(results[2U].points[index]), Value(results[3U].points[index])};
        VnaCalibration::TwoPortNetwork corrected;
        const auto correctionError =
            VnaCalibration::CorrectTwoPort(measured, calibration.twoPortTerms[index], corrected);
        const std::array<VnaCalibration::Complex, 4U> values = {
            corrected.s11, corrected.s21, corrected.s22, corrected.s12};
        for (std::size_t parameter = 0U; parameter < results.size(); ++parameter) {
            results[parameter].points[index] = Corrected(correctionError, values[parameter]);
        }
    }
    return results;
}

std::array<TraceResult, 16U> CalculateFourPort(
    const VnaCore::MeasurementFrame &frame, const VnaCalibration::CalibrationData &calibration)
{
    const auto error = Validate(frame, calibration);
    if (!error.empty()) {
        return Unavailable<16U>(error);
    }
    std::array<TraceResult, 16U> results;
    for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
        for (unsigned source = 1U; source <= 4U; ++source) {
            auto &trace = results[(receiver - 1U) * 4U + source - 1U];
            trace = Ratio(frame, receiver, source);
            if (!trace.available) {
                return Unavailable<16U>(trace.error);
            }
            trace.calibrated = true;
        }
    }
    for (std::size_t point = 0U; point < frame.plan.frequencyRaw.size(); ++point) {
        VnaCalibration::FourPortNetwork measured{};
        ComplexPoint invalid;
        bool valid = true;
        for (std::size_t index = 0U; index < results.size(); ++index) {
            if (!results[index].points[point].valid) {
                invalid = results[index].points[point];
                valid = false;
                break;
            }
            measured[index] = Value(results[index].points[point]);
        }
        VnaCalibration::FourPortNetwork corrected{};
        const auto correctionError =
            valid ? VnaCalibration::CorrectFourPort(measured, calibration.fourPortTerms[point], corrected)
                  : VnaCalibration::CalibrationError::INVALID_INPUT;
        for (std::size_t index = 0U; index < results.size(); ++index) {
            results[index].points[point] = valid ? Corrected(correctionError, corrected[index]) : invalid;
        }
    }
    return results;
}

TraceResult ApplyCalibration(const VnaCore::MeasurementFrame &frame, const Measurement &measurement,
    const VnaCalibration::CalibrationData &calibration, TraceResult uncalibrated)
{
    if (!calibration.Covers(measurement.receiverPort, measurement.sourcePort)) {
        return uncalibrated;
    }
    const auto error = Validate(frame, calibration);
    if (!error.empty()) {
        TraceResult unavailable;
        unavailable.error = error;
        return unavailable;
    }
    if (calibration.method == VnaCalibration::CalibrationMethod::TWO_PORT) {
        auto pair = CalculatePair(frame, calibration);
        return std::move(pair[PairIndex(measurement, calibration)]);
    }
    if (calibration.method == VnaCalibration::CalibrationMethod::FOUR_PORT) {
        auto matrix = CalculateFourPort(frame, calibration);
        return std::move(matrix[(measurement.receiverPort - 1U) * 4U + measurement.sourcePort - 1U]);
    }
    uncalibrated.calibrated = true;
    for (std::size_t index = 0U; index < uncalibrated.points.size(); ++index) {
        auto &point = uncalibrated.points[index];
        if (point.valid) {
            VnaCalibration::Complex corrected;
            const auto result =
                VnaCalibration::CorrectOnePort(Value(point), calibration.onePortTerms[index], corrected);
            point = Corrected(result, corrected);
        }
    }
    return uncalibrated;
}

}  // 命名空间 Detail
}  // 命名空间 VnaMeasurement
