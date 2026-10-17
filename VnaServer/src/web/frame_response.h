/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_FRAME_RESPONSE_H
#define VNAPRO_WEB_FRAME_RESPONSE_H

#include <vector>

#include "vna_service/instrument_snapshot.h"
#include "web/http_types.h"

namespace VnaWeb {
// 测量定义及校准身份由状态和帧响应共用，不在浏览器重复推导。
Json MeasurementJson(const VnaService::Measurement &measurement,
    const VnaCalibration::CalibrationPtr &calibration = VnaCalibration::CalibrationPtr());

// 同一快照投影完整帧与仅供曲线显示的真实采集前缀；不会发布预览标记读数。
HttpResponse ChannelFrameResponse(const VnaService::InstrumentSnapshot &snapshot,
    const VnaService::InstrumentChannelSnapshot &channel, const std::vector<VnaService::Measurement> &selection,
    bool packed);
}  // 命名空间 VnaWeb

#endif
