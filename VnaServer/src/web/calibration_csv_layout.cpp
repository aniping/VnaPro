/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_csv_layout.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "vna_core/sweep_plan.h"
#include "vna_service/instrument_profile.h"
#include "web/calibration_csv_reader.h"
#include "web/json.h"

namespace VnaWeb {
namespace CalibrationCsvDetail {
namespace {
using Method = VnaCalibration::CalibrationMethod;

void Append(std::vector<double> &row, const VnaCalibration::Complex &first, const VnaCalibration::Complex &second,
    const VnaCalibration::Complex &third)
{
    for (const auto &value : {first, second, third}) {
        Require(std::isfinite(value.real()) && std::isfinite(value.imag()));
        row.push_back(value.real());
        row.push_back(value.imag());
    }
}

template <class Terms>
Terms TermsAt(const std::vector<std::string> &row, std::size_t offset)
{
    return {{Numeric(row[offset]), Numeric(row[offset + 1U])}, {Numeric(row[offset + 2U]), Numeric(row[offset + 3U])},
        {Numeric(row[offset + 4U]), Numeric(row[offset + 5U])}};
}

void ValidateText(const VnaService::CalibrationSet &set)
{
    Require(set.name.size() <= 128U && !set.deviceIdentity.empty() && set.deviceIdentity.size() <= 512U);
    for (const unsigned char value : set.name) {
        Require(value >= 32U && value != 127U);
    }
    try {
        ValidateJsonString(set.name);
        ValidateJsonString(set.deviceIdentity);
    } catch (const std::runtime_error &) {
        throw std::invalid_argument("Invalid UTF-8 calibration CSV metadata");
    }
}

std::vector<unsigned> ActivePorts(const VnaCalibration::CalibrationData &data)
{
    const bool one = data.method == Method::ONE_PORT;
    const bool four = data.method == Method::FOUR_PORT;
    MethodText(data.method);
    Require(data.port1 >= 1U && data.port1 <= 4U && data.port2 <= 4U);
    Require(one ? data.port2 == 0U : data.port2 != 0U && data.port1 != data.port2);
    Require(!four || (data.port1 == 1U && data.port2 == 4U));
    return one    ? std::vector<unsigned>{data.port1}
           : four ? std::vector<unsigned>{1U, 2U, 3U, 4U}
                  : std::vector<unsigned>{data.port1, data.port2};
}

void ValidateDimensions(const VnaService::CalibrationSet &set)
{
    const auto &data = *set.data;
    const auto points = data.frequencyHz.size();
    Require(points >= VnaService::MIN_SWEEP_POINTS && points <= VnaService::MAX_SWEEP_POINTS);
    Require(set.powerDbm.size() == points && std::isfinite(set.ifBandwidthHz) && set.ifBandwidthHz > 0.0);
    Require(data.onePortTerms.size() == (data.method == Method::ONE_PORT ? points : 0U));
    Require(data.twoPortTerms.size() == (data.method == Method::TWO_PORT ? points : 0U));
    Require(data.fourPortTerms.size() == (data.method == Method::FOUR_PORT ? points : 0U));
}

void ValidateStimulus(const VnaService::CalibrationSet &set)
{
    double previous = 0.0;
    for (std::size_t point = 0U; point < set.data->frequencyHz.size(); ++point) {
        const double hz = set.data->frequencyHz[point];
        const double power = set.powerDbm[point];
        Require(std::isfinite(hz) && hz > previous && std::floor(hz) == hz);
        Require(std::isfinite(power) && power >= VnaCore::MIN_POWER_DBM && power <= VnaCore::MAX_POWER_DBM);
        previous = hz;
    }
}
}  // 匿名命名空间

std::string MethodText(VnaCalibration::CalibrationMethod method)
{
    switch (method) {
        case Method::ONE_PORT:
            return "onePort";
        case Method::TWO_PORT:
            return "twoPort";
        case Method::FOUR_PORT:
            return "fourPort";
    }
    throw std::invalid_argument("Unknown calibration CSV method");
}

VnaCalibration::CalibrationMethod ParseMethod(const std::string &text)
{
    for (const auto method : {Method::ONE_PORT, Method::TWO_PORT, Method::FOUR_PORT}) {
        if (text == MethodText(method)) {
            return method;
        }
    }
    throw std::invalid_argument("Unknown calibration CSV method");
}

std::vector<std::string> Columns(const VnaCalibration::CalibrationData &data)
{
    const auto ports = ActivePorts(data);
    std::vector<std::string> columns{"frequency_hz", "power_dbm"};
    for (const auto receiver : ports) {
        for (const auto source : ports) {
            const std::vector<std::string> names =
                receiver == source ? std::vector<std::string>{"directivity", "source_match", "reflection_tracking"}
                                   : std::vector<std::string>{"isolation", "load_match", "transmission_tracking"};
            for (const auto &name : names) {
                const auto prefix = "S" + std::to_string(receiver) + std::to_string(source) + '_' + name;
                columns.push_back(prefix + "_real");
                columns.push_back(prefix + "_imag");
            }
        }
    }
    return columns;
}

std::vector<double> Values(const VnaCalibration::CalibrationData &data, std::size_t point)
{
    std::vector<double> values;
    values.reserve(96U);
    if (data.method == Method::ONE_PORT) {
        const auto &terms = data.onePortTerms[point];
        Append(values, terms.directivity, terms.sourceMatch, terms.reflectionTracking);
    } else if (data.method == Method::TWO_PORT) {
        const auto &terms = data.twoPortTerms[point];
        Append(values, terms.forwardReflection.directivity, terms.forwardReflection.sourceMatch,
            terms.forwardReflection.reflectionTracking);
        Append(values, terms.reverseTransmission.isolation, terms.reverseTransmission.loadMatch,
            terms.reverseTransmission.transmissionTracking);
        Append(values, terms.forwardTransmission.isolation, terms.forwardTransmission.loadMatch,
            terms.forwardTransmission.transmissionTracking);
        Append(values, terms.reverseReflection.directivity, terms.reverseReflection.sourceMatch,
            terms.reverseReflection.reflectionTracking);
    } else {
        for (const auto &cell : data.fourPortTerms[point]) {
            Append(values, cell.offset, cell.match, cell.tracking);
        }
    }
    return values;
}

void AppendValues(VnaCalibration::CalibrationData &data, const std::vector<std::string> &row)
{
    if (data.method == Method::ONE_PORT) {
        data.onePortTerms.push_back(TermsAt<VnaCalibration::OnePortTerms>(row, 2U));
    } else if (data.method == Method::TWO_PORT) {
        data.twoPortTerms.push_back({TermsAt<VnaCalibration::OnePortTerms>(row, 2U),
            TermsAt<VnaCalibration::OnePortTerms>(row, 20U), TermsAt<VnaCalibration::TransmissionTerms>(row, 14U),
            TermsAt<VnaCalibration::TransmissionTerms>(row, 8U)});
    } else {
        VnaCalibration::FourPortTerms terms;
        for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
            terms[cell] = TermsAt<VnaCalibration::ErrorTerms>(row, 2U + cell * 6U);
        }
        data.fourPortTerms.push_back(terms);
    }
}

void Validate(const VnaService::CalibrationSet &set)
{
    Require(set.data && set.id != 0U && set.id != (std::numeric_limits<std::uint64_t>::max)());
    ValidateDimensions(set);
    ValidateText(set);
    Columns(*set.data);
    Require(set.data->method != Method::ONE_PORT || !set.includeIsolation);
    ValidateStimulus(set);
}
}  // 命名空间 CalibrationCsvDetail
}  // 命名空间 VnaWeb
