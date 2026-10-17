/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SWEEP_PROGRESS_TEST_SUPPORT_H
#define VNAPRO_SWEEP_PROGRESS_TEST_SUPPORT_H

#include <string>

#include "web/frame_response.h"

namespace SweepProgressTest {
void Require(bool condition, const std::string &message);
const VnaWeb::Json &Field(const VnaWeb::Json &value, const std::string &name);
VnaCore::MeasurementFrame Frame(double reflected);
VnaService::InstrumentSnapshot Snapshot();
VnaWeb::Json Response(const VnaService::InstrumentSnapshot &snapshot, bool packed = false);
void ReplaceProgress(VnaService::InstrumentSnapshot &snapshot, VnaCore::MeasurementFrame frame, std::size_t count);
}  // 命名空间 SweepProgressTest

#endif
