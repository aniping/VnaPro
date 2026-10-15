/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "board_sweep_validation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "vna_board/board_api_contract.h"

namespace VnaBoard {
namespace {

constexpr double MIN_POWER_DBM = -120.0;
constexpr double MAX_POWER_DBM = 100.0;

bool ListsValid(const SweepPlan &plan)
{
    const auto points = plan.frequencies.size();
    return points != 0U && plan.powers.size() == points && plan.bandwidths.size() == points &&
           std::find(plan.frequencies.begin(), plan.frequencies.end(), 0U) == plan.frequencies.end() &&
           std::find(plan.bandwidths.begin(), plan.bandwidths.end(), 0U) == plan.bandwidths.end();
}

bool PowerValid(double power) noexcept
{
    return std::isfinite(power) && power >= MIN_POWER_DBM && power <= MAX_POWER_DBM;
}

bool PortsValid(const std::vector<std::uint32_t> &ports)
{
    if (ports.empty() || ports.size() > BOARD_PORT_COUNT) {
        return false;
    }
    std::uint32_t seen = 0U;
    for (const auto port : ports) {
        if (port >= BOARD_PORT_COUNT || (seen & (1U << port)) != 0U) {
            return false;
        }
        seen |= 1U << port;
    }
    return true;
}

}  // 匿名命名空间

std::uint32_t ValidatePlan(SweepPlan &plan)
{
    if (!ListsValid(plan) || !PortsValid(plan.workPorts) ||
        !std::all_of(plan.powers.begin(), plan.powers.end(), PowerValid)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    const auto valuesPerPoint = plan.workPorts.size() * IQ_VALUES_PER_SOURCE_POINT;
    const auto maxValues =
        std::min(MAX_SWEEP_BYTES / sizeof(double), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    if (plan.frequencies.size() > maxValues / valuesPerPoint) {
        return DRV_SIM_SIZE_OVERFLOW;
    }
    plan.valuesPerPoint = valuesPerPoint;
    plan.totalValues = plan.frequencies.size() * valuesPerPoint;
    return DRV_SIM_OK;
}

}  // 命名空间 VnaBoard
