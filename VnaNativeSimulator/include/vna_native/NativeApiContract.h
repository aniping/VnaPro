#ifndef VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
#define VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H

#include <cstdint>

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

enum : std::uint32_t {
    kNativePortCount = 4U,
    kParamValuesPerPoint = 6U,
    kTableValuesPerPoint = 9U,
    kIqValuesPerPoint = 16U
};

static constexpr const char kDrvHwInitSymbol[] = "DrvHwInit";
static constexpr const char kDrvGetSweepDataSymbol[] = "DrvGetSweepData";
static constexpr const char kDrvStartSweepSymbol[] = "DrvStartSweep";
static constexpr const char kDrvActivateRxLinkSymbol[] = "DrvActivateRxLink";
static constexpr const char kDrvSetRfPowerOnSymbol[] = "DrvSetRfPowerOn";
static constexpr const char kDrvSetFastSweepModeSymbol[] = "DrvSetFastSweepMode";
static constexpr const char kDrvGetIDNSymbol[] = "DrvGetIDN";
static constexpr const char kDrvCheckSweepPortPowerSymbol[] = "DrvCheckSweepPortPower";
static constexpr const char kDrvDataFrameDownloadSymbol[] = "DrvDataFrameDownload";

typedef std::uint32_t(VNA_NATIVE_CALL* DrvHwInitFn)();
typedef std::uint32_t(VNA_NATIVE_CALL* DrvGetSweepDataFn)(std::uint32_t, double*);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvStartSweepFn)(std::uint32_t, std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvActivateRxLinkFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvSetRfPowerOnFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvSetFastSweepModeFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvGetIDNFn)(char*, int);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvCheckSweepPortPowerFn)(
    std::uint32_t, std::uint32_t*, std::uint16_t*, std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvDataFrameDownloadFn)(
    std::uint32_t, std::uint32_t*, std::uint32_t, double*, std::uint32_t);

}  // namespace vna_native

extern "C" {

VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit();
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double* iqData);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char* buffer, int capacity);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap, std::uint16_t* overFreqIndex, std::uint32_t indexCapacity);
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount, std::uint32_t* paramData,
    std::uint32_t paramDataLen, double* tableData, std::uint32_t tableDataLen);

}  // extern "C"

#endif  // VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
