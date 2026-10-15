/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <iostream>
#include <stdexcept>

#include "test_support.h"

int main(int argc, char **argv)
{
    try {
        HardwareBoardTest::Require(argc == 3, "usage: hardware board test <independent ABI probe> <trace prefix>");
        const std::string library = argv[1];
        const std::string trace = argv[2];
        HardwareBoardTest::VerifyProfile(library, trace + "-two", {4U, 2U});
        HardwareBoardTest::VerifyProfile(library, trace + "-four", {3U, 1U, 4U, 2U});
        HardwareBoardTest::VerifyPowerDbmContract(library, trace + "-power-dbm");
        HardwareBoardTest::VerifyCloseWaits(library, trace + "-blocked-close");
        HardwareBoardTest::VerifyContinuousFrames(library, trace + "-continuous");
        HardwareBoardTest::VerifyIdleRecovery(library, trace + "-idle-recovery");
        HardwareBoardTest::VerifyFaults(library, trace + "-failure");
        HardwareBoardTest::VerifyUnknownStatus(library, trace + "-unknown-status");
        // 此项关闭射频失败，最后执行以便进程退出时也能观测未释放的驱动状态。
        HardwareBoardTest::VerifyFailedRfOff(library, trace + "-failed-close");
        std::cout << "TDC1VN10 独立硬件 ABI 全流程通过：初始化、列表生命周期、多源分段、连续完整帧、"
                     "关闭屏障、故障传播及关闭后保留无释放接口的库\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
