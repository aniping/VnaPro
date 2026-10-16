/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#include <cstdint>
#include <string>
#include <vector>

#include "vna_service/measurement.h"

namespace VnaService {

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
    bool hasCenterHz = false;
    bool hasSpanHz = false;
    bool hasPoints = false;
    bool hasIfBandwidthHz = false;
    bool hasPowerDbm = false;
    double startHz = 0;
    double stopHz = 0;
    double centerHz = 0;
    double spanHz = 0;
    unsigned points = 0;
    double ifBandwidthHz = 0;
    double powerDbm = 0;
};

struct InstrumentSettings final {
    double startHz = 10000000.0;
    double stopHz = 8000000000.0;
    unsigned points = 201U;
    double ifBandwidthHz = 1000.0;
    double powerDbm = 0.0;
    TriggerSource triggerSource = TriggerSource::IMMEDIATE;
    // 用户选中的扫描方式独立于运行态；停止及单次完成后保留该选择。
    SweepMode selectedSweepMode = SweepMode::CONTINUOUS;
    SweepMode sweepMode = SweepMode::HOLD;
    bool currentTriggerScope = false;
    std::vector<Measurement> measurements;
    // 服务配置与测量目录的逻辑版本，用于写操作的预期版本校验。
    std::uint64_t revision = 0U;
    // 核心实际下发的采集计划版本；纯测量目录更新不会改变此值。
    std::uint64_t acquisitionRevision = 0U;
};

}  // 命名空间 VnaService

#endif
