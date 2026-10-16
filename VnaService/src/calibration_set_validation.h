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
// 含对象、目录槽及字符串/向量内容；超过预算时返回预算加一，避免 size_t 溢出。
std::size_t CalibrationStorageBytes(const CalibrationSet &set) noexcept;
}  // 命名空间 Detail
}  // 命名空间 VnaService
#endif
