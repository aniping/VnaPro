/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_CSV_LAYOUT_H
#define VNAPRO_WEB_CALIBRATION_CSV_LAYOUT_H

#include <cstddef>
#include <string>
#include <vector>

#include "vna_service/calibration.h"

namespace VnaWeb {
namespace CalibrationCsvDetail {
std::string MethodText(VnaCalibration::CalibrationMethod method);
VnaCalibration::CalibrationMethod ParseMethod(const std::string &text);
std::vector<std::string> Columns(const VnaCalibration::CalibrationData &data);
std::vector<double> Values(const VnaCalibration::CalibrationData &data, std::size_t point);
void AppendValues(VnaCalibration::CalibrationData &data, const std::vector<std::string> &row);
void Validate(const VnaService::CalibrationSet &set);
}  // 命名空间 CalibrationCsvDetail
}  // 命名空间 VnaWeb
#endif
