/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_STIMULUS_ZOOM_H
#define VNAPRO_VNA_SERVICE_STIMULUS_ZOOM_H

#include <string>

namespace VnaService {

enum class StimulusZoomAction {
    APPLY,
    OUT
};

struct StimulusZoomChange final {
    StimulusZoomAction action = StimulusZoomAction::APPLY;
    std::string traceName;
    double startHz = 0.0;
    double stopHz = 0.0;
    double min = 0.0;
    double max = 0.0;
};

struct StimulusZoomStatus final {
    bool available = false;
    bool canZoomOut = false;
    std::string reason;
    unsigned depth = 0U;
};

}  // 命名空间 VnaService
#endif
