/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_COEFFICIENT_SLOTS_H
#define VNAPRO_CALIBRATION_COEFFICIENT_SLOTS_H

#include <array>

#include "vna_calibration/calibration.h"

namespace VnaService {
namespace Detail {
std::array<VnaCalibration::Complex, 3U> CoefficientSlots(const VnaCalibration::OnePortTerms &terms);

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 3U> &values, VnaCalibration::OnePortTerms &terms);

std::array<VnaCalibration::Complex, 12U> CoefficientSlots(const VnaCalibration::TwoPortTerms &terms);

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 12U> &values, VnaCalibration::TwoPortTerms &terms);

std::array<VnaCalibration::Complex, 48U> CoefficientSlots(const VnaCalibration::FourPortTerms &terms);

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 48U> &values, VnaCalibration::FourPortTerms &terms);
}  // 命名空间 Detail
}  // 命名空间 VnaService

#endif
