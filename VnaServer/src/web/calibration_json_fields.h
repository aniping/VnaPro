/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_JSON_FIELDS_H
#define VNAPRO_CALIBRATION_JSON_FIELDS_H

#include <string>
#include <vector>

#include "vna_service/calibration.h"
#include "web/json.h"

namespace VnaWeb {
namespace CalibrationJsonDetail {
const Json &Field(const Json &object, const std::string &name, Json::Type type);
void Fields(const Json &object, const std::vector<std::string> &allowed);
std::string Text(const Json &body, const std::string &name);
double Number(const Json &body, const std::string &name);
unsigned Port(const Json &body, const std::string &name, bool zero = false);
VnaCalibration::CalibrationMethod Method(const std::string &method);
const char *MethodName(VnaCalibration::CalibrationMethod method);
}  // 命名空间 CalibrationJsonDetail
}  // 命名空间 VnaWeb
#endif
