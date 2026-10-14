/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cmath>

#include "scoped_simulator_environment.h"
#include "test_support.h"
#include "vna_board/board_api_contract.h"

namespace BoardTest {
namespace {

std::vector<double> AcquireAmplitude(VnaBoard::BoardSimulatorState &state, const char *amplitude)
{
    SetEnvironment("VNAPRO_SIM_NOISE_AMPLITUDE", amplitude);
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "噪声幅度初始化失败");
    Prepare(state, 3U, {0U, 1U});
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "噪声幅度扫描启动失败");
    return Read(state, 3U, 2U);
}

void TestAmplitudeScalingAndReplay()
{
    ScopedSimulatorEnvironment environment;
    VnaBoard::BoardSimulatorState state;
    const auto golden = AcquireAmplitude(state, "");
    SetEnvironment("VNAPRO_SIM_MODE", "noise");
    Require(AcquireAmplitude(state, "0") == golden, "零幅度噪声必须等于黄金数据");
    const auto normal = AcquireAmplitude(state, "");
    Require(normal != golden, "默认幅度必须保留原有扰动");
    Require(AcquireAmplitude(state, "0.002") == normal, "显式默认幅度改变了原有噪声");
    Require(AcquireAmplitude(state, "2e-3") == normal, "科学计数法幅度不能改变结果");
    const auto doubled = AcquireAmplitude(state, "0.004");
    for (std::size_t index = 0U; index < golden.size(); ++index) {
        Require(std::abs((doubled[index] - golden[index]) - 2.0 * (normal[index] - golden[index])) < 1e-14,
            "相同种子的 IQ 扰动必须随幅度线性缩放");
    }
    Require(AcquireAmplitude(state, "0.004") == doubled, "相同种子和幅度不能复现首帧");
    const auto maximum = AcquireAmplitude(state, "1");
    Require(maximum != doubled, "上界幅度没有影响数据");
    Require(AcquireAmplitude(state, "0e10") == golden, "真正的科学计数法零幅度不能拒绝");
    AcquireAmplitude(state, "5e-324");
}

void TestAmplitudeValidationAndFreeze()
{
    ScopedSimulatorEnvironment environment;
    SetEnvironment("VNAPRO_SIM_MODE", "noise");
    VnaBoard::BoardSimulatorState state;
    const auto first = AcquireAmplitude(state, "0.004");
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "第二帧参考扫描启动失败");
    const auto second = Read(state, 3U, 2U);
    Require(AcquireAmplitude(state, "0.004") == first, "冻结测试首帧复现失败");
    SetEnvironment("VNAPRO_SIM_NOISE_AMPLITUDE", "0");
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "环境修改后扫描启动失败");
    Require(Read(state, 3U, 2U) == second, "运行中修改环境不能改变已冻结幅度");
    const char *invalid[] = {"-0.001", "1.001", "nan", "inf", "1e309", " 0.002", "0.002 ", "0.002x", "0,002", "1e-400",
        "-1e-400", "1e-324", "-1e-324"};
    for (const char *value : invalid) {
        SetEnvironment("VNAPRO_SIM_NOISE_AMPLITUDE", value);
        Require(state.Initialize() == VnaBoard::DRV_SIM_INVALID_ARGUMENT, "非法噪声幅度未拒绝");
        Require(Read(state, 3U, 2U) == second, "非法初始化不能破坏已采集数据");
    }
    SetEnvironment("VNAPRO_SIM_MODE", "golden");
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "黄金模式必须忽略噪声专用环境参数");
    SetEnvironment("VNAPRO_SIM_MODE", "noise");
    SetEnvironment("VNAPRO_SIM_SEED", "");
    SetEnvironment("VNAPRO_SIM_NOISE_AMPLITUDE", "1.001");
    Require(state.Initialize() == VnaBoard::DRV_SIM_INVALID_ARGUMENT, "缺省种子不能跳过幅度校验");
}

}  // 匿名命名空间

void TestNoiseAmplitude()
{
    TestAmplitudeScalingAndReplay();
    TestAmplitudeValidationAndFreeze();
}

}  // 命名空间 BoardTest
