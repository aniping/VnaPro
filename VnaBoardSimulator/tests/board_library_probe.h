/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_LIBRARY_PROBE_H
#define VNAPRO_BOARD_LIBRARY_PROBE_H

#include <string>

#include "vna_board/board_api_contract.h"

namespace BoardTest {

struct BoardFunctions {
    VnaBoard::DrvHwInitFn initialize = nullptr;
    VnaBoard::DrvSetFreqListFn frequencies = nullptr;
    VnaBoard::DrvSetPowerListFn powers = nullptr;
    VnaBoard::DrvSetIfbwListFn bandwidths = nullptr;
    VnaBoard::DrvSetWorkPortsFn ports = nullptr;
    VnaBoard::DrvDataFrameDownloadFn download = nullptr;
    VnaBoard::DrvSetRfPowerOnFn rfPower = nullptr;
    VnaBoard::DrvStartSweepFn start = nullptr;
    VnaBoard::DrvGetSweepDataFn get = nullptr;
};

class BoardLibraryProbe final {
public:
    // 加载独立 DLL 并逐个验证九个原生符号。
    explicit BoardLibraryProbe(const std::string &path);
    // 在卸载 DLL 前先通过标准射频关闭调用回收工作线程。
    ~BoardLibraryProbe();
    BoardLibraryProbe(const BoardLibraryProbe &) = delete;
    BoardLibraryProbe &operator=(const BoardLibraryProbe &) = delete;
    BoardLibraryProbe(BoardLibraryProbe &&) = delete;
    BoardLibraryProbe &operator=(BoardLibraryProbe &&) = delete;
    // 返回该库绑定的函数指针，调用期不得超过探针生命周期。
    const BoardFunctions &Functions() const;

private:
    template <typename Function>
    Function Load(const char *name);
    void Close();

    void *handle_ = nullptr;
    BoardFunctions functions_;
};

}  // 命名空间 BoardTest
#endif  // VNAPRO_BOARD_LIBRARY_PROBE_H
