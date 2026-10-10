/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/native/native_library.h"

#include <cctype>
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace VnaCore {
namespace Detail {
namespace {

bool IsAbsolutePath(const std::string &path)
{
#if defined(_WIN32)
    const bool driveRooted = path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
                             (path[2] == '/' || path[2] == '\\');
    const bool unc = path.size() >= 5U && path[0] == '\\' && path[1] == '\\' && path[2] != '\\';
    return driveRooted || unc;
#else
    return !path.empty() && path[0] == '/';
#endif
}

template <typename Function>
Function Resolve(void *handle, const char *name)
{
#if defined(_WIN32)
    const FARPROC symbol = GetProcAddress(static_cast<HMODULE>(handle), name);
#else
    void *symbol = dlsym(handle, name);
#endif
    if (!symbol) {
        throw std::runtime_error(std::string("native symbol missing: ") + name);
    }
    static_assert(sizeof(Function) == sizeof(symbol), "Native function pointer size mismatch");
    Function function = nullptr;
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

}  // 匿名命名空间

NativeLibrary::~NativeLibrary() noexcept
{
    Close();
}

void NativeLibrary::Open(const std::string &path)
{
    if (!IsAbsolutePath(path)) {
        throw std::runtime_error("native library path must be absolute");
    }
    if (handle_) {
        return;
    }
#if defined(_WIN32)
    handle_ = static_cast<void *>(LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
    if (!handle_) {
        throw std::runtime_error("LoadLibraryEx failed, error=" + std::to_string(GetLastError()));
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char *error = dlerror();
        throw std::runtime_error(error ? error : "dlopen failed");
    }
#endif
    try {
        functions_.hwInit = Resolve<VnaNative::DrvHwInitFn>(handle_, VnaNative::g_drvHwInitSymbol);
        functions_.getSweepData = Resolve<VnaNative::DrvGetSweepDataFn>(handle_, VnaNative::g_drvGetSweepDataSymbol);
        functions_.startSweep = Resolve<VnaNative::DrvStartSweepFn>(handle_, VnaNative::g_drvStartSweepSymbol);
        functions_.activateRxLink =
            Resolve<VnaNative::DrvActivateRxLinkFn>(handle_, VnaNative::g_drvActivateRxLinkSymbol);
        functions_.setRfPowerOn = Resolve<VnaNative::DrvSetRfPowerOnFn>(handle_, VnaNative::g_drvSetRfPowerOnSymbol);
        functions_.setFastSweepMode =
            Resolve<VnaNative::DrvSetFastSweepModeFn>(handle_, VnaNative::g_drvSetFastSweepModeSymbol);
        functions_.getIdn = Resolve<VnaNative::DrvGetIDNFn>(handle_, VnaNative::g_drvGetIdnSymbol);
        functions_.checkSweepPortPower =
            Resolve<VnaNative::DrvCheckSweepPortPowerFn>(handle_, VnaNative::g_drvCheckSweepPortPowerSymbol);
        functions_.dataFrameDownload =
            Resolve<VnaNative::DrvDataFrameDownloadFn>(handle_, VnaNative::g_drvDataFrameDownloadSymbol);
    } catch (...) {
        Close();
        throw;
    }
}

void NativeLibrary::Close() noexcept
{
    functions_ = NativeFunctions();
    if (handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
