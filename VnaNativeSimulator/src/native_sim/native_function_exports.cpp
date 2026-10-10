#include <cstdint>
#include <type_traits>

#include "native_simulator_state.h"
#include "vna_native/native_api_contract.h"

// 九个编译期断言防止声明与动态加载器所用的函数指针类型分叉。
// 这里只比较本工程的类型；符号是否齐全还需由动态加载/导出符号检查验证。
static_assert(std::is_same<decltype(&DrvHwInit), vna_native::DrvHwInitFn>::value,
    "DrvHwInit signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvGetSweepData), vna_native::DrvGetSweepDataFn>::value,
    "DrvGetSweepData signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvStartSweep), vna_native::DrvStartSweepFn>::value,
    "DrvStartSweep signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvActivateRxLink), vna_native::DrvActivateRxLinkFn>::value,
    "DrvActivateRxLink signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvSetRfPowerOn), vna_native::DrvSetRfPowerOnFn>::value,
    "DrvSetRfPowerOn signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvSetFastSweepMode), vna_native::DrvSetFastSweepModeFn>::value,
    "DrvSetFastSweepMode signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvGetIDN), vna_native::DrvGetIDNFn>::value,
    "DrvGetIDN signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvCheckSweepPortPower), vna_native::DrvCheckSweepPortPowerFn>::value,
    "DrvCheckSweepPortPower signature does not match NativeApiContract");
static_assert(std::is_same<decltype(&DrvDataFrameDownload), vna_native::DrvDataFrameDownloadFn>::value,
    "DrvDataFrameDownload signature does not match NativeApiContract");

// 导出层只转发到进程内单例，状态校验、互斥和信号生成由 NativeSimulatorState 负责。
// 每个入口都在 try 内取得 Instance()，连首次构造单例的异常也转换为 INTERNAL_ERROR。
// C++ 异常不能越过 C ABI；以下入口沿用同一捕获规则。
extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit()
{
    try {
        return vna_native::NativeSimulatorState::Instance().HwInit();
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double* iqData)
{
    try {
        return vna_native::NativeSimulatorState::Instance().GetSweepData(pointCount, iqData);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(
    std::uint32_t pointCount, std::uint32_t sourcePort)
{
    try {
        return vna_native::NativeSimulatorState::Instance().StartSweep(pointCount, sourcePort);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap)
{
    try {
        return vna_native::NativeSimulatorState::Instance().ActivateRxLink(rxPortBitmap);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled)
{
    try {
        return vna_native::NativeSimulatorState::Instance().SetRfPowerOn(enabled);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode)
{
    try {
        return vna_native::NativeSimulatorState::Instance().SetFastSweepMode(mode);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char* buffer, int capacity)
{
    try {
        return vna_native::NativeSimulatorState::Instance().GetIDN(buffer, capacity);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap, std::uint16_t* overFreqIndex, std::uint32_t indexCapacity)
{
    try {
        return vna_native::NativeSimulatorState::Instance().CheckSweepPortPower(
            rfPortBitmap, checkBitmap, overFreqIndex, indexCapacity);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount,
    std::uint32_t* paramData, std::uint32_t paramDataLen, double* tableData, std::uint32_t tableDataLen)
{
    try {
        return vna_native::NativeSimulatorState::Instance().DataFrameDownload(
            pointCount, paramData, paramDataLen, tableData, tableDataLen);
    } catch (...) {
        return vna_native::DRV_SIM_INTERNAL_ERROR;
    }
}
