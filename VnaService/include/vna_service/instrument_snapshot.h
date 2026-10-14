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

namespace VnaService {

struct InstrumentSnapshot final {
    InstrumentSettings settings;
    VnaDisplay::DisplayState display;
    VnaCore::CoreView core;
    CalibrationSnapshot calibration;
    VnaCalibration::CalibrationPtr correction;
    std::string controlOwner;
    bool releasing = false;
    bool pending = false;
    bool finitePending = false;
};

}  // 命名空间 VnaService

#endif
