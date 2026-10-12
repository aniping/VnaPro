/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "board_simulator_state.h"
#include "vna_board/board_api_exports.h"

namespace {

VnaBoard::BoardSimulatorState &State()
{
    static VnaBoard::BoardSimulatorState state;
    return state;
}

template <typename Action>
std::uint32_t Protect(Action action) noexcept
{
    try {
        return action();
    } catch (...) {
        return VnaBoard::DRV_SIM_INTERNAL_ERROR;
    }
}

}  // 匿名命名空间

extern "C" {

std::uint32_t VNA_BOARD_CALL DrvHwInit()
{
    return Protect([] {
        return State().Initialize();
    });
}

void VNA_BOARD_CALL DrvSetFreqList(std::uint64_t freqList[], std::uint32_t length)
{
    Protect([freqList, length] {
        State().SetFrequencies(freqList, length);
        return VnaBoard::DRV_SIM_OK;
    });
}

void VNA_BOARD_CALL DrvSetPowerList(std::uint64_t powerList[], std::uint32_t length)
{
    Protect([powerList, length] {
        State().SetPowers(powerList, length);
        return VnaBoard::DRV_SIM_OK;
    });
}

void VNA_BOARD_CALL DrvSetIfbwList(std::uint64_t ifbwList[], std::uint32_t length)
{
    Protect([ifbwList, length] {
        State().SetBandwidths(ifbwList, length);
        return VnaBoard::DRV_SIM_OK;
    });
}

void VNA_BOARD_CALL DrvSetWorkPorts(std::uint32_t ports[], std::uint32_t number)
{
    Protect([ports, number] {
        State().SetWorkPorts(ports, number);
        return VnaBoard::DRV_SIM_OK;
    });
}

std::uint32_t VNA_BOARD_CALL DrvDataFrameDownload()
{
    return Protect([] {
        return State().Download();
    });
}

std::uint32_t VNA_BOARD_CALL DrvSetRfPowerOn(std::uint32_t isOn)
{
    return Protect([isOn] {
        return State().SetRfPower(isOn);
    });
}

std::uint32_t VNA_BOARD_CALL DrvStartSweep()
{
    return Protect([] {
        return State().Start();
    });
}

std::uint32_t VNA_BOARD_CALL DrvGetSweepData(std::uint32_t start_index, std::uint32_t fNum, double data[], int dataLen)
{
    return Protect([start_index, fNum, data, dataLen] {
        return State().Get(start_index, fNum, data, dataLen);
    });
}

}  // C 链接接口
