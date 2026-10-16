/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_CALIBRATION_JSON_H
#define VNAPRO_WEB_CALIBRATION_JSON_H

#include "vna_service/calibration.h"
#include "vna_service/instrument_profile.h"
#include "web/json.h"

namespace VnaWeb {

// HTTP 文件封套只含标量字段；系数在有界 binary/CSV 内容中，协议不接收数值 JSON 树。
// CSV 原文最多 32 MiB；另为固定行列、引号及控制字符的 JSON 转义预留 4 MiB。
constexpr std::size_t CALIBRATION_JSON_BYTE_LIMIT = 36U * 1024U * 1024U;
constexpr unsigned CALIBRATION_JSON_NODE_LIMIT = 64U;

// 只转换校准协议表示；采集、系数校验和发布由服务及数学模块拥有。
class CalibrationJson final {
public:
    CalibrationJson() = delete;
    static Json Snapshot(const VnaService::CalibrationSnapshot &snapshot);
    static VnaService::CalibrationChange Change(const Json &body);
};

}  // 命名空间 VnaWeb
#endif
