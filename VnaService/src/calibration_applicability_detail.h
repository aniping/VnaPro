/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_APPLICABILITY_DETAIL_H
#define VNAPRO_CALIBRATION_APPLICABILITY_DETAIL_H

#include <cstddef>
#include <vector>

#include "vna_service/calibration_applicability.h"

namespace VnaService {
namespace Detail {
struct CalibrationPoint final {
    std::size_t lower = 0U;
    std::size_t upper = 0U;
    double fraction = 0.0;
};

// 轴已验证且目标处于源范围内；cursor 单调推进，总映射成本 O(M+N)。
CalibrationPoint LocateCalibrationPoint(
    const std::vector<double> &axis, double frequency, std::size_t &cursor) noexcept;
void RejectCalibration(
    CalibrationApplicability &result, CalibrationApplicabilityReason code, const std::string &reason);
}  // 命名空间 Detail
}  // 命名空间 VnaService

#endif
