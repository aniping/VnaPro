/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "test_support.h"

#include "vna_board/board_api_contract.h"

namespace BoardTest {

void Prepare(VnaBoard::BoardSimulatorState &state, std::size_t points, const std::vector<std::uint32_t> &ports,
    std::uint64_t firstFrequency)
{
    std::vector<std::uint64_t> frequencies(points);
    std::vector<double> powers(points, 0.0);
    std::vector<std::uint32_t> bandwidths(points, 1000U);
    for (std::size_t point = 0U; point < points; ++point) {
        frequencies[point] = firstFrequency + point * 1000000ULL;
    }
    state.SetFrequencies(frequencies.data(), static_cast<std::uint32_t>(points));
    state.SetPowers(powers.data(), static_cast<std::uint32_t>(points));
    state.SetBandwidths(bandwidths.data(), static_cast<std::uint32_t>(points));
    state.SetWorkPorts(ports.data(), static_cast<std::uint32_t>(ports.size()));
    Require(state.Download() == VnaBoard::DRV_SIM_OK, "组帧失败");
    Require(state.SetRfPower(1U) == VnaBoard::DRV_SIM_OK, "射频打开失败");
}

std::vector<double> Read(VnaBoard::BoardSimulatorState &state, std::uint32_t points, std::size_t sourceCount)
{
    std::vector<double> values(static_cast<std::size_t>(points) * sourceCount * 16U, -999.0);
    Require(state.Get(0U, points, values.data(), static_cast<int>(values.size())) == VnaBoard::DRV_SIM_OK, "读取失败");
    return values;
}

}  // 命名空间 BoardTest
