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

namespace vna_core {
namespace detail {
namespace {

bool isAbsolutePath(const std::string &path)
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
Function resolve(void *handle, const char *name)
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
    Function function;
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

}  // namespace

NativeLibrary::~NativeLibrary()
{
    close();
}

void NativeLibrary::open(const std::string &path)
{
    if (!isAbsolutePath(path)) {
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
        functions_.hwInit = resolve<vna_native::DrvHwInitFn>(handle_, vna_native::kDrvHwInitSymbol);
        functions_.getSweepData = resolve<vna_native::DrvGetSweepDataFn>(handle_, vna_native::kDrvGetSweepDataSymbol);
        functions_.startSweep = resolve<vna_native::DrvStartSweepFn>(handle_, vna_native::kDrvStartSweepSymbol);
        functions_.activateRxLink =
            resolve<vna_native::DrvActivateRxLinkFn>(handle_, vna_native::kDrvActivateRxLinkSymbol);
        functions_.setRfPowerOn = resolve<vna_native::DrvSetRfPowerOnFn>(handle_, vna_native::kDrvSetRfPowerOnSymbol);
        functions_.setFastSweepMode =
            resolve<vna_native::DrvSetFastSweepModeFn>(handle_, vna_native::kDrvSetFastSweepModeSymbol);
        functions_.getIDN = resolve<vna_native::DrvGetIDNFn>(handle_, vna_native::kDrvGetIDNSymbol);
        functions_.checkSweepPortPower =
            resolve<vna_native::DrvCheckSweepPortPowerFn>(handle_, vna_native::kDrvCheckSweepPortPowerSymbol);
        functions_.dataFrameDownload =
            resolve<vna_native::DrvDataFrameDownloadFn>(handle_, vna_native::kDrvDataFrameDownloadSymbol);
    } catch (...) {
        close();
        throw;
    }
}

void NativeLibrary::close()
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

}  // namespace detail
}  // namespace vna_core
