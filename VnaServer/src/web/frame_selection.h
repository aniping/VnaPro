/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_FRAME_SELECTION_H
#define VNAPRO_WEB_FRAME_SELECTION_H

#include <string>
#include <vector>

#include "vna_service/measurement.h"

namespace VnaWeb {
struct FrameQuery final {
    std::string selection;
    bool packed = false;
    unsigned channelId = 0U;
};

// 分离显示集合选择与数组编码；重复编码字段、未知编码或空字段均拒绝。
FrameQuery ParseFrameQuery(const std::string &query);

// 从测量目录选择预算内全部显示迹线；非法选择抛出 invalid_argument。
std::vector<VnaService::Measurement> SelectFrameMeasurements(
    const std::string &query, const std::vector<VnaService::Measurement> &catalog);
}  // 命名空间 VnaWeb

#endif
