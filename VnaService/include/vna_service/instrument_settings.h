/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#include <cstdint>
#include <string>
#include <vector>

#include "vna_service/measurement.h"

namespace vna_service {

enum class TriggerSource {
    IMMEDIATE,
    MANUAL
};

enum class SweepMode {
    HOLD,
    CONTINUOUS,
    SINGLE
};

struct ConfigPatch final {
    bool hasStartHz = false;
    bool hasStopHz = false;
    bool hasPoints = false;
    bool hasIfBandwidthHz = false;
    double startHz = 0;
    double stopHz = 0;
    unsigned points = 0;
    double ifBandwidthHz = 0;
};

struct InstrumentSettings final {
    double startHz = 1000000.0;
    double stopHz = 100000000.0;
    unsigned points = 201U;
    double ifBandwidthHz = 1000.0;
    TriggerSource triggerSource = TriggerSource::IMMEDIATE;
    SweepMode sweepMode = SweepMode::HOLD;
    bool currentTriggerScope = false;
    std::vector<Measurement> measurements;
    std::string selectedMeasurement;
    std::uint64_t revision = 0U;
};

}  // namespace vna_service

#endif
