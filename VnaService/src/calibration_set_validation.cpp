/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_set_validation.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "vna_core/sweep_plan.h"

namespace VnaService {
namespace {
using VnaCalibration::Complex;
constexpr std::size_t MAX_NAME_BYTES = 128U;

bool Finite(const Complex &value) noexcept
{
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

bool ValidTerms(const VnaCalibration::OnePortTerms &terms) noexcept
{
    Complex corrected;
    return Finite(terms.directivity) && Finite(terms.sourceMatch) && Finite(terms.reflectionTracking) &&
           VnaCalibration::CorrectOnePort(terms.directivity, terms, corrected) ==
               VnaCalibration::CalibrationError::NONE;
}

bool ValidTerms(const VnaCalibration::TwoPortTerms &terms, bool includeIsolation) noexcept
{
    VnaCalibration::TwoPortNetwork offsets, corrected;
    offsets.s11 = terms.forwardReflection.directivity;
    offsets.s21 = terms.forwardTransmission.isolation;
    offsets.s22 = terms.reverseReflection.directivity;
    offsets.s12 = terms.reverseTransmission.isolation;
    // 导入使用实际校正函数的跟踪项门槛，不能接受启用后必然全部失败的系数。
    return (includeIsolation || (offsets.s21 == Complex{} && offsets.s12 == Complex{})) &&
           VnaCalibration::CorrectTwoPort(offsets, terms, corrected) == VnaCalibration::CalibrationError::NONE;
}

bool ValidTerms(const VnaCalibration::FourPortTerms &terms, bool includeIsolation) noexcept
{
    VnaCalibration::FourPortNetwork offsets, corrected;
    for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
        if (!Finite(terms[cell].offset) || !Finite(terms[cell].match) || !Finite(terms[cell].tracking) ||
            (!includeIsolation && cell / 4U != cell % 4U && terms[cell].offset != Complex{})) {
            return false;
        }
        offsets[cell] = terms[cell].offset;
    }
    return VnaCalibration::CorrectFourPort(offsets, terms, corrected) == VnaCalibration::CalibrationError::NONE;
}

bool ValidShape(const CalibrationSet &set, const InstrumentProfile &profile)
{
    const auto &data = *set.data;
    const auto count = data.frequencyHz.size();
    return Detail::CalibrationPortsValid(data.method, data.port1, data.port2, profile.portCount) &&
           count >= MIN_SWEEP_POINTS && count <= MAX_SWEEP_POINTS && count == set.powerDbm.size() &&
           std::isfinite(set.ifBandwidthHz) &&
           !(data.method == VnaCalibration::CalibrationMethod::ONE_PORT && set.includeIsolation);
}

bool ValidFrequencyAxis(const std::vector<double> &axis, const InstrumentProfile &profile)
{
    double previous = 0.0;
    for (double frequency : axis) {
        if (!std::isfinite(frequency) || frequency < profile.minFrequencyHz || frequency > profile.maxFrequencyHz ||
            frequency <= previous || std::floor(frequency) != frequency) {
            return false;
        }
        previous = frequency;
    }
    return true;
}

bool ValidateStimulus(const CalibrationSet &set, const InstrumentProfile &profile, std::string &error)
{
    const std::array<double, 5U> bandwidths{{10.0, 100.0, 1000.0, 10000.0, 100000.0}};
    if (std::find(bandwidths.begin(), bandwidths.end(), set.ifBandwidthHz) == bandwidths.end()) {
        error = "Calibration set IF bandwidth is unsupported";
        return false;
    }
    if (!ValidFrequencyAxis(set.data->frequencyHz, profile)) {
        error = "Calibration set frequency axis is invalid for this instrument";
        return false;
    }
    if (!std::all_of(set.powerDbm.begin(), set.powerDbm.end(), [](double power) {
            return std::isfinite(power) && power >= VnaCore::MIN_POWER_DBM && power <= VnaCore::MAX_POWER_DBM;
        })) {
        error = "Calibration powerDbm must be finite numbers in [-120, 100]";
        return false;
    }
    return true;
}

bool ValidCoefficientShape(const VnaCalibration::CalibrationData &data)
{
    const auto count = data.frequencyHz.size();
    if (data.method == VnaCalibration::CalibrationMethod::ONE_PORT) {
        return data.onePortTerms.size() == count && data.twoPortTerms.empty() && data.fourPortTerms.empty();
    }
    if (data.method == VnaCalibration::CalibrationMethod::FOUR_PORT) {
        return data.fourPortTerms.size() == count && data.onePortTerms.empty() && data.twoPortTerms.empty();
    }
    return data.twoPortTerms.size() == count && data.onePortTerms.empty() && data.fourPortTerms.empty();
}

bool ValidCoefficients(const CalibrationSet &set)
{
    const auto &data = *set.data;
    if (data.method == VnaCalibration::CalibrationMethod::ONE_PORT) {
        return std::all_of(
            data.onePortTerms.begin(), data.onePortTerms.end(), [](const VnaCalibration::OnePortTerms &terms) {
                return ValidTerms(terms);
            });
    }
    if (data.method == VnaCalibration::CalibrationMethod::FOUR_PORT) {
        return std::all_of(
            data.fourPortTerms.begin(), data.fourPortTerms.end(), [&set](const VnaCalibration::FourPortTerms &terms) {
                return ValidTerms(terms, set.includeIsolation);
            });
    }
    return std::all_of(
        data.twoPortTerms.begin(), data.twoPortTerms.end(), [&set](const VnaCalibration::TwoPortTerms &terms) {
            return ValidTerms(terms, set.includeIsolation);
        });
}
}  // 匿名命名空间

namespace Detail {
std::size_t CalibrationStorageBytes(const CalibrationSet &set) noexcept
{
    const auto overflow = MAX_CALIBRATION_STORAGE_BYTES + 1U;
    if (!set.data) {
        return overflow;
    }
    std::size_t bytes = sizeof(CalibrationSet) + sizeof(VnaCalibration::CalibrationData) + sizeof(CalibrationSetPtr);
    const auto add = [&bytes, overflow](std::size_t count, std::size_t elementSize) {
        if (bytes >= overflow || count > (overflow - bytes) / elementSize) {
            bytes = overflow;
        } else {
            bytes += count * elementSize;
        }
    };
    add(set.name.size(), sizeof(char));
    add(set.deviceIdentity.size(), sizeof(char));
    add(2U, sizeof(char));
    add(set.powerDbm.size(), sizeof(double));
    add(set.data->frequencyHz.size(), sizeof(double));
    add(set.data->onePortTerms.size(), sizeof(VnaCalibration::OnePortTerms));
    add(set.data->twoPortTerms.size(), sizeof(VnaCalibration::TwoPortTerms));
    add(set.data->fourPortTerms.size(), sizeof(VnaCalibration::FourPortTerms));
    return bytes;
}

bool CalibrationNameValid(const std::string &name)
{
    return name.size() <= MAX_NAME_BYTES && std::none_of(name.begin(), name.end(), [](unsigned char value) {
        return value < 32U || value == 127U;
    });
}

bool CalibrationPortsValid(
    VnaCalibration::CalibrationMethod method, unsigned first, unsigned second, unsigned count) noexcept
{
    return first > 0U && first <= count &&
           ((method == VnaCalibration::CalibrationMethod::ONE_PORT && second == 0U) ||
               (method == VnaCalibration::CalibrationMethod::TWO_PORT && second > 0U && second <= count &&
                   second != first) ||
               (method == VnaCalibration::CalibrationMethod::FOUR_PORT && count >= 4U && first == 1U && second == 4U));
}

bool CalibrationSetupValid(const CalibrationChange &change, const InstrumentProfile &profile)
{
    return CalibrationNameValid(change.name) &&
           CalibrationPortsValid(change.method, change.port1, change.port2, profile.portCount) &&
           !(change.method == VnaCalibration::CalibrationMethod::ONE_PORT && change.includeIsolation);
}

bool ValidateCalibrationMetadata(const CalibrationSet &set, const InstrumentProfile &profile, std::string &error)
{
    if (!set.data || !CalibrationNameValid(set.name) || set.deviceIdentity.empty() ||
        set.deviceIdentity.size() > 512U) {
        error = "Calibration set name, identity or data is invalid";
        return false;
    }
    if (!ValidShape(set, profile)) {
        error = "Calibration set ports, frequency axis or stimulus are invalid";
        return false;
    }
    if (!ValidateStimulus(set, profile, error)) {
        return false;
    }
    if (!ValidCoefficientShape(*set.data)) {
        error = "Calibration coefficient shape, values or isolation metadata are invalid";
        return false;
    }
    return true;
}

bool ValidateCalibrationCoefficients(const CalibrationSet &set, std::string &error)
{
    if (!set.data || !ValidCoefficientShape(*set.data) || !ValidCoefficients(set)) {
        error = "Calibration coefficient shape, values or isolation metadata are invalid";
        return false;
    }
    return true;
}

bool ValidateImportedCalibration(const CalibrationSet &set, const InstrumentProfile &profile, std::string &error)
{
    return ValidateCalibrationMetadata(set, profile, error) && ValidateCalibrationCoefficients(set, error);
}
}  // 命名空间 Detail
}  // 命名空间 VnaService
