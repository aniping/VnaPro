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
    // 只清空当前绑定，不卸载 VN10 模块；解析失败及析构也遵守进程生命周期策略。
    void Close() noexcept;
    // VN10 没有释放入口；经过初始化的模块正常或异常关闭都保持驻留，不释放加载引用。
    void KeepLoadedUntilExit() noexcept;

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
