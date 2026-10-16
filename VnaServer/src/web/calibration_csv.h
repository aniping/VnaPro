/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_CSV_H
#define VNAPRO_WEB_CALIBRATION_CSV_H

#include <cstddef>
#include <string>

#include "vna_service/calibration.h"

namespace VnaWeb {
namespace CalibrationCsv {
constexpr std::size_t BYTE_LIMIT = 32U * 1024U * 1024U;

// 自包含 UTF-8 CSV：完整物理刺激、端口及复数误差项；不解算、不插值或改变当前校准。
// 无效输入抛出 invalid_argument；失败不发布任何服务状态。导入服务仍分配本机新 ID。
std::string Encode(const VnaService::CalibrationSet &set);
VnaService::CalibrationSetPtr Decode(const std::string &bytes);
}  // 命名空间 CalibrationCsv
}  // 命名空间 VnaWeb
#endif
