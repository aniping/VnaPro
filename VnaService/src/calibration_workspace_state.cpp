/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <utility>

#include "calibration_workspace_cache.h"
#include "vna_service/calibration_workspace.h"

namespace VnaService {
namespace {
void ApplyPreparedSnapshot(
    CalibrationSnapshot &result, const std::shared_ptr<const Detail::CalibrationPlanCache> &cache, bool current)
{
    result.applicability =
        current ? cache->selected.applicability : Detail::UnpreparedCalibration(result.selectedSetId != 0U);
    result.compatible = result.applicability.applicable && result.applicability.prepared;
    result.effective = current && result.enabled && result.compatible && cache->selected.correction != nullptr;
    result.reason = !result.compatible ? result.applicability.reason
                    : !result.enabled  ? "Correction is disabled"
                                       : "Correction is enabled";
}

std::vector<CalibrationSetSummary> CachedSummaries(
    const std::shared_ptr<const Detail::CalibrationPlanCache> &cache, bool current)
{
    std::vector<CalibrationSetSummary> result;
    if (!cache) {
        return result;
    }
    result.reserve(cache->summaries.size());
    for (const auto &item : cache->summaries) {
        auto summary = item.summary;
        if (!current) {
            summary.compatible = false;
            summary.applicability = Detail::UnpreparedCalibration(true);
        }
        result.push_back(std::move(summary));
    }
    return result;
}
}  // 匿名命名空间

void CalibrationWorkspace::PrepareForPlan(const std::string &identity, const VnaCore::SweepPlan &plan)
{
    if (!planCache_ || !planCache_->Matches(identity, plan) || !planCache_->MatchesSets(sets_, selectedSetId_)) {
        planCache_ = Detail::PrepareCalibrationCache(sets_, selectedSetId_, identity, plan, planCache_);
    }
}

void CalibrationWorkspace::RefreshPrepared()
{
    VnaCore::SweepPlan plan;
    if (planCache_) {
        plan.frequencyHz = planCache_->frequencyHz;
        plan.powerDbm = planCache_->powerDbm;
        plan.ifBandwidthRaw = planCache_->ifBandwidthRaw;
    }
    PrepareForPlan(planCache_ ? planCache_->identity : std::string(), plan);
}

CalibrationSnapshot CalibrationWorkspace::CollectionSnapshot() const
{
    CalibrationSnapshot result;
    result.revision = revision_;
    result.collecting = collecting_;
    result.capturePending = capturePending_;
    result.sessionId = collecting_ ? session_.id : 0U;
    result.method = method_;
    result.port1 = port1_;
    result.port2 = port2_;
    result.includeIsolation = includeIsolation_;
    for (const auto &sample : samples_) {
        result.steps.push_back(sample.step);
    }
    return result;
}

CalibrationSnapshot CalibrationWorkspace::Snapshot(const std::string &identity, const VnaCore::SweepPlan &plan) const
{
    auto result = CollectionSnapshot();
    result.selectedSetId = selectedSetId_;
    result.enabled = enabled_;
    result.error = error_;
    const auto selected = Find(selectedSetId_);
    if (!collecting_ && selected) {
        result.method = selected->data->method;
        result.port1 = selected->data->port1;
        result.port2 = selected->data->port2;
        result.includeIsolation = selected->includeIsolation;
    }
    const bool current = planCache_ && planCache_->Matches(identity, plan);
    ApplyPreparedSnapshot(result, planCache_, current);
    result.sets = CachedSummaries(planCache_, current);
    return result;
}

VnaCalibration::CalibrationPtr CalibrationWorkspace::Correction(
    const std::string &identity, const VnaCore::SweepPlan &plan) const
{
    return enabled_ && planCache_ && planCache_->Matches(identity, plan) &&
                   planCache_->selected.applicability.applicable && planCache_->selected.applicability.prepared
               ? planCache_->selected.correction
               : VnaCalibration::CalibrationPtr{};
}

}  // 命名空间 VnaService
