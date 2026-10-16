/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "../src/calibration_workspace_cache.h"
#include "calibration_plan_test_support.h"

namespace {
using namespace CalibrationPlanTest;
const std::string IDENTITY = "VnaPro,NativeSimulator,4PORT,1.0";

bool SameBits(double left, double right)
{
    std::uint64_t first = 0U;
    std::uint64_t second = 0U;
    std::memcpy(&first, &left, sizeof(first));
    std::memcpy(&second, &right, sizeof(second));
    return first == second;
}

void RequireSubsetTerms(
    const VnaCalibration::CalibrationData &correction, const VnaCalibration::CalibrationData &source)
{
    for (unsigned point = 0U; point < 3U; ++point) {
        for (unsigned cell = 0U; cell < 16U; ++cell) {
            const auto &actual = correction.fourPortTerms[point][cell];
            const auto &expected = source.fourPortTerms[point * 2U][cell];
            Require(SameBits(actual.offset.real(), expected.offset.real()) &&
                        SameBits(actual.offset.imag(), expected.offset.imag()) && actual.match == expected.match &&
                        actual.tracking == expected.tracking,
                "exact subset terms must preserve source values including signed zero");
        }
    }
}

void ExactAxisAndSubsetPreserveOriginalData()
{
    VnaService::CalibrationWorkspace workspace;
    Import(workspace, Source(IDENTITY));
    Activate(workspace);
    const auto source = workspace.Find(1U);
    const auto full = Plan(source->data->frequencyHz);
    Require(!workspace.Snapshot(IDENTITY, full).compatible && !workspace.Correction(IDENTITY, full),
        "read-only queries must not implicitly prepare a new plan");
    workspace.PrepareForPlan(IDENTITY, full);
    Require(workspace.Correction(IDENTITY, full) == source->data,
        "the identical axis must reuse the frozen original coefficient pointer");
    const auto subset = Plan({10000000.0, 10002000.0, 10004000.0});
    workspace.PrepareForPlan(IDENTITY, subset);
    const auto correction = workspace.Correction(IDENTITY, subset);
    Require(correction && correction != source->data && correction->frequencyHz == subset.frequencyHz &&
                workspace.Snapshot(IDENTITY, subset).applicability.frequencyMode ==
                    VnaService::CalibrationFrequencyMode::SUBSET,
        "an actual source-axis subset must gather coefficients without interpolation");
    RequireSubsetTerms(*correction, *source->data);
    for (unsigned read = 0U; read < 1000U; ++read) {
        Require(workspace.Snapshot(IDENTITY, subset).effective && workspace.Correction(IDENTITY, subset) == correction,
            "metadata and correction reads must retain the prepared pointer");
    }
    Require(workspace.Find(1U) == source && workspace.SavedSets().front()->data == source->data,
        "derived data must never replace the persisted immutable original");
}

void ConditionsAndCacheLifecycleAreIndependent()
{
    VnaService::CalibrationWorkspace workspace;
    Import(workspace, Source(IDENTITY));
    Activate(workspace);
    auto plan = Plan({10000000.0, 10001500.0, 10004000.0});
    workspace.PrepareForPlan(IDENTITY, plan);
    const auto derived = workspace.Correction(IDENTITY, plan);
    const auto initial = workspace.Snapshot(IDENTITY, plan);
    Require(derived && initial.applicability.interpolatedPoints == 1U && initial.applicability.exactPoints == 2U,
        "mixed exact and new target points must prepare one immutable correction");
    plan.ifBandwidthRaw = 10000U;
    workspace.PrepareForPlan(IDENTITY, plan);
    const auto warning = workspace.Snapshot(IDENTITY, plan);
    Require(workspace.Correction(IDENTITY, plan) == derived && warning.effective &&
                std::find(warning.applicability.warnings.begin(), warning.applicability.warnings.end(),
                    VnaService::CalibrationWarning::IF_BANDWIDTH_CHANGED) != warning.applicability.warnings.end(),
        "IFBW only changes a warning and must reuse prepared coefficients");
    auto mismatch = plan;
    mismatch.powerDbm[1U] = -1.0;
    Require(!workspace.Snapshot(IDENTITY, mismatch).effective && !workspace.Correction(IDENTITY, mismatch) &&
                workspace.Correction(IDENTITY, plan) == derived,
        "a stale read must not mutate or build a replacement cache");
    workspace.PrepareForPlan(IDENTITY, mismatch);
    Require(workspace.Snapshot(IDENTITY, mismatch).enabled && !workspace.Correction(IDENTITY, mismatch) &&
                workspace.Snapshot(IDENTITY, mismatch).applicability.reasonCode ==
                    VnaService::CalibrationApplicabilityReason::POWER_MISMATCH,
        "bad stimulus must preserve enable intent with a precise blocked reason");
    workspace.PrepareForPlan(IDENTITY, plan);
    Require(workspace.Correction(IDENTITY, plan) == derived,
        "returning to the same target axis must reuse the bounded derived cache");
}

void TransactionCandidatesAndDerivedBudget()
{
    VnaService::CalibrationWorkspace workspace;
    Import(workspace, Source(IDENTITY, 10001U));
    Activate(workspace);
    const auto source = workspace.Find(1U);
    auto full = Plan(source->data->frequencyHz);
    workspace.PrepareForPlan(IDENTITY, full);
    auto candidate = workspace;
    const auto outside = Plan({10000000.0, 30000000.0});
    candidate.PrepareForPlan(IDENTITY, outside);
    Require(!candidate.Correction(IDENTITY, outside) && workspace.Correction(IDENTITY, full) == source->data,
        "failed candidate preparation cannot publish over the committed correction");
    std::shared_ptr<const VnaService::Detail::CalibrationPlanCache> cache;
    VnaCalibration::CalibrationPtr oldest;
    for (unsigned start = 0U; start < 8U; ++start) {
        const auto begin = source->data->frequencyHz.begin() + start;
        const auto target = Plan(std::vector<double>(begin, begin + 6700U));
        cache = VnaService::Detail::PrepareCalibrationCache(workspace.SavedSets(), 1U, IDENTITY, target, cache);
        if (start == 0U) {
            oldest = cache->selected.correction;
        }
        std::size_t bytes = 0U;
        for (const auto &entry : cache->derived) {
            bytes += entry.bytes;
        }
        Require(bytes <= 32U * 1024U * 1024U && cache->selected.correction && cache->derived.size() <= 6U,
            "old target axes must retain their separate 32 MiB byte budget");
    }
    Require(cache->derived.size() < 8U, "least recently used derived coefficients must be evicted");
    const auto first =
        Plan(std::vector<double>(source->data->frequencyHz.begin(), source->data->frequencyHz.begin() + 6700U));
    cache = VnaService::Detail::PrepareCalibrationCache(workspace.SavedSets(), 1U, IDENTITY, first, cache);
    Require(cache->selected.correction != oldest, "the oldest evicted target axis must be rebuilt from the original");
}

void CurrentWorkingSetExceedsOldAxisBudgetWithoutThrashing()
{
    VnaService::CalibrationWorkspace workspace;
    for (unsigned set = 0U; set < 8U; ++set) {
        Import(workspace, Source(IDENTITY, 6701U));
    }
    const auto source = workspace.Find(1U);
    auto plan = Plan(std::vector<double>(source->data->frequencyHz.begin(), source->data->frequencyHz.begin() + 6700U));
    std::shared_ptr<const VnaService::Detail::CalibrationPlanCache> cache;
    std::vector<VnaCalibration::CalibrationPtr> first;
    for (std::uint64_t id = 1U; id <= 8U; ++id) {
        cache = VnaService::Detail::PrepareCalibrationCache(workspace.SavedSets(), id, IDENTITY, plan, cache);
        first.push_back(cache->selected.correction);
    }
    std::size_t bytes = 0U;
    for (const auto &entry : cache->working) {
        bytes += entry.bytes;
    }
    Require(bytes > 32U * 1024U * 1024U && bytes <= VnaService::MAX_CALIBRATION_STORAGE_BYTES &&
                cache->working.size() == 8U && cache->derived.empty(),
        "the complete current working set must stay resident beyond the old-axis LRU limit");
    for (std::uint64_t id = 1U; id <= 8U; ++id) {
        cache = VnaService::Detail::PrepareCalibrationCache(workspace.SavedSets(), id, IDENTITY, plan, cache);
        Require(cache->selected.correction == first[id - 1U],
            "a second full pool cycle must not interpolate or gather coefficients again");
    }
    plan.ifBandwidthRaw = 10000U;
    cache = VnaService::Detail::PrepareCalibrationCache(workspace.SavedSets(), 8U, IDENTITY, plan, cache);
    Require(cache->working.size() == 8U && cache->selected.correction == first.back(),
        "IFBW must not replace or demote the current frequency/power working set");
}

VnaService::CalibrationSetPtr OverflowingMagnitudeSource()
{
    auto set = std::make_shared<VnaService::CalibrationSet>(*Source(IDENTITY, 2U));
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = {10000000.0, 10002000.0};
    VnaCalibration::OnePortTerms terms;
    terms.directivity = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    terms.reflectionTracking = {1.0, 0.0};
    data->onePortTerms.push_back(terms);
    terms.directivity = {0.1, 0.0};
    data->onePortTerms.push_back(terms);
    set->data = data;
    return set;
}

void InvalidDerivedDoesNotPoisonExactOrAnotherSet()
{
    VnaService::CalibrationWorkspace workspace;
    Import(workspace, OverflowingMagnitudeSource());
    Import(workspace, Source(IDENTITY, 3U));
    const auto original = workspace.Find(1U);
    const auto target = Plan({10000000.0, 10001000.0, 10002000.0});
    workspace.PrepareForPlan(IDENTITY, target);
    Activate(workspace, 1U);
    Require(
        !workspace.Correction(IDENTITY, target) && workspace.Snapshot(IDENTITY, target).applicability.reasonCode ==
                                                       VnaService::CalibrationApplicabilityReason::INVALID_COEFFICIENTS,
        "a numerically invalid Polar result must remain blocked");
    const auto exact = Plan(original->data->frequencyHz);
    workspace.PrepareForPlan(IDENTITY, exact);
    Require(workspace.Correction(IDENTITY, exact) == original->data,
        "returning to the validated original axis must bypass the failed derived result");
    workspace.PrepareForPlan(IDENTITY, target);
    Activate(workspace, 2U);
    Require(workspace.Snapshot(IDENTITY, target).effective &&
                workspace.Correction(IDENTITY, target) == workspace.Find(2U)->data,
        "a failed derived set must not prevent another immutable set becoming effective");
}
}  // 匿名命名空间

int main()
{
    try {
        ExactAxisAndSubsetPreserveOriginalData();
        ConditionsAndCacheLifecycleAreIndependent();
        TransactionCandidatesAndDerivedBudget();
        CurrentWorkingSetExceedsOldAxisBudgetWithoutThrashing();
        InvalidDerivedDoesNotPoisonExactOrAnotherSet();
        std::cout << "calibration plan cache tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
