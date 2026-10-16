/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#include <string>

#include "vna_core/core_types.h"
#include "vna_display/display_workspace.h"
#include "vna_service/calibration.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/stimulus_zoom.h"

namespace VnaService {

struct InstrumentChannelSnapshot final {
    unsigned id = 0U;
    std::string name;
    std::string selectedMeasurement;
    InstrumentSettings settings;
    VnaCore::CoreView core;
    CalibrationSnapshot calibration;
    VnaCalibration::CalibrationPtr correction;
    StimulusZoomStatus stimulusZoom;
    bool pending = false;
    bool finitePending = false;
};

struct InstrumentSnapshot final {
    unsigned activeChannelId = 1U;
    std::vector<InstrumentChannelSnapshot> channels;
    InstrumentSettings settings;
    VnaDisplay::DisplayState display;
    VnaCore::CoreView core;
    CalibrationSnapshot calibration;
    StimulusZoomStatus stimulusZoom;
    VnaCalibration::CalibrationPtr correction;
    std::string controlOwner;
    bool releasing = false;
    bool pending = false;
    bool finitePending = false;
};

}  // 命名空间 VnaService

#endif
