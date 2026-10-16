/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_BINARY_H
#define VNAPRO_WEB_CALIBRATION_BINARY_H

#include <cstddef>
#include <string>

#include "vna_service/calibration.h"

namespace VnaWeb {
namespace CalibrationBinary {
constexpr std::size_t BYTE_LIMIT = 8U * 1024U * 1024U;

// 自包含 .vcal：固定小端、完整元数据与 IEEE754 binary64；不依赖源 JSON 或旁路缓存。
// VNBCAL02/v2 的32字节头携带 flags=0、payload长度、payload FNV64；负零及整数ID不经文本转换。
// 无效版本、长度、校验或数据形状抛出 invalid_argument；设备/刺激适用性由服务验证。
std::string Encode(const VnaService::CalibrationSet &set);
VnaService::CalibrationSetPtr Decode(const std::string &bytes);
}  // 命名空间 CalibrationBinary
}  // 命名空间 VnaWeb
#endif
