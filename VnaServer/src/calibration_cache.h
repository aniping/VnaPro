/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_SERVER_CALIBRATION_CACHE_H
#define VNAPRO_SERVER_CALIBRATION_CACHE_H

#include <cstddef>
#include <string>

#include "vna_service/calibration.h"

namespace VnaServer {
namespace CalibrationCache {
constexpr std::size_t BYTE_LIMIT = 8U * 1024U * 1024U;

// 无共享可变状态；可并发用于不同输出对象。源摘要为 Store 的 16 位小写 FNV 十六进制。
// 仅缓存已解析的完整数据，不代替 Restore 的设备、刺激和校准系数语义验证。
// 缓存版本同时约束线格式和 JSON 接受/规范化规则；规则改变时必须失效旧版本。
std::string Encode(const VnaService::CalibrationSet &set, const std::string &sourceDigest, std::size_t sourceBytes);
// 无效、过期或分配失败均返回假，保留原输出；调用者仍须先验证源 JSON 的摘要。
bool Decode(const std::string &bytes, const std::string &sourceDigest, std::size_t sourceBytes,
    VnaService::CalibrationSetPtr &set) noexcept;
}  // 命名空间 CalibrationCache
}  // 命名空间 VnaServer
#endif
