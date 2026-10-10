/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#include <string>

#include "vna_core/core_types.h"
#include "vna_service/instrument_settings.h"

namespace VnaService {

struct InstrumentSnapshot final {
    InstrumentSettings settings;
    VnaCore::CoreView core;
    std::string controlOwner;
    bool releasing = false;
    bool pending = false;
    bool finitePending = false;
};

}  // 命名空间 VnaService

#endif
