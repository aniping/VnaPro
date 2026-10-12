/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/native/native_library.h"

namespace VnaCore {
namespace Detail {

NativeLibrary::~NativeLibrary() noexcept
{
    Close();
}

void NativeLibrary::Open(const std::string &path)
{
    library_.Open(path);
    try {
        // 必须先完整解析旧接口，再允许调用同名且签名不兼容的原生函数。
        functions_.activateRxLink =
            library_.Resolve<VnaNative::DrvActivateRxLinkFn>(VnaNative::g_drvActivateRxLinkSymbol);
        functions_.hwInit = library_.Resolve<VnaNative::DrvHwInitFn>(VnaNative::g_drvHwInitSymbol);
        functions_.getSweepData = library_.Resolve<VnaNative::DrvGetSweepDataFn>(VnaNative::g_drvGetSweepDataSymbol);
        functions_.startSweep = library_.Resolve<VnaNative::DrvStartSweepFn>(VnaNative::g_drvStartSweepSymbol);
        functions_.setRfPowerOn = library_.Resolve<VnaNative::DrvSetRfPowerOnFn>(VnaNative::g_drvSetRfPowerOnSymbol);
        functions_.setFastSweepMode =
            library_.Resolve<VnaNative::DrvSetFastSweepModeFn>(VnaNative::g_drvSetFastSweepModeSymbol);
        functions_.getIdn = library_.Resolve<VnaNative::DrvGetIDNFn>(VnaNative::g_drvGetIdnSymbol);
        functions_.checkSweepPortPower =
            library_.Resolve<VnaNative::DrvCheckSweepPortPowerFn>(VnaNative::g_drvCheckSweepPortPowerSymbol);
        functions_.dataFrameDownload =
            library_.Resolve<VnaNative::DrvDataFrameDownloadFn>(VnaNative::g_drvDataFrameDownloadSymbol);
    } catch (...) {
        Close();
        throw;
    }
}

void NativeLibrary::Close() noexcept
{
    functions_ = NativeFunctions();
    library_.Close();
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
