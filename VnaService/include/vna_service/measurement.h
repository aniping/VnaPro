/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#include "vna_measurement/measurement.h"

namespace VnaService {

// 测量定义由计算域统一维护，服务层仅使用这一份合同。
using Measurement = VnaMeasurement::Measurement;
using MeasurementKind = VnaMeasurement::MeasurementKind;

}  // 命名空间 VnaService

#endif
