/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_applicability_fixture.h"

#include <cmath>
#include <complex>
#include <cstring>
#include <stdexcept>

namespace CalibrationTest {

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Near(const Complex &actual, const Complex &expected)
{
    Require(std::abs(actual - expected) < 1e-12, "Interpolated complex coefficient differs");
}

bool SameBits(double first, double second)
{
    return std::memcmp(&first, &second, sizeof(double)) == 0;
}

void Exact(const Complex &actual, const Complex &expected)
{
    Require(SameBits(actual.real(), expected.real()) && SameBits(actual.imag(), expected.imag()),
        "Exact coefficient bits changed");
}

VnaCalibration::OnePortTerms Reflection(double point, double direction)
{
    return {{0.001 * point + direction, 0.002}, {0.003, 0.0001 * point}, {0.8 + 0.01 * point, direction}};
}

VnaCalibration::TransmissionTerms Transmission(double point, double direction)
{
    return {{0.0001 * point, direction}, {0.001 + direction, 0.0001 * point}, {0.7 + 0.01 * point, direction}};
}

std::shared_ptr<VnaService::CalibrationSet> Set(CalibrationMethod method, const std::vector<double> &axis)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = method;
    data->port1 = method == CalibrationMethod::FOUR_PORT ? 1U : 4U;
    data->port2 = method == CalibrationMethod::ONE_PORT ? 0U : 2U;
    if (method == CalibrationMethod::FOUR_PORT) {
        data->port2 = 4U;
    }
    data->frequencyHz = axis;
    for (std::size_t index = 0U; index < axis.size(); ++index) {
        const auto point = static_cast<double>(index);
        if (method == CalibrationMethod::ONE_PORT) {
            data->onePortTerms.push_back(Reflection(point, 0.01));
        } else if (method == CalibrationMethod::TWO_PORT) {
            data->twoPortTerms.push_back({Reflection(point, 0.01), Reflection(point, 0.02), Transmission(point, 0.003),
                Transmission(point, 0.004)});
        } else {
            VnaCalibration::FourPortTerms terms;
            for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
                const auto direction = static_cast<double>(cell) * 0.0001;
                terms[cell] = {{point * 0.0001, direction}, {0.002 + direction, point * 0.00001},
                    {0.8 + point * 0.001, direction}};
            }
            data->fourPortTerms.push_back(terms);
        }
    }
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->id = 1U;
    set->name = "Nonconstant calibration";
    set->deviceIdentity = IDENTITY;
    set->ifBandwidthHz = 1000.0;
    set->includeIsolation = method != CalibrationMethod::ONE_PORT;
    set->powerDbm.assign(axis.size(), 0.0);
    set->data = data;
    return set;
}

VnaCore::SweepPlan Plan(const std::vector<double> &axis)
{
    VnaCore::SweepPlan plan;
    plan.frequencyHz = axis;
    plan.powerDbm.assign(axis.size(), 0.0);
    plan.ifBandwidthRaw = 1000U;
    return plan;
}

void CheckReason(const VnaService::CalibrationPreparation &result, CalibrationApplicabilityReason reason)
{
    Require(!result.correction && !result.applicability.applicable && !result.applicability.prepared &&
                result.applicability.reasonCode == reason && !result.applicability.reason.empty(),
        "Rejected preparation did not retain its reason or returned coefficients");
}
}  // 命名空间 CalibrationTest
