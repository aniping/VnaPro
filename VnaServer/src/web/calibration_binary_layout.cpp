/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_binary_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "vna_core/sweep_plan.h"
#include "vna_service/instrument_profile.h"
#include "web/json.h"

namespace VnaWeb {
namespace CalibrationBinaryDetail {
namespace {
using Method = VnaCalibration::CalibrationMethod;

void Require(bool condition)
{
    if (!condition) {
        throw std::invalid_argument("Invalid calibration binary document");
    }
}

void ValidateText(const VnaService::CalibrationSet &set)
{
    Require(set.name.size() <= 128U && !set.deviceIdentity.empty() && set.deviceIdentity.size() <= 512U);
    Require(std::none_of(set.name.begin(), set.name.end(), [](unsigned char value) {
        return value < 32U || value == 127U;
    }));
    try {
        ValidateJsonString(set.name);
        ValidateJsonString(set.deviceIdentity);
    } catch (const std::runtime_error &) {
        throw std::invalid_argument("Invalid UTF-8 calibration metadata");
    }
}

void ValidatePorts(const VnaCalibration::CalibrationData &data, bool includeIsolation)
{
    Require(data.port1 >= 1U && data.port1 <= 4U && data.port2 <= 4U);
    switch (data.method) {
        case Method::ONE_PORT:
            Require(data.port2 == 0U && !includeIsolation);
            return;
        case Method::TWO_PORT:
            Require(data.port2 != 0U && data.port1 != data.port2);
            return;
        case Method::FOUR_PORT:
            Require(data.port1 == 1U && data.port2 == 4U);
            return;
    }
    throw std::invalid_argument("Unknown calibration binary method");
}

void ValidateCoefficientShape(const VnaCalibration::CalibrationData &data)
{
    const auto points = data.frequencyHz.size();
    Require(data.onePortTerms.size() == (data.method == Method::ONE_PORT ? points : 0U));
    Require(data.twoPortTerms.size() == (data.method == Method::TWO_PORT ? points : 0U));
    Require(data.fourPortTerms.size() == (data.method == Method::FOUR_PORT ? points : 0U));
}

void ValidatePower(const std::vector<double> &powerDbm)
{
    for (const auto power : powerDbm) {
        Require(std::isfinite(power) && power >= VnaCore::MIN_POWER_DBM && power <= VnaCore::MAX_POWER_DBM);
    }
}
}  // 匿名命名空间

void ValidateShape(const VnaService::CalibrationSet &set)
{
    Require(set.data && set.id != 0U && set.id != (std::numeric_limits<std::uint64_t>::max)());
    const auto points = set.data->frequencyHz.size();
    Require(points >= VnaService::MIN_SWEEP_POINTS && points <= VnaService::MAX_SWEEP_POINTS &&
            set.powerDbm.size() == points && std::isfinite(set.ifBandwidthHz));
    ValidateText(set);
    ValidatePorts(*set.data, set.includeIsolation);
    ValidateCoefficientShape(*set.data);
    ValidatePower(set.powerDbm);
}

void ResizeTerms(VnaCalibration::CalibrationData &data, std::size_t points)
{
    data.onePortTerms.resize(data.method == Method::ONE_PORT ? points : 0U);
    data.twoPortTerms.resize(data.method == Method::TWO_PORT ? points : 0U);
    data.fourPortTerms.resize(data.method == Method::FOUR_PORT ? points : 0U);
}

std::size_t PayloadSize(std::size_t points, unsigned components, std::size_t name, std::size_t identity)
{
    // 调用前已限定 points<=10001、components<=96、字符串<=128/512。
    return FIXED_PAYLOAD_BYTES + name + identity + points * (components + 2U) * sizeof(double);
}
}  // 命名空间 CalibrationBinaryDetail
}  // 命名空间 VnaWeb
