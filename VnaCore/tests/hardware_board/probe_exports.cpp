/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "probe_state.h"

// 导出声明独立复写供应头的九个签名，不链接任何模拟器代码或包含模拟错误枚举。
#if defined(_WIN32)
#define BOARD_PROBE_EXPORT __declspec(dllexport)
#else
#define BOARD_PROBE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" BOARD_PROBE_EXPORT std::uint32_t DrvHwInit()
{
    return HardwareBoardProbe::State().Initialize();
}

extern "C" BOARD_PROBE_EXPORT void DrvSetFreqList(std::uint64_t *values, std::uint32_t count)
{
    HardwareBoardProbe::State().SetList("FREQ", values, count);
}

extern "C" BOARD_PROBE_EXPORT void DrvSetPowerList(std::uint64_t *values, std::uint32_t count)
{
    HardwareBoardProbe::State().SetList("POWER", values, count);
}

extern "C" BOARD_PROBE_EXPORT void DrvSetIfbwList(std::uint64_t *values, std::uint32_t count)
{
    HardwareBoardProbe::State().SetList("IFBW", values, count);
}

extern "C" BOARD_PROBE_EXPORT void DrvSetWorkPorts(std::uint32_t *values, std::uint32_t count)
{
    HardwareBoardProbe::State().SetPorts(values, count);
}

extern "C" BOARD_PROBE_EXPORT std::uint32_t DrvDataFrameDownload()
{
    return HardwareBoardProbe::State().Download();
}

extern "C" BOARD_PROBE_EXPORT std::uint32_t DrvSetRfPowerOn(std::uint32_t enabled)
{
    return HardwareBoardProbe::State().Rf(enabled);
}

extern "C" BOARD_PROBE_EXPORT std::uint32_t DrvStartSweep()
{
    return HardwareBoardProbe::State().Start();
}

extern "C" BOARD_PROBE_EXPORT std::uint32_t DrvGetSweepData(
    std::uint32_t first, std::uint32_t count, double *data, int capacity)
{
    return HardwareBoardProbe::State().Get(first, count, data, capacity);
}
