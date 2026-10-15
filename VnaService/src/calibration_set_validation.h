/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_SET_VALIDATION_H
#define VNAPRO_CALIBRATION_SET_VALIDATION_H

#include <string>

#include "vna_service/calibration.h"
#include "vna_service/instrument_profile.h"

namespace VnaService {
namespace Detail {
bool CalibrationNameValid(const std::string &name);
bool CalibrationPortsValid(
    VnaCalibration::CalibrationMethod method, unsigned first, unsigned second, unsigned count) noexcept;
bool CalibrationSetupValid(const CalibrationChange &change, const InstrumentProfile &profile);
bool ValidateImportedCalibration(const CalibrationSet &set, const InstrumentProfile &profile, std::string &error);
}  // 命名空间 Detail
}  // 命名空间 VnaService
#endif
