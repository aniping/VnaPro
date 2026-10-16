/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_CALIBRATION_APPLICABILITY_JSON_H
#define VNAPRO_WEB_CALIBRATION_APPLICABILITY_JSON_H

#include "vna_service/calibration.h"
#include "web/json.h"

namespace VnaWeb {

Json EncodeCalibrationApplicability(const VnaService::CalibrationApplicability &applicability);
Json EncodeCalibrationSetSummary(const VnaService::CalibrationSetSummary &set);

}  // 命名空间 VnaWeb
#endif
