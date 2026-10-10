/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cstdint>
#include <type_traits>

#include "native_simulator_state.h"
#include "vna_native/native_api_contract.h"

static_assert(std::is_same<decltype(&DrvHwInit), VnaNative::DrvHwInitFn>::value,
    "DrvHwInit signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvGetSweepData), VnaNative::DrvGetSweepDataFn>::value,
    "DrvGetSweepData signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvStartSweep), VnaNative::DrvStartSweepFn>::value,
    "DrvStartSweep signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvActivateRxLink), VnaNative::DrvActivateRxLinkFn>::value,
    "DrvActivateRxLink signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvSetRfPowerOn), VnaNative::DrvSetRfPowerOnFn>::value,
    "DrvSetRfPowerOn signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvSetFastSweepMode), VnaNative::DrvSetFastSweepModeFn>::value,
    "DrvSetFastSweepMode signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvGetIDN), VnaNative::DrvGetIDNFn>::value,
    "DrvGetIDN signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvCheckSweepPortPower), VnaNative::DrvCheckSweepPortPowerFn>::value,
    "DrvCheckSweepPortPower signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvDataFrameDownload), VnaNative::DrvDataFrameDownloadFn>::value,
    "DrvDataFrameDownload signature does not match NativeApiContract");

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit()
{
    try {
        return VnaNative::NativeSimulatorState::Instance().HwInit();
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double *iqData)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().GetSweepData(pointCount, iqData);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(
    std::uint32_t pointCount, std::uint32_t sourcePort)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().StartSweep(pointCount, sourcePort);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().ActivateRxLink(rxPortBitmap);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().SetRfPowerOn(enabled);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().SetFastSweepMode(mode);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char *buffer, int capacity)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().GetIDN(buffer, capacity);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap, std::uint16_t *overFreqIndex, std::uint32_t indexCapacity)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().CheckSweepPortPower(
            rfPortBitmap, checkBitmap, overFreqIndex, indexCapacity);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount,
    std::uint32_t *paramData, std::uint32_t paramDataLen, double *tableData, std::uint32_t tableDataLen)
{
    try {
        return VnaNative::NativeSimulatorState::Instance().DataFrameDownload(
            pointCount, paramData, paramDataLen, tableData, tableDataLen);
    } catch (...) {
        // 转换单例构造失败；C++ 异常不得越过已固定的 C ABI 边界。
        return VnaNative::DRV_SIM_INTERNAL_ERROR;
    }
}
