/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_CALIBRATION_PREVIEW_JSON_H
#define VNAPRO_WEB_CALIBRATION_PREVIEW_JSON_H

#include <cstdint>
#include <string>

#include "vna_service/calibration.h"
#include "web/json.h"

namespace VnaWeb {
struct CalibrationPreviewQuery final {
    std::uint64_t sessionId = 0U;
    std::string stepId;
};

// 校准标准的只读协议投影，不修改 DUT 测量目录或显示工作区。
class CalibrationPreviewJson final {
public:
    CalibrationPreviewJson() = delete;
    static CalibrationPreviewQuery ParseQuery(const std::string &query);
    static Json Encode(const VnaService::CalibrationPreviewResult &preview);
};
}  // 命名空间 VnaWeb
#endif
