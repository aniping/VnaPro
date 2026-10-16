/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_PLAN_TEST_SUPPORT_H
#define VNAPRO_CALIBRATION_PLAN_TEST_SUPPORT_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vna_service/calibration_workspace.h"

namespace CalibrationPlanTest {
void Require(bool value, const std::string &message);

VnaService::CalibrationSetPtr Source(const std::string &identity, unsigned points = 5U);

VnaCore::SweepPlan Plan(const std::vector<double> &axis, double power = 0.0);

void Import(VnaService::CalibrationWorkspace &workspace, const VnaService::CalibrationSetPtr &set);

void Activate(VnaService::CalibrationWorkspace &workspace, std::uint64_t id = 1U);
}  // 命名空间 CalibrationPlanTest
#endif
