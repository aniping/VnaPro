/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
#define VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H

#include <cstdint>

// 加载器与模拟器共享的 ABI 声明九个签名保持不变的 C 链接函数。
// CALL 不额外指定调用约定；真实厂商的 Windows ABI 需另行验证。
#if defined(_WIN32)
#if defined(VNA_NATIVE_BUILD_SHARED)
#define VNA_NATIVE_EXPORT __declspec(dllexport)
#else
#define VNA_NATIVE_EXPORT
#endif
#define VNA_NATIVE_HIDDEN
#define VNA_NATIVE_CALL
#elif defined(__GNUC__) || defined(__clang__)
#define VNA_NATIVE_EXPORT __attribute__((visibility("default")))
#define VNA_NATIVE_HIDDEN __attribute__((visibility("hidden")))
#define VNA_NATIVE_CALL
#else
#define VNA_NATIVE_EXPORT
#define VNA_NATIVE_HIDDEN
#define VNA_NATIVE_CALL
#endif

namespace VnaNative {

// 这些状态值定义模拟器行为，不代表未公开的厂商驱动契约。
enum DrvSimErrorCode : std::uint32_t {
    DRV_SIM_OK = 0U,
    DRV_SIM_INVALID_ARGUMENT = 1U,
    DRV_SIM_NOT_INITIALIZED = 2U,
    DRV_SIM_RF_DISABLED = 3U,
    DRV_SIM_INVALID_STATE = 4U,
    DRV_SIM_NOT_SUPPORTED = 5U,
    DRV_SIM_SIZE_OVERFLOW = 6U,
    DRV_SIM_BUFFER_TOO_SMALL = 7U,
    DRV_SIM_INTERNAL_ERROR = 8U
};

// 端口编号为 0～3；所有 ABI 长度均以元素计数，不以字节计数。
enum NativeFrameLayout : std::uint32_t {
    NATIVE_PORT_COUNT = 4U,
    PARAM_VALUES_PER_POINT = 6U,
    TABLE_VALUES_PER_POINT = 9U,
    IQ_VALUES_PER_POINT = 16U
};

constexpr const char g_drvHwInitSymbol[] = "DrvHwInit";
constexpr const char g_drvGetSweepDataSymbol[] = "DrvGetSweepData";
constexpr const char g_drvStartSweepSymbol[] = "DrvStartSweep";
constexpr const char g_drvActivateRxLinkSymbol[] = "DrvActivateRxLink";
constexpr const char g_drvSetRfPowerOnSymbol[] = "DrvSetRfPowerOn";
constexpr const char g_drvSetFastSweepModeSymbol[] = "DrvSetFastSweepMode";
constexpr const char g_drvGetIdnSymbol[] = "DrvGetIDN";
constexpr const char g_drvCheckSweepPortPowerSymbol[] = "DrvCheckSweepPortPower";
constexpr const char g_drvDataFrameDownloadSymbol[] = "DrvDataFrameDownload";

// 使用明确的函数指针类型别名，保持已固定的动态加载 ABI 签名。
using DrvHwInitFn = std::uint32_t(VNA_NATIVE_CALL *)();
using DrvGetSweepDataFn = std::uint32_t(VNA_NATIVE_CALL *)(std::uint32_t, double *);
using DrvStartSweepFn = std::uint32_t(VNA_NATIVE_CALL *)(std::uint32_t, std::uint32_t);
using DrvActivateRxLinkFn = std::uint32_t(VNA_NATIVE_CALL *)(std::uint32_t);
using DrvSetRfPowerOnFn = std::uint32_t(VNA_NATIVE_CALL *)(std::uint32_t);
using DrvSetFastSweepModeFn = std::uint32_t(VNA_NATIVE_CALL *)(std::uint32_t);
using DrvGetIDNFn = std::uint32_t(VNA_NATIVE_CALL *)(char *, int);
using DrvCheckSweepPortPowerFn = std::uint32_t(VNA_NATIVE_CALL *)(
    std::uint32_t, std::uint32_t *, std::uint16_t *, std::uint32_t);
using DrvDataFrameDownloadFn = std::uint32_t(VNA_NATIVE_CALL *)(
    std::uint32_t, std::uint32_t *, std::uint32_t, double *, std::uint32_t);

}  // 命名空间 VnaNative

extern "C" {

// 重置帧、待取结果和配置，并关闭射频；环境配置无效时保留原状态。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit();
// 取走一个匹配的待取结果；iqData 必须可写入 16 * pointCount 个 double。
// 此固定 ABI 没有容量参数；失败时不取走待取结果，也不修改输出。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double *iqData);
// 根据匹配的源端口帧同步生成待取 IQ，源端口编号为 0～3。
// 要求已完成初始化，并配置射频、接收链路和普通模式；存在未取结果时拒绝再次启动。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
// 接受 1～0x0f 的位图；模拟器仍生成完整的四端口 IQ 布局。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap);
// 接受 0 或 1；关闭射频时丢弃待取结果，保留已下载的帧。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled);
// 仅接受普通模式 0；启动前必须显式调用。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode);
// 初始化后写入 VnaPro,NativeSimulator,4PORT,1.0；容量以字符计数，包含空字符终止符。
// 容量为正但不足时，写入以空字符终止的前缀，并返回 BUFFER_TOO_SMALL。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char *buffer, int capacity);
// 未模拟功率检测；初始化后的有效调用清空输出，并返回 NOT_SUPPORTED。
// checkBitmap 指向一个 uint32；overFreqIndex 容纳 indexCapacity 个 uint16，容量为零时允许空指针。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap, std::uint16_t *overFreqIndex, std::uint32_t indexCapacity);
// 将普通帧复制到对应源端口的槽位；N 个点的元素数量必须分别为 1 + 6N 和 9N。
// 所有点使用同一个 0～3 范围内的源端口；quick、dynamicLink 和 dynamicEnabled 必须为零。
// 固定 ABI 中的指针保留非 const 类型，但解码只读取输入，并保留独立副本。
// 调用者保证实际可读容量；新帧下载不影响待取 IQ。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount, std::uint32_t *paramData,
    std::uint32_t paramDataLen, double *tableData, std::uint32_t tableDataLen);

}  // C 链接接口
#endif  // VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
