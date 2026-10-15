/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <cmath>
#include <limits>

#include "test_support.h"
#include "vna_board/board_api_contract.h"

namespace BoardTest {
namespace {

void TestStagingAndGolden()
{
    VnaBoard::BoardSimulatorState state;
    Require(state.Start() == VnaBoard::DRV_SIM_NOT_INITIALIZED, "启动必须先初始化");
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "初始化失败");
    Prepare(state, 3U, {2U, 0U});
    std::vector<std::uint64_t> nextFrequencies(3U, 1100000000ULL);
    state.SetFrequencies(nextFrequencies.data(), 3U);
    nextFrequencies.assign(3U, 999U);
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "旧计划应能扫描");
    const auto previous = Read(state, 3U, 2U);
    Require(std::abs(previous[48U] - 0.7289686274214116) < 1e-14, "1GHz 源0入射实部基准不符");
    Require(std::abs(previous[49U] - 0.6845471059286886) < 1e-14, "1GHz 源0入射虚部基准不符");
    Require(std::abs(previous[50U] - 0.026319979048503892) < 1e-14, "1GHz 源0反射实部基准不符");
    Require(std::abs(previous[51U] + 0.20834408727604034) < 1e-14, "1GHz 源0反射虚部基准不符");
    Require(std::hypot(previous[8U], previous[9U]) > 0.999, "工作源端口排列被改变");
    Require(state.Download() == VnaBoard::DRV_SIM_OK, "暂存计划提交失败");
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "新计划扫描失败");
    const auto changed = Read(state, 3U, 2U);
    Require(previous != changed, "暂存频率未生效");
    Require(std::abs(changed[48U] - 0.6753328081210244) < 1e-14, "setter 必须复制输入数组");
    state.SetFrequencies(nullptr, 3U);
    Require(state.Download() == VnaBoard::DRV_SIM_INVALID_ARGUMENT, "非法 setter 不能沿用旧暂存项成功组帧");
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "失败组帧不能破坏旧计划");
    Require(Read(state, 3U, 2U) == changed, "失败组帧改变了旧计划");
}

void TestValidationAndBudget()
{
    VnaBoard::BoardSimulatorState state;
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "初始化失败");
    Prepare(state, 1U);
    const std::uint32_t duplicate[] = {0U, 0U};
    state.SetWorkPorts(duplicate, 2U);
    Require(state.Download() == VnaBoard::DRV_SIM_INVALID_ARGUMENT, "重复工作端口必须拒绝");
    const std::size_t tooMany = VnaBoard::MAX_SWEEP_BYTES / (4U * 16U * sizeof(double)) + 1U;
    const std::vector<std::uint64_t> frequencies(tooMany, 1000000000ULL);
    const std::vector<double> powers(tooMany, 17.0);
    const std::vector<std::uint64_t> bandwidths(tooMany, 1U);
    const std::uint32_t ports[] = {0U, 1U, 2U, 3U};
    state.SetFrequencies(frequencies.data(), static_cast<std::uint32_t>(tooMany));
    state.SetPowers(powers.data(), static_cast<std::uint32_t>(tooMany));
    state.SetBandwidths(bandwidths.data(), static_cast<std::uint32_t>(tooMany));
    state.SetWorkPorts(ports, 4U);
    Require(state.Download() == VnaBoard::DRV_SIM_SIZE_OVERFLOW, "扫描数据预算必须在分配前检查");
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "预算失败后旧计划应保留");
    const auto values = Read(state, 1U);
    std::vector<double> untouched(16U, -123.0);
    Require(state.Get(0U, 1U, untouched.data(), 15) == VnaBoard::DRV_SIM_BUFFER_TOO_SMALL, "容量不足必须拒绝");
    Require(state.Get(std::numeric_limits<std::uint32_t>::max(), 2U, untouched.data(), 16) ==
                VnaBoard::DRV_SIM_INVALID_ARGUMENT,
        "越界范围不能溢出后被接受");
    Require(std::all_of(untouched.begin(), untouched.end(),
                [](double value) {
                    return value == -123.0;
                }),
        "读取失败改变输出");
    Require(Read(state, 1U) == values, "读取必须支持重读");
}

void TestNoiseAndEnvironment()
{
    SetEnvironment("VNAPRO_SIM_MODE", "noise");
    SetEnvironment("VNAPRO_SIM_SEED", "42");
    VnaBoard::BoardSimulatorState state;
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "噪声初始化失败");
    Prepare(state, 2U);
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "噪声启动失败");
    const auto first = Read(state, 2U);
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "第二次噪声启动失败");
    Require(Read(state, 2U) != first, "噪声应随扫描更新");
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "重新初始化失败");
    Prepare(state, 2U);
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "种子复现启动失败");
    Require(Read(state, 2U) == first, "相同种子重新初始化应复现");
    SetEnvironment("VNAPRO_SIM_SEED", "42 ");
    Require(state.Initialize() == VnaBoard::DRV_SIM_INVALID_ARGUMENT, "噪声种子尾随空格必须拒绝");
    Require(Read(state, 2U) == first, "非法初始化不能破坏原状态");
    SetEnvironment("VNAPRO_SIM_MODE", "");
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "空模式应采用 golden 并忽略噪声种子");
    SetEnvironment("VNAPRO_SIM_MODE", "golden");
    SetEnvironment("VNAPRO_SIM_SEED", "42");
}

}  // 匿名命名空间

void TestState()
{
    TestStagingAndGolden();
    TestValidationAndBudget();
    TestNoiseAndEnvironment();
}

}  // 命名空间 BoardTest
