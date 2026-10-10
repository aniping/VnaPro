/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNACORE_SRC_DEVICE_NATIVE_NATIVEFUNCTIONS_H
#define VNAPRO_VNACORE_SRC_DEVICE_NATIVE_NATIVEFUNCTIONS_H
#include "vna_native/native_api_contract.h"

namespace vna_core {
namespace Detail {

struct NativeFunctions final {
    VnaNative::DrvHwInitFn hwInit = nullptr;
    VnaNative::DrvGetSweepDataFn getSweepData = nullptr;
    VnaNative::DrvStartSweepFn startSweep = nullptr;
    VnaNative::DrvActivateRxLinkFn activateRxLink = nullptr;
    VnaNative::DrvSetRfPowerOnFn setRfPowerOn = nullptr;
    VnaNative::DrvSetFastSweepModeFn setFastSweepMode = nullptr;
    VnaNative::DrvGetIDNFn getIdn = nullptr;
    VnaNative::DrvCheckSweepPortPowerFn checkSweepPortPower = nullptr;
    VnaNative::DrvDataFrameDownloadFn dataFrameDownload = nullptr;
};

}  // namespace Detail
}  // namespace vna_core

#endif
