/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "board_library_probe.h"

#include <cstring>

#include "test_support.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace BoardTest {

BoardLibraryProbe::BoardLibraryProbe(const std::string &path)
{
#if defined(_WIN32)
    handle_ = static_cast<void *>(LoadLibraryA(path.c_str()));
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    Require(handle_ != nullptr, "TDC1VN10 动态库加载失败");
    try {
        functions_.initialize = Load<VnaBoard::DrvHwInitFn>(VnaBoard::g_drvHwInitSymbol);
        functions_.frequencies = Load<VnaBoard::DrvSetFreqListFn>(VnaBoard::g_drvSetFreqListSymbol);
        functions_.powers = Load<VnaBoard::DrvSetPowerListFn>(VnaBoard::g_drvSetPowerListSymbol);
        functions_.bandwidths = Load<VnaBoard::DrvSetIfBwListFn>(VnaBoard::g_drvSetIfBwListSymbol);
        functions_.ports = Load<VnaBoard::DrvSetWorkPortsFn>(VnaBoard::g_drvSetWorkPortsSymbol);
        functions_.download = Load<VnaBoard::DrvDataFrameDownloadFn>(VnaBoard::g_drvDataFrameDownloadSymbol);
        functions_.rfPower = Load<VnaBoard::DrvSetRfPowerOnFn>(VnaBoard::g_drvSetRfPowerOnSymbol);
        functions_.start = Load<VnaBoard::DrvStartSweepFn>(VnaBoard::g_drvStartSweepSymbol);
        functions_.get = Load<VnaBoard::DrvGetSweepDataFn>(VnaBoard::g_drvGetSweepDataSymbol);
    } catch (...) {
        Close();
        throw;
    }
}

BoardLibraryProbe::~BoardLibraryProbe()
{
    Close();
}

template <typename Function>
Function BoardLibraryProbe::Load(const char *name)
{
#if defined(_WIN32)
    const auto address = GetProcAddress(static_cast<HMODULE>(handle_), name);
#else
    const auto address = dlsym(handle_, name);
#endif
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address), "函数指针与动态库符号地址大小不一致");
    std::memcpy(&function, &address, sizeof(function));
    Require(function != nullptr, std::string("缺少 TDC1VN10 符号：") + name);
    return function;
}

void BoardLibraryProbe::Close()
{
    if (handle_ == nullptr) {
        return;
    }
    if (functions_.rfPower != nullptr) {
        functions_.rfPower(0U);
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

const BoardFunctions &BoardLibraryProbe::Functions() const
{
    return functions_;
}

}  // 命名空间 BoardTest
