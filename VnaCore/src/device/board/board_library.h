/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_BOARD_LIBRARY_H
#define VNAPRO_VNA_CORE_BOARD_LIBRARY_H

#include "device/dynamic_library.h"
#include "vna_board/board_api_contract.h"

namespace VnaCore {
namespace Detail {

// VN10 的函数表与其加载器同属接口绑定职责，不与旧 VN08 的同名函数指针互换。
struct BoardFunctions final {
    VnaBoard::DrvHwInitFn hwInit = nullptr;
    VnaBoard::DrvSetFreqListFn setFreqList = nullptr;
    VnaBoard::DrvSetPowerListFn setPowerList = nullptr;
    VnaBoard::DrvSetIfbwListFn setIfbwList = nullptr;
    VnaBoard::DrvSetWorkPortsFn setWorkPorts = nullptr;
    VnaBoard::DrvDataFrameDownloadFn dataFrameDownload = nullptr;
    VnaBoard::DrvSetRfPowerOnFn setRfPowerOn = nullptr;
    VnaBoard::DrvStartSweepFn startSweep = nullptr;
    VnaBoard::DrvGetSweepDataFn getSweepData = nullptr;
};

class BoardLibrary final {
public:
    BoardLibrary() = default;
    BoardLibrary(const BoardLibrary &) = delete;
    BoardLibrary &operator=(const BoardLibrary &) = delete;
    BoardLibrary(BoardLibrary &&) = delete;
    BoardLibrary &operator=(BoardLibrary &&) = delete;
    ~BoardLibrary() noexcept;

    // 先解析全部 VN10 函数，缺少任何函数时不调用设备接口。
    void Open(const std::string &path);
    // 设备会话先关闭射频和结束扫描，再清空函数表并卸载库。
    void Close() noexcept;

    const BoardFunctions &Functions() const noexcept
    {
        return functions_;
    }

private:
    DynamicLibrary library_;
    BoardFunctions functions_;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
