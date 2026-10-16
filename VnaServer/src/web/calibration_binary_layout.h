/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_BINARY_LAYOUT_H
#define VNAPRO_WEB_CALIBRATION_BINARY_LAYOUT_H

#include <cstddef>

#include "vna_calibration/calibration.h"
#include "vna_service/calibration.h"

namespace VnaWeb {
namespace CalibrationBinaryDetail {
constexpr std::size_t FIXED_PAYLOAD_BYTES = 48U;

void ValidateShape(const VnaService::CalibrationSet &set);
void ResizeTerms(VnaCalibration::CalibrationData &data, std::size_t points);
std::size_t PayloadSize(std::size_t points, unsigned components, std::size_t name, std::size_t identity);
}  // 命名空间 CalibrationBinaryDetail
}  // 命名空间 VnaWeb
#endif
