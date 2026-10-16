/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_store_data.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "vna_service/instrument_profile.h"

namespace VnaServer {
namespace CalibrationStoreData {
namespace {
constexpr std::size_t BYTE_LIMIT = VnaService::MAX_CALIBRATION_STORAGE_BYTES;

std::size_t Bytes(const VnaService::CalibrationSet &set)
{
    if (!set.data) {
        throw std::invalid_argument("Calibration store set has no coefficients");
    }
    const auto &data = *set.data;
    std::size_t bytes = sizeof(VnaService::CalibrationSet) + sizeof(VnaCalibration::CalibrationData) +
                        sizeof(VnaService::CalibrationSetPtr);
    const auto add = [&bytes](std::size_t count, std::size_t size) {
        if (count > (BYTE_LIMIT - bytes) / size) {
            throw std::invalid_argument("Calibration pool exceeds its coefficient storage budget");
        }
        bytes += count * size;
    };
    add(set.name.size(), sizeof(char));
    add(set.deviceIdentity.size(), sizeof(char));
    add(2U, sizeof(char));
    add(data.frequencyHz.size(), sizeof(double));
    add(set.powerDbm.size(), sizeof(double));
    add(data.onePortTerms.size(), sizeof(VnaCalibration::OnePortTerms));
    add(data.twoPortTerms.size(), sizeof(VnaCalibration::TwoPortTerms));
    add(data.fourPortTerms.size(), sizeof(VnaCalibration::FourPortTerms));
    return bytes;
}

void CheckMetadata(const VnaService::CalibrationSet &set)
{
    if (set.name.size() > 128U || set.deviceIdentity.empty() || set.deviceIdentity.size() > 512U) {
        throw std::invalid_argument("Calibration set exceeds its string, point or coefficient shape limits");
    }
}

void CheckPoints(const VnaService::CalibrationSet &set)
{
    const auto points = set.data->frequencyHz.size();
    if (points < VnaService::MIN_SWEEP_POINTS || points > VnaService::MAX_SWEEP_POINTS ||
        set.powerDbm.size() != points) {
        throw std::invalid_argument("Calibration set exceeds its string, point or coefficient shape limits");
    }
}

void CheckCoefficients(const VnaCalibration::CalibrationData &data)
{
    const auto points = data.frequencyHz.size();
    const bool one = data.method == VnaCalibration::CalibrationMethod::ONE_PORT;
    const bool two = data.method == VnaCalibration::CalibrationMethod::TWO_PORT;
    const bool four = data.method == VnaCalibration::CalibrationMethod::FOUR_PORT;
    if (!one && !two && !four) {
        throw std::invalid_argument("Calibration set exceeds its string, point or coefficient shape limits");
    }
    if (data.onePortTerms.size() != (one ? points : 0U) || data.twoPortTerms.size() != (two ? points : 0U) ||
        data.fourPortTerms.size() != (four ? points : 0U)) {
        throw std::invalid_argument("Calibration set exceeds its string, point or coefficient shape limits");
    }
}

bool SameTerms(const VnaCalibration::OnePortTerms &a, const VnaCalibration::OnePortTerms &b)
{
    return a.directivity == b.directivity && a.sourceMatch == b.sourceMatch &&
           a.reflectionTracking == b.reflectionTracking;
}

bool SameTerms(const VnaCalibration::TransmissionTerms &a, const VnaCalibration::TransmissionTerms &b)
{
    return a.isolation == b.isolation && a.loadMatch == b.loadMatch && a.transmissionTracking == b.transmissionTracking;
}

bool SameTerms(const VnaCalibration::TwoPortTerms &a, const VnaCalibration::TwoPortTerms &b)
{
    return SameTerms(a.forwardReflection, b.forwardReflection) && SameTerms(a.reverseReflection, b.reverseReflection) &&
           SameTerms(a.forwardTransmission, b.forwardTransmission) &&
           SameTerms(a.reverseTransmission, b.reverseTransmission);
}

bool SameTerms(const VnaCalibration::FourPortTerms &a, const VnaCalibration::FourPortTerms &b)
{
    for (std::size_t cell = 0U; cell < a.size(); ++cell) {
        if (a[cell].offset != b[cell].offset || a[cell].match != b[cell].match ||
            a[cell].tracking != b[cell].tracking) {
            return false;
        }
    }
    return true;
}

template <class T>
bool SameTerms(const std::vector<T> &a, const std::vector<T> &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const T &left, const T &right) {
        return SameTerms(left, right);
    });
}

bool SameMetadata(const VnaService::CalibrationSet &a, const VnaService::CalibrationSet &b)
{
    return a.id == b.id && a.name == b.name && a.deviceIdentity == b.deviceIdentity &&
           a.includeIsolation == b.includeIsolation && a.ifBandwidthHz == b.ifBandwidthHz && a.powerDbm == b.powerDbm;
}

bool SameData(const VnaCalibration::CalibrationData &a, const VnaCalibration::CalibrationData &b)
{
    return a.method == b.method && a.port1 == b.port1 && a.port2 == b.port2 && a.frequencyHz == b.frequencyHz &&
           SameTerms(a.onePortTerms, b.onePortTerms) && SameTerms(a.twoPortTerms, b.twoPortTerms) &&
           SameTerms(a.fourPortTerms, b.fourPortTerms);
}
}  // 匿名命名空间

void CheckPool(const std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t selected)
{
    if (sets.size() > VnaService::MAX_CALIBRATION_SETS) {
        throw std::invalid_argument("Calibration pool exceeds its set count budget");
    }
    std::set<std::uint64_t> ids;
    std::size_t bytes = 0U;
    for (const auto &set : sets) {
        if (!set || set->id == 0U || !ids.insert(set->id).second) {
            throw std::invalid_argument("Calibration pool contains a missing or duplicate identifier");
        }
        const auto count = Bytes(*set);
        CheckMetadata(*set);
        CheckPoints(*set);
        CheckCoefficients(*set->data);
        if (count > BYTE_LIMIT - bytes) {
            throw std::invalid_argument("Calibration pool exceeds its coefficient storage budget");
        }
        bytes += count;
    }
    if (selected != 0U && ids.count(selected) == 0U) {
        throw std::invalid_argument("Selected calibration identifier is absent from the pool");
    }
}

bool Same(const VnaService::CalibrationSet &a, const VnaService::CalibrationSet &b)
{
    if (&a == &b) {
        return true;
    }
    if (!SameMetadata(a, b) || !a.data || !b.data) {
        return false;
    }
    return a.data == b.data || SameData(*a.data, *b.data);
}
}  // 命名空间 CalibrationStoreData
}  // 命名空间 VnaServer
