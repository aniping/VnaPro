/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_CALIBRATION_APPLICABILITY_H
#define VNAPRO_VNA_SERVICE_CALIBRATION_APPLICABILITY_H

#include <memory>
#include <string>
#include <vector>

#include "vna_calibration/calibration.h"
#include "vna_core/sweep_plan.h"

namespace VnaService {
struct CalibrationSet;

enum class CalibrationFrequencyMode {
    NONE,
    EXACT,
    SUBSET,
    INTERPOLATED
};
enum class CalibrationApplicabilityReason {
    NONE,
    NO_SELECTION,
    CONFIGURATION_NOT_READY,
    DEVICE_MISMATCH,
    INVALID_AXIS,
    OUT_OF_RANGE,
    POWER_MISMATCH,
    INVALID_COEFFICIENTS
};
enum class CalibrationWarning {
    IF_BANDWIDTH_CHANGED,
    INTERPOLATION
};

struct CalibrationApplicability final {
    bool applicable = false;
    bool prepared = false;
    CalibrationFrequencyMode frequencyMode = CalibrationFrequencyMode::NONE;
    CalibrationApplicabilityReason reasonCode = CalibrationApplicabilityReason::NO_SELECTION;
    std::string reason;
    unsigned sourcePoints = 0U;
    unsigned targetPoints = 0U;
    unsigned exactPoints = 0U;
    unsigned interpolatedPoints = 0U;
    double targetStartHz = 0.0;
    double targetStopHz = 0.0;
    double targetIfBandwidthHz = 0.0;
    double targetMinPowerDbm = 0.0;
    double targetMaxPowerDbm = 0.0;
    std::vector<CalibrationWarning> warnings;
};

struct CalibrationPreparation final {
    CalibrationApplicability applicability;
    VnaCalibration::CalibrationPtr correction;
};

// 只检查元数据及实际频率/功率轴；不分配、校验或改写误差系数数组。
CalibrationApplicability AssessCalibration(
    const CalibrationSet &set, const std::string &identity, const VnaCore::SweepPlan &plan);

// 输入集须已完整验证且冻结。精确轴复用原指针；子集逐项复制，新点用 PolarLinearV1。
// 插值后完整数学验证失败时返回不可用状态及空 correction，始终从原集生成。
CalibrationPreparation PrepareCalibration(
    const std::shared_ptr<const CalibrationSet> &set, const std::string &identity, const VnaCore::SweepPlan &plan);
}  // 命名空间 VnaService

#endif
