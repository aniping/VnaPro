/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "sweep_plan_validation.h"

#include <cmath>
#include <initializer_list>
#include <limits>

#include "vna_core/frame_layout.h"

namespace VnaCore {
namespace Detail {
namespace {

constexpr std::size_t FRAME_BUDGET = 64U * 1024U * 1024U;

std::string ValidatePower(const SweepPlan &plan)
{
    if (!plan.powerDbm.empty() && plan.powerDbm.size() != plan.frequencyRaw.size()) {
        return "dBm power list length differs from raw frequency axis";
    }
    for (const auto power : plan.powerDbm) {
        if (!std::isfinite(power) || power < MIN_POWER_DBM || power > MAX_POWER_DBM) {
            return "power must be finite dBm within -120..100";
        }
    }
    return std::string();
}

std::string ValidateSweepTime(const SweepPlan &plan)
{
    if (!std::isfinite(plan.sweepTimeSeconds) || plan.sweepTimeSeconds <= 0.0 ||
        plan.sweepTimeSeconds > MAX_SWEEP_TIME_SECONDS) {
        return "sweep time must be finite seconds within (0,100000]";
    }
    return {};
}

std::string ValidateFrequency(const SweepPlan &plan)
{
    const std::size_t count = plan.frequencyRaw.size();
    if (count == 0U || count > (std::numeric_limits<std::uint32_t>::max() - 1U) / TABLE_VALUES_PER_POINT) {
        return "frequency count is empty or exceeds native lengths";
    }
    if (!plan.frequencyHz.empty() && plan.frequencyHz.size() != count) {
        return "Hz axis length differs from raw axis";
    }
    for (const double value : plan.frequencyHz) {
        if (!std::isfinite(value)) {
            return "Hz axis contains a non-finite value";
        }
    }
    return std::string();
}

std::string ValidatePorts(const SweepPlan &plan)
{
    if (plan.receiverMask == 0U || (plan.receiverMask & ~ALL_RECEIVERS_MASK) != 0U) {
        return "receiver mask must select ports 1..4";
    }
    if (plan.sourcePorts.empty() || plan.sourcePorts.size() > PHYSICAL_PORT_COUNT) {
        return "source ports must be a nonempty set of ports 1..4";
    }
    std::uint32_t sources = 0U;
    for (const auto source : plan.sourcePorts) {
        if (source < 1U || source > PHYSICAL_PORT_COUNT || (sources & (1U << (source - 1U))) != 0U) {
            return "source port is out of range or duplicated";
        }
        sources |= 1U << (source - 1U);
    }
    return std::string();
}

}  // 匿名命名空间

std::string ValidateSweepPlan(const SweepPlan &plan)
{
    for (const auto validate : {ValidateFrequency, ValidatePorts, ValidatePower, ValidateSweepTime}) {
        const auto error = validate(plan);
        if (!error.empty()) {
            return error;
        }
    }
    const auto count = plan.frequencyRaw.size();
    if (count > FRAME_BUDGET / (IQ_VALUES_PER_POINT * sizeof(double) * plan.sourcePorts.size())) {
        return "IQ frame exceeds the core software memory budget";
    }
    if (!plan.tableData.empty() && plan.tableData.size() != count * TABLE_VALUES_PER_POINT) {
        return "opaque table must contain exactly 9N values";
    }
    return std::string();
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
