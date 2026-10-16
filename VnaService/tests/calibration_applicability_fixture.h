/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_APPLICABILITY_FIXTURE_H
#define VNAPRO_CALIBRATION_APPLICABILITY_FIXTURE_H

#include <memory>
#include <string>
#include <vector>

#include "vna_service/calibration.h"
#include "vna_service/calibration_applicability.h"

namespace CalibrationTest {
using VnaCalibration::CalibrationMethod;
using VnaCalibration::Complex;
using VnaService::CalibrationApplicabilityReason;
using VnaService::CalibrationFrequencyMode;
constexpr const char *IDENTITY = "VnaPro,ApplicabilityFixture,4PORT,1.0";

void Require(bool value, const std::string &message);

void Near(const Complex &actual, const Complex &expected);

bool SameBits(double first, double second);

void Exact(const Complex &actual, const Complex &expected);

VnaCalibration::OnePortTerms Reflection(double point, double direction);

VnaCalibration::TransmissionTerms Transmission(double point, double direction);

std::shared_ptr<VnaService::CalibrationSet> Set(CalibrationMethod method, const std::vector<double> &axis);

VnaCore::SweepPlan Plan(const std::vector<double> &axis);

void CheckReason(const VnaService::CalibrationPreparation &result, CalibrationApplicabilityReason reason);

void RunPreparationTests();
}  // 命名空间 CalibrationTest

#endif
