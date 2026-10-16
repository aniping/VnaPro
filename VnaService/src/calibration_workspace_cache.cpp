/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_workspace_cache.h"

#include <algorithm>
#include <utility>

namespace VnaService {
namespace Detail {
namespace {
constexpr std::size_t MAX_DERIVED_BYTES = 32U * 1024U * 1024U;
constexpr std::size_t MAX_WORKING_BYTES = 512U * 1024U * 1024U;

std::size_t DerivedBytes(const VnaCalibration::CalibrationData &data)
{
    return sizeof(data) + sizeof(double) * data.frequencyHz.size() +
           sizeof(VnaCalibration::OnePortTerms) * data.onePortTerms.size() +
           sizeof(VnaCalibration::TwoPortTerms) * data.twoPortTerms.size() +
           sizeof(VnaCalibration::FourPortTerms) * data.fourPortTerms.size();
}

CalibrationSetSummary AssessedSummary(const CalibrationSetPtr &set, const CalibrationPlanCache &cache,
    const VnaCore::SweepPlan &plan, const std::shared_ptr<const CalibrationPlanCache> &previous)
{
    if (previous) {
        const auto found = std::find_if(
            previous->summaries.begin(), previous->summaries.end(), [&set](const CalibrationCachedSummary &item) {
                return item.set == set;
            });
        if (found != previous->summaries.end()) {
            auto summary = found->summary;
            summary.applicability.prepared = false;
            return summary;
        }
    }
    auto summary = SummarizeCalibration(set);
    summary.applicability = AssessCalibration(*set, cache.identity, plan);
    summary.compatible = summary.applicability.applicable;
    return summary;
}

void LimitEntries(std::vector<CalibrationDerivedEntry> &entries, std::size_t limit)
{
    std::size_t bytes = 0U;
    for (const auto &entry : entries) {
        bytes += entry.bytes;
    }
    while (bytes > limit && !entries.empty()) {
        bytes -= entries.front().bytes;
        entries.erase(entries.begin());
    }
}

bool ContainsSource(const CalibrationPlanCache &cache, const CalibrationSetPtr &source)
{
    return std::any_of(cache.summaries.begin(), cache.summaries.end(), [&source](const CalibrationCachedSummary &item) {
        return item.set == source;
    });
}

void RetainOldEntries(CalibrationPlanCache &cache, const std::vector<CalibrationDerivedEntry> &entries, bool sameAxis)
{
    for (const auto &entry : entries) {
        if (!ContainsSource(cache, entry.source)) {
            continue;
        }
        const bool target =
            !sameAxis && entry.correction->frequencyHz == cache.frequencyHz && *entry.powerDbm == cache.powerDbm;
        (target ? cache.working : cache.derived).push_back(entry);
    }
}

void RetainDerived(CalibrationPlanCache &cache, const std::shared_ptr<const CalibrationPlanCache> &previous)
{
    const bool sameAxis =
        previous && previous->frequencyHz == cache.frequencyHz && previous->powerDbm == cache.powerDbm;
    cache.powerKey = sameAxis ? previous->powerKey : std::make_shared<const std::vector<double>>(cache.powerDbm);
    if (!previous) {
        return;
    }
    RetainOldEntries(cache, previous->derived, sameAxis);
    for (const auto &entry : previous->working) {
        if (ContainsSource(cache, entry.source)) {
            (sameAxis ? cache.working : cache.derived).push_back(entry);
        }
    }
    LimitEntries(cache.derived, MAX_DERIVED_BYTES);
}

VnaCalibration::CalibrationPtr FindDerived(CalibrationPlanCache &cache, const CalibrationSetPtr &set)
{
    const auto found =
        std::find_if(cache.working.begin(), cache.working.end(), [&set](const CalibrationDerivedEntry &entry) {
            return entry.source == set;
        });
    if (found == cache.working.end()) {
        return {};
    }
    const auto entry = *found;
    cache.working.erase(found);
    cache.working.push_back(entry);
    return entry.correction;
}

void RememberDerived(CalibrationPlanCache &cache, const CalibrationSetPtr &set)
{
    const auto correction = cache.selected.correction;
    if (!correction || correction == set->data) {
        return;
    }
    // 功率键在条目间共享，但保守地逐条计费，保证实际驻留不会超过逻辑预算。
    CalibrationDerivedEntry entry{
        set, correction, cache.powerKey, DerivedBytes(*correction) + sizeof(double) * cache.powerDbm.size()};
    cache.working.push_back(std::move(entry));
    LimitEntries(cache.working, MAX_WORKING_BYTES);
}

void PrepareSelected(CalibrationPlanCache &cache, const VnaCore::SweepPlan &plan)
{
    const auto found =
        std::find_if(cache.summaries.begin(), cache.summaries.end(), [&cache](const CalibrationCachedSummary &item) {
            return item.set->id == cache.selectedSetId;
        });
    if (found == cache.summaries.end()) {
        cache.selected.applicability = UnpreparedCalibration(false);
        return;
    }
    cache.selected.applicability = found->summary.applicability;
    if (cache.selected.applicability.applicable) {
        cache.selected.correction = FindDerived(cache, found->set);
        if (cache.selected.correction) {
            cache.selected.applicability.prepared = true;
        } else {
            cache.selected = VnaService::PrepareCalibration(found->set, cache.identity, plan);
            RememberDerived(cache, found->set);
        }
    }
    found->summary.applicability = cache.selected.applicability;
    found->summary.compatible = cache.selected.applicability.applicable;
}
}  // 匿名命名空间

bool CalibrationPlanCache::Matches(const std::string &device, const VnaCore::SweepPlan &plan) const noexcept
{
    return identity == device && ifBandwidthRaw == plan.ifBandwidthRaw && frequencyHz == plan.frequencyHz &&
           powerDbm == plan.powerDbm;
}

bool CalibrationPlanCache::MatchesSets(
    const std::vector<CalibrationSetPtr> &sets, std::uint64_t selected) const noexcept
{
    return selected == selectedSetId && sets.size() == summaries.size() &&
           std::equal(sets.begin(), sets.end(), summaries.begin(),
               [](const CalibrationSetPtr &set, const CalibrationCachedSummary &item) {
                   return set == item.set;
               });
}

CalibrationApplicability UnpreparedCalibration(bool selected)
{
    CalibrationApplicability result;
    result.reasonCode = selected ? CalibrationApplicabilityReason::CONFIGURATION_NOT_READY
                                 : CalibrationApplicabilityReason::NO_SELECTION;
    result.reason = selected ? "Calibration has not been prepared for the committed acquisition plan"
                             : "No calibration set selected";
    return result;
}

CalibrationSetSummary SummarizeCalibration(const CalibrationSetPtr &set)
{
    CalibrationSetSummary result;
    result.id = set->id;
    result.name = set->name;
    result.method = set->data->method;
    result.port1 = set->data->port1;
    result.port2 = set->data->port2;
    result.includeIsolation = set->includeIsolation;
    result.startHz = set->data->frequencyHz.front();
    result.stopHz = set->data->frequencyHz.back();
    result.points = static_cast<unsigned>(set->data->frequencyHz.size());
    result.ifBandwidthHz = set->ifBandwidthHz;
    const auto powers = std::minmax_element(set->powerDbm.begin(), set->powerDbm.end());
    result.powerMinDbm = *powers.first;
    result.powerMaxDbm = *powers.second;
    result.deviceIdentity = set->deviceIdentity;
    return result;
}

std::shared_ptr<const CalibrationPlanCache> PrepareCalibrationCache(const std::vector<CalibrationSetPtr> &sets,
    std::uint64_t selected, const std::string &identity, const VnaCore::SweepPlan &plan,
    const std::shared_ptr<const CalibrationPlanCache> &previous)
{
    auto cache = std::make_shared<CalibrationPlanCache>();
    cache->identity = identity;
    cache->frequencyHz = plan.frequencyHz;
    cache->powerDbm = plan.powerDbm;
    cache->ifBandwidthRaw = plan.ifBandwidthRaw;
    cache->selectedSetId = selected;
    cache->summaries.reserve(sets.size());
    const auto reusable = previous && previous->Matches(identity, plan) ? previous : nullptr;
    for (const auto &set : sets) {
        cache->summaries.push_back({set, AssessedSummary(set, *cache, plan, reusable)});
    }
    RetainDerived(*cache, previous);
    PrepareSelected(*cache, plan);
    return cache;
}

}  // 命名空间 Detail
}  // 命名空间 VnaService
