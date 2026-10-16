/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <iostream>
#include <limits>

#include "calibration_applicability_fixture.h"

namespace CalibrationTest {
namespace {
void TestClassification()
{
    const auto set = Set(CalibrationMethod::ONE_PORT, {100, 200, 300, 400, 500});
    const auto exact = VnaService::PrepareCalibration(set, IDENTITY, Plan({100, 200, 300, 400, 500}));
    Require(exact.correction == set->data && exact.applicability.prepared &&
                exact.applicability.frequencyMode == CalibrationFrequencyMode::EXACT,
        "Exact axis was rebuilt");
    const auto subset = VnaService::PrepareCalibration(set, IDENTITY, Plan({200, 400}));
    Require(subset.applicability.frequencyMode == CalibrationFrequencyMode::SUBSET &&
                subset.applicability.exactPoints == 2U && subset.applicability.interpolatedPoints == 0U,
        "Actual subset was incorrectly interpolated");
    const auto changed = VnaService::AssessCalibration(*set, IDENTITY, Plan({100, 150, 300, 450, 500}));
    Require(changed.applicable && !changed.prepared &&
                changed.frequencyMode == CalibrationFrequencyMode::INTERPOLATED && changed.exactPoints == 3U &&
                changed.interpolatedPoints == 2U,
        "Same count with changed Hz was not mapped");
    const auto reduced = VnaService::AssessCalibration(*set, IDENTITY, Plan({150, 350}));
    Require(reduced.frequencyMode == CalibrationFrequencyMode::INTERPOLATED, "Reduced range bypassed interpolation");
}

void TestRangeAndAxes()
{
    const auto set = Set(CalibrationMethod::TWO_PORT, {100, 200, 300});
    CheckReason(
        VnaService::PrepareCalibration(set, IDENTITY, Plan({99, 200})), CalibrationApplicabilityReason::OUT_OF_RANGE);
    CheckReason(
        VnaService::PrepareCalibration(set, IDENTITY, Plan({100, 301})), CalibrationApplicabilityReason::OUT_OF_RANGE);
    for (const auto &axis : std::vector<std::vector<double>>{{200}, {100, 100}, {200, 100}, {100.5, 200},
             {100, std::numeric_limits<double>::infinity()}, {100, std::numeric_limits<double>::quiet_NaN()}}) {
        CheckReason(
            VnaService::PrepareCalibration(set, IDENTITY, Plan(axis)), CalibrationApplicabilityReason::INVALID_AXIS);
    }
    CheckReason(VnaService::PrepareCalibration(set, IDENTITY, Plan({})),
        CalibrationApplicabilityReason::CONFIGURATION_NOT_READY);
    const auto invalid = Set(CalibrationMethod::ONE_PORT, {100, 100});
    CheckReason(VnaService::PrepareCalibration(invalid, IDENTITY, Plan({100, 200})),
        CalibrationApplicabilityReason::INVALID_AXIS);
}

void TestIdentityAndShape()
{
    const auto set = Set(CalibrationMethod::ONE_PORT, {100, 200, 300});
    CheckReason(VnaService::PrepareCalibration(nullptr, IDENTITY, Plan({100, 200})),
        CalibrationApplicabilityReason::NO_SELECTION);
    CheckReason(VnaService::PrepareCalibration(set, "different device", Plan({100, 200})),
        CalibrationApplicabilityReason::DEVICE_MISMATCH);
    CheckReason(VnaService::PrepareCalibration(set, "", Plan({100, 200})),
        CalibrationApplicabilityReason::CONFIGURATION_NOT_READY);
    const auto malformed = std::make_shared<VnaService::CalibrationSet>(*set);
    malformed->data.reset();
    CheckReason(VnaService::PrepareCalibration(malformed, IDENTITY, Plan({100, 200})),
        CalibrationApplicabilityReason::INVALID_COEFFICIENTS);
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*set->data);
    data->onePortTerms.pop_back();
    malformed->data = data;
    CheckReason(VnaService::PrepareCalibration(malformed, IDENTITY, Plan({100, 200})),
        CalibrationApplicabilityReason::INVALID_COEFFICIENTS);
}

void TestBandwidthWarning()
{
    const auto set = Set(CalibrationMethod::FOUR_PORT, {100, 200, 300});
    for (unsigned bandwidth : {100U, 10000U}) {
        auto plan = Plan({100, 200, 300});
        plan.ifBandwidthRaw = bandwidth;
        const auto result = VnaService::PrepareCalibration(set, IDENTITY, plan);
        Require(
            result.correction == set->data && result.applicability.prepared &&
                result.applicability.warnings ==
                    std::vector<VnaService::CalibrationWarning>{VnaService::CalibrationWarning::IF_BANDWIDTH_CHANGED},
            "IFBW changed validity or frequency mode");
        Require(result.applicability.targetIfBandwidthHz == bandwidth, "Target IFBW context was lost");
    }
    auto plan = Plan({150, 250});
    plan.ifBandwidthRaw = 100U;
    const auto result = VnaService::PrepareCalibration(set, IDENTITY, plan);
    Require(result.applicability.warnings.size() == 2U && result.applicability.prepared,
        "IFBW and interpolation warnings did not coexist");
}

void TestPowerGate()
{
    const auto set = Set(CalibrationMethod::ONE_PORT, {100, 200, 300, 400, 500});
    set->powerDbm = {0, 0, 1, 1, 1};
    Require(VnaService::PrepareCalibration(set, IDENTITY, Plan({100, 200})).applicability.applicable,
        "Exact points incorrectly compared unused source powers");
    auto plan = Plan({150, 350});
    plan.powerDbm = {0, 1};
    Require(VnaService::PrepareCalibration(set, IDENTITY, plan).applicability.prepared,
        "Equal bracket power did not permit frequency interpolation");
    CheckReason(VnaService::PrepareCalibration(set, IDENTITY, Plan({100, 250})),
        CalibrationApplicabilityReason::POWER_MISMATCH);
    CheckReason(VnaService::PrepareCalibration(set, IDENTITY, Plan({100, 300})),
        CalibrationApplicabilityReason::POWER_MISMATCH);
    plan.powerDbm = {0};
    CheckReason(VnaService::PrepareCalibration(set, IDENTITY, plan), CalibrationApplicabilityReason::POWER_MISMATCH);
    plan.powerDbm = {0, std::numeric_limits<double>::quiet_NaN()};
    CheckReason(VnaService::PrepareCalibration(set, IDENTITY, plan), CalibrationApplicabilityReason::POWER_MISMATCH);
}

void TestInvalidPowerContext()
{
    const auto set = Set(CalibrationMethod::ONE_PORT, {100, 200});
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    for (const auto &powers : std::vector<std::vector<double>>{
             {nan, -10}, {-10, nan}, {infinity, -10}, {-10, infinity}, {-infinity, -10}, {-10, -infinity}}) {
        auto plan = Plan({100, 200});
        plan.powerDbm = powers;
        const auto result = VnaService::PrepareCalibration(set, IDENTITY, plan);
        CheckReason(result, CalibrationApplicabilityReason::POWER_MISMATCH);
        Require(result.applicability.targetMinPowerDbm == 0.0 && result.applicability.targetMaxPowerDbm == 0.0,
            "Nonfinite power axis did not produce a finite ordered blocked context");
    }
    auto plan = Plan({100, 200});
    plan.powerDbm = {-20, -10};
    const auto result = VnaService::AssessCalibration(*set, IDENTITY, plan);
    Require(result.targetMinPowerDbm == -20.0 && result.targetMaxPowerDbm == -10.0,
        "Finite blocked power context lost its actual minimum or maximum");
}
}  // 匿名命名空间
}  // 命名空间 CalibrationTest

int main()
{
    try {
        CalibrationTest::TestClassification();
        CalibrationTest::TestRangeAndAxes();
        CalibrationTest::TestIdentityAndShape();
        CalibrationTest::TestBandwidthWarning();
        CalibrationTest::TestPowerGate();
        CalibrationTest::TestInvalidPowerContext();
        CalibrationTest::RunPreparationTests();
        std::cout << "calibration applicability tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
