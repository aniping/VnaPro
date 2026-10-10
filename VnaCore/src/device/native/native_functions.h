#ifndef VNAPRO_VNACORE_SRC_DEVICE_NATIVE_NATIVEFUNCTIONS_H
#define VNAPRO_VNACORE_SRC_DEVICE_NATIVE_NATIVEFUNCTIONS_H
#include "vna_native/native_api_contract.h"

namespace vna_core {
namespace detail {

struct NativeFunctions {
    vna_native::DrvHwInitFn hwInit = nullptr;
    vna_native::DrvGetSweepDataFn getSweepData = nullptr;
    vna_native::DrvStartSweepFn startSweep = nullptr;
    vna_native::DrvActivateRxLinkFn activateRxLink = nullptr;
    vna_native::DrvSetRfPowerOnFn setRfPowerOn = nullptr;
    vna_native::DrvSetFastSweepModeFn setFastSweepMode = nullptr;
    vna_native::DrvGetIDNFn getIDN = nullptr;
    vna_native::DrvCheckSweepPortPowerFn checkSweepPortPower = nullptr;
    vna_native::DrvDataFrameDownloadFn dataFrameDownload = nullptr;
};

}  // namespace detail
}  // namespace vna_core

#endif
