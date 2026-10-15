/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_VNA_BOARD_BOARD_API_CONTRACT_H
#define VNAPRO_VNA_BOARD_BOARD_API_CONTRACT_H

#include <cstdint>

// 保留供应头的默认调用约定；实际硬件 DLL 的链接约定仍需厂商确认。
#if defined(_WIN32)
#if defined(VNA_BOARD_BUILD_SHARED)
#define VNA_BOARD_EXPORT __declspec(dllexport)
#else
#define VNA_BOARD_EXPORT
#endif
#define VNA_BOARD_CALL
#elif defined(__GNUC__) || defined(__clang__)
#define VNA_BOARD_EXPORT __attribute__((visibility("default")))
#define VNA_BOARD_CALL
#else
#define VNA_BOARD_EXPORT
#define VNA_BOARD_CALL
#endif

namespace VnaBoard {

// 以下错误码是模拟器约定，供应头没有声明真实硬件的错误值。
enum DrvSimErrorCode : std::uint32_t {
    DRV_SIM_OK = 0U,
    DRV_SIM_INVALID_ARGUMENT = 1U,
    DRV_SIM_NOT_INITIALIZED = 2U,
    DRV_SIM_RF_DISABLED = 3U,
    DRV_SIM_INVALID_STATE = 4U,
    DRV_SIM_SIZE_OVERFLOW = 5U,
    DRV_SIM_BUFFER_TOO_SMALL = 6U,
    DRV_SIM_INTERNAL_ERROR = 7U,
    DRV_SIM_BUSY = 8U,
    DRV_SIM_CANCELLED = 9U
};

// 用户已确认每工作源端口连续返回请求频点，单频点包含四接收端口的 A/B 复数波。
enum BoardFrameLayout : std::uint32_t {
    BOARD_PORT_COUNT = 4U,
    IQ_VALUES_PER_RECEIVER = 4U,
    IQ_VALUES_PER_SOURCE_POINT = 16U
};

constexpr const char g_drvHwInitSymbol[] = "DrvHwInit";
constexpr const char g_drvSetFreqListSymbol[] = "DrvSetFreqList";
constexpr const char g_drvSetPowerListSymbol[] = "DrvSetPowerList";
constexpr const char g_drvSetIfBwListSymbol[] = "DrvSetIfBwList";
constexpr const char g_drvSetWorkPortsSymbol[] = "DrvSetWorkPorts";
constexpr const char g_drvDataFrameDownloadSymbol[] = "DrvDataFrameDownload";
constexpr const char g_drvSetRfPowerOnSymbol[] = "DrvSetRfPowerOn";
constexpr const char g_drvStartSweepSymbol[] = "DrvStartSweep";
constexpr const char g_drvGetSweepDataSymbol[] = "DrvGetSweepData";

using DrvHwInitFn = std::uint32_t(VNA_BOARD_CALL *)();
using DrvSetFreqListFn = void(VNA_BOARD_CALL *)(std::uint64_t *, std::uint32_t);
using DrvSetPowerListFn = void(VNA_BOARD_CALL *)(double *, std::uint32_t);
using DrvSetIfBwListFn = void(VNA_BOARD_CALL *)(std::uint32_t *, std::uint32_t);
using DrvSetWorkPortsFn = void(VNA_BOARD_CALL *)(std::uint32_t *, std::uint32_t);
using DrvDataFrameDownloadFn = std::uint32_t(VNA_BOARD_CALL *)();
using DrvSetRfPowerOnFn = std::uint32_t(VNA_BOARD_CALL *)(std::uint32_t);
using DrvStartSweepFn = std::uint32_t(VNA_BOARD_CALL *)();
using DrvGetSweepDataFn = std::uint32_t(VNA_BOARD_CALL *)(std::uint32_t, std::uint32_t, double *, int);

}  // 命名空间 VnaBoard

#endif  // VNAPRO_VNA_BOARD_BOARD_API_CONTRACT_H
