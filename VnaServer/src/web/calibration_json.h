/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_CALIBRATION_JSON_H
#define VNAPRO_WEB_CALIBRATION_JSON_H

#include "vna_service/calibration.h"
#include "vna_service/instrument_profile.h"
#include "web/json.h"

namespace VnaWeb {

// 最大四端口文件：10001 点，每点 96 个系数分量、行节点、频率及功率。
constexpr std::size_t CALIBRATION_JSON_BYTE_LIMIT = 32U * 1024U * 1024U;
constexpr unsigned CALIBRATION_JSON_NODE_LIMIT = 102U * VnaService::MAX_SWEEP_POINTS + 64U;

// 只转换校准协议表示；采集、系数校验和发布由服务及数学模块拥有。
class CalibrationJson final {
public:
    CalibrationJson() = delete;
    static Json Snapshot(const VnaService::CalibrationSnapshot &snapshot);
    static Json Export(const VnaService::CalibrationSet &set);
    static VnaService::CalibrationChange Change(const Json &body);

private:
    static VnaService::CalibrationSetPtr Import(const Json &document);
};

}  // 命名空间 VnaWeb
#endif
