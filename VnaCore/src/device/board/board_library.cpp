/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/board/board_library.h"

namespace VnaCore {
namespace Detail {

BoardLibrary::~BoardLibrary() noexcept
{
    Close();
}

void BoardLibrary::Open(const std::string &path)
{
    library_.Open(path);
    try {
        // 从 VN10 专有函数开始检查，避免选错板卡时调用同名但签名不同的函数。
        functions_.setFreqList = library_.Resolve<VnaBoard::DrvSetFreqListFn>(VnaBoard::g_drvSetFreqListSymbol);
        functions_.setPowerList = library_.Resolve<VnaBoard::DrvSetPowerListFn>(VnaBoard::g_drvSetPowerListSymbol);
        functions_.setIfbwList = library_.Resolve<VnaBoard::DrvSetIfbwListFn>(VnaBoard::g_drvSetIfbwListSymbol);
        functions_.setWorkPorts = library_.Resolve<VnaBoard::DrvSetWorkPortsFn>(VnaBoard::g_drvSetWorkPortsSymbol);
        functions_.hwInit = library_.Resolve<VnaBoard::DrvHwInitFn>(VnaBoard::g_drvHwInitSymbol);
        functions_.dataFrameDownload =
            library_.Resolve<VnaBoard::DrvDataFrameDownloadFn>(VnaBoard::g_drvDataFrameDownloadSymbol);
        functions_.setRfPowerOn = library_.Resolve<VnaBoard::DrvSetRfPowerOnFn>(VnaBoard::g_drvSetRfPowerOnSymbol);
        functions_.startSweep = library_.Resolve<VnaBoard::DrvStartSweepFn>(VnaBoard::g_drvStartSweepSymbol);
        functions_.getSweepData = library_.Resolve<VnaBoard::DrvGetSweepDataFn>(VnaBoard::g_drvGetSweepDataSymbol);
    } catch (...) {
        Close();
        throw;
    }
}

void BoardLibrary::Close() noexcept
{
    functions_ = BoardFunctions();
    library_.Close();
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
