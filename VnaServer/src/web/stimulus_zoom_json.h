/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_STIMULUS_ZOOM_JSON_H
#define VNAPRO_WEB_STIMULUS_ZOOM_JSON_H

#include "vna_service/stimulus_zoom.h"
#include "web/json.h"

namespace VnaWeb {

// 只转换刺激缩放协议；频率、显示事务和回退历史由仪器服务执行。
class StimulusZoomJson final {
public:
    StimulusZoomJson() = delete;
    // 严格解析动作对应字段，不接受浏览器图形尺寸或局部状态。
    static VnaService::StimulusZoomChange Change(const Json &body);
    // 输出服务快照中的可用性和有界回退历史状态。
    static Json Status(const VnaService::StimulusZoomStatus &status);
};

}  // 命名空间 VnaWeb
#endif
