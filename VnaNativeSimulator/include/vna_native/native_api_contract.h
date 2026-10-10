/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
#define VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H

#include <cstdint>

// The shared loader/simulator ABI declares exactly nine unchanged C-linkage functions.
// CALL intentionally adds no calling convention; real vendor Windows ABI needs separate verification.
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

namespace vna_native {

// These status values describe the simulator, not an undocumented vendor driver contract.
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

// Ports are numbered 0..3. All ABI lengths count elements rather than bytes.
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

// Strong function-pointer aliases preserve the frozen dynamically loaded ABI signatures.
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

}  // namespace vna_native

extern "C" {

// Resets frames/pending/configuration and disables RF; invalid environment preserves the old state.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit();
// Consumes one matching pending result. iqData must hold 16 * pointCount writable doubles.
// This frozen ABI has no capacity parameter; failures do not consume pending or modify output.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double *iqData);
// Generates pending IQ synchronously from the matching source-port frame (0..3).
// Requires initialized/configured RF, RX and normal mode; unread pending rejects another Start.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
// Accepts bitmap 1..0x0f. The simulator still produces the complete four-port IQ layout.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap);
// Accepts 0/1; disabling RF discards pending and retains downloaded frames.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled);
// Accepts only normal mode 0; must be called explicitly before Start.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode);
// Writes VnaPro,NativeSimulator,4PORT,1.0 after initialization; capacity counts chars including NUL.
// Positive insufficient capacity produces a NUL-terminated prefix and BUFFER_TOO_SMALL.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char *buffer, int capacity);
// Valid initialized calls clear outputs and return NOT_SUPPORTED: power detection is not modeled.
// checkBitmap points to one uint32; overFreqIndex holds indexCapacity uint16s (nullable when zero).
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t *checkBitmap, std::uint16_t *overFreqIndex, std::uint32_t indexCapacity);
// Copies a normal frame into its source-port slot. Counts must equal 1 + 6N and 9N for N points.
// Every point uses the same source port in 0..3; quick/dynamicLink/dynamicEnabled must be zero.
// Frozen ABI pointers remain non-const, but decoding is read-only and keeps independent copies.
// The caller guarantees actual readable capacity; pending IQ is unaffected by a new download.
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount, std::uint32_t *paramData,
    std::uint32_t paramDataLen, double *tableData, std::uint32_t tableDataLen);

}  // extern "C"
#endif  // VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
