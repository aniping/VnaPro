/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_INSTRUMENT_CONFIGURATION_H
#define VNAPRO_INSTRUMENT_CONFIGURATION_H

#include "vna_core/sweep_plan.h"
#include "vna_service/instrument_profile.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/operation_result.h"

namespace VnaService {
namespace Detail {
bool ValidFrequency(double value, const InstrumentProfile &profile) noexcept;
double QuantizedBandwidth(double value) noexcept;
VnaCore::SweepPlan MakePlan(const InstrumentSettings &settings, const InstrumentProfile &profile, unsigned channelId);
bool CoversPlan(const VnaCore::SweepPlan &active, const VnaCore::SweepPlan &requested);
// 在候选值上完成全部校验，失败不影响已提交设置；显式 Auto 优先于数值的隐式关闭。
OperationResult ApplySettingsPatch(const ConfigPatch &patch, InstrumentSettings &settings,
    const InstrumentProfile &profile, bool manualSweepTimeSupported);
}  // 命名空间 Detail
}  // 命名空间 VnaService

#endif
