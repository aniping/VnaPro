/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CALIBRATION_WORKSPACE_CACHE_H
#define VNAPRO_CALIBRATION_WORKSPACE_CACHE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vna_core/sweep_plan.h"
#include "vna_service/calibration.h"

namespace VnaService {
namespace Detail {

struct CalibrationCachedSummary final {
    CalibrationSetPtr set;
    CalibrationSetSummary summary;
};

struct CalibrationDerivedEntry final {
    CalibrationSetPtr source;
    VnaCalibration::CalibrationPtr correction;
    std::shared_ptr<const std::vector<double>> powerDbm;
    std::size_t bytes = 0U;
};

// 候选工作区共享不可变缓存；仅新事务构造替代对象，不修改已发布结果。
struct CalibrationPlanCache final {
    std::string identity;
    std::vector<double> frequencyHz;
    std::vector<double> powerDbm;
    std::shared_ptr<const std::vector<double>> powerKey;
    std::uint32_t ifBandwidthRaw = 0U;
    std::uint64_t selectedSetId = 0U;
    std::vector<CalibrationCachedSummary> summaries;
    CalibrationPreparation selected;
    std::vector<CalibrationDerivedEntry> working;
    std::vector<CalibrationDerivedEntry> derived;

    bool Matches(const std::string &device, const VnaCore::SweepPlan &plan) const noexcept;
    bool MatchesSets(const std::vector<CalibrationSetPtr> &sets, std::uint64_t selected) const noexcept;
};

// 每通道按需保留当前工作轴最多 512 MiB、旧轴最多 32 MiB，原始池受独立 1 GiB 限制。
// 不预分配或预读；事务共享不可变系数，源池与同轴引用不计为派生副本。
std::shared_ptr<const CalibrationPlanCache> PrepareCalibrationCache(const std::vector<CalibrationSetPtr> &sets,
    std::uint64_t selected, const std::string &identity, const VnaCore::SweepPlan &plan,
    const std::shared_ptr<const CalibrationPlanCache> &previous);
CalibrationApplicability UnpreparedCalibration(bool selected);
CalibrationSetSummary SummarizeCalibration(const CalibrationSetPtr &set);

}  // 命名空间 Detail
}  // 命名空间 VnaService
#endif
