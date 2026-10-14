/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <iostream>
#include <type_traits>

#include "scoped_simulator_environment.h"
#include "test_support.h"
#include "vna_board/board_api_exports.h"

static_assert(std::is_same<decltype(&DrvHwInit), VnaBoard::DrvHwInitFn>::value, "初始化 ABI 不符");
static_assert(std::is_same<decltype(&DrvSetFreqList), VnaBoard::DrvSetFreqListFn>::value, "频点 ABI 不符");
static_assert(std::is_same<decltype(&DrvSetPowerList), VnaBoard::DrvSetPowerListFn>::value, "功率 ABI 不符");
static_assert(std::is_same<decltype(&DrvSetIfbwList), VnaBoard::DrvSetIfbwListFn>::value, "中频 ABI 不符");
static_assert(std::is_same<decltype(&DrvSetWorkPorts), VnaBoard::DrvSetWorkPortsFn>::value, "端口 ABI 不符");
static_assert(std::is_same<decltype(&DrvDataFrameDownload), VnaBoard::DrvDataFrameDownloadFn>::value, "组帧 ABI 不符");
static_assert(std::is_same<decltype(&DrvSetRfPowerOn), VnaBoard::DrvSetRfPowerOnFn>::value, "射频 ABI 不符");
static_assert(std::is_same<decltype(&DrvStartSweep), VnaBoard::DrvStartSweepFn>::value, "启动 ABI 不符");
static_assert(std::is_same<decltype(&DrvGetSweepData), VnaBoard::DrvGetSweepDataFn>::value, "读取 ABI 不符");

int main()
{
    try {
        BoardTest::ScopedSimulatorEnvironment environment;
        BoardTest::SetEnvironment("VNAPRO_SIM_MODE", "golden");
        BoardTest::SetEnvironment("VNAPRO_SIM_SEED", "42");
        BoardTest::TestState();
        BoardTest::TestNoiseAmplitude();
        BoardTest::TestAsync();
        std::cout << "TDC1VN10 simulator unit tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
