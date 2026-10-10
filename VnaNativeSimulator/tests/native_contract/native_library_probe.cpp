/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_library_probe.h"

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace VnaTest {
namespace {

bool IsAbsolutePath(const std::string &path) noexcept
{
#if defined(_WIN32)
    constexpr std::size_t driveRootedMinLength = 3U;
    constexpr std::size_t uncMinLength = 5U;
    const bool driveRooted = path.size() >= driveRootedMinLength && std::isalpha(static_cast<unsigned char>(path[0])) &&
                             path[1] == ':' && (path[2] == '/' || path[2] == '\\');
    const bool unc = path.size() >= uncMinLength && path[0] == '\\' && path[1] == '\\' && path[2] != '\\';
    return driveRooted || unc;
#else
    return !path.empty() && path[0] == '/';
#endif
}

#if defined(_WIN32)
std::string LoaderError(const char *operation)
{
    std::ostringstream message;
    message << operation << " failed (Windows error " << GetLastError() << ')';
    return message.str();
}
#endif

}  // namespace

NativeLibraryProbe::~NativeLibraryProbe() noexcept
{
    if (!Close()) {
        std::fputs("Failed to unload the native test library\n", stderr);
    }
}

bool NativeLibraryProbe::Open(const std::string &absolutePath, std::string &errorMessage)
{
    errorMessage.clear();
    if (!Close()) {
        errorMessage = "Unable to unload the previous native library";
        return false;
    }
    if (!IsAbsolutePath(absolutePath)) {
        errorMessage = "The native library path must be absolute";
        return false;
    }
#if defined(_WIN32)
    handle_ = LoadLibraryExA(absolutePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    handle_ = dlopen(absolutePath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (handle_ == nullptr) {
#if defined(_WIN32)
        errorMessage = LoaderError("LoadLibraryEx");
#else
        const char *loaderError = dlerror();
        errorMessage = loaderError == nullptr ? "dlopen failed" : loaderError;
#endif
        return false;
    }
    return true;
}

bool NativeLibraryProbe::Close() noexcept
{
    functions_ = NativeFunctions{};
    if (handle_ == nullptr) {
        return true;
    }
#if defined(_WIN32)
    const bool closed = FreeLibrary(handle_) != 0;
#else
    const bool closed = dlclose(handle_) == 0;
#endif
    if (closed) {
        handle_ = nullptr;
    }
    return closed;
}

bool NativeLibraryProbe::IsOpen() const noexcept
{
    return handle_ != nullptr;
}

NativeLibraryProbe::SymbolAddress NativeLibraryProbe::ResolveAddress(
    const char *symbolName, std::vector<std::string> &missingSymbols, std::string &errorMessage) const
{
#if defined(_WIN32)
    const SymbolAddress symbol = GetProcAddress(handle_, symbolName);
    const bool failed = symbol == nullptr;
    if (failed && errorMessage.empty()) {
        errorMessage = std::string(symbolName) + ": " + LoaderError("GetProcAddress");
    }
#else
    dlerror();
    const SymbolAddress symbol = dlsym(handle_, symbolName);
    const char *loaderError = dlerror();
    const bool failed = loaderError != nullptr || symbol == nullptr;
    if (failed && errorMessage.empty()) {
        errorMessage = std::string(symbolName) + ": " + (loaderError == nullptr ? "Null symbol address" : loaderError);
    }
#endif
    if (failed) {
        missingSymbols.push_back(symbolName);
        return nullptr;
    }
    return symbol;
}

void NativeLibraryProbe::ResolveInitialization(std::vector<std::string> &missingSymbols, std::string &errorMessage)
{
    // Platform loaders return an address representation; copy it into the exact ABI pointer type.
    // Each assertion makes this explicitly platform-dependent bridge reject unsupported layouts.
    const SymbolAddress hwInit = ResolveAddress(VnaNative::g_drvHwInitSymbol, missingSymbols, errorMessage);
    static_assert(sizeof(functions_.hwInit) == sizeof(hwInit), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.hwInit, &hwInit, sizeof(hwInit));
    const SymbolAddress getIdn = ResolveAddress(VnaNative::g_drvGetIdnSymbol, missingSymbols, errorMessage);
    static_assert(sizeof(functions_.getIDN) == sizeof(getIdn), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.getIDN, &getIdn, sizeof(getIdn));
    const SymbolAddress checkPower =
        ResolveAddress(VnaNative::g_drvCheckSweepPortPowerSymbol, missingSymbols, errorMessage);
    static_assert(
        sizeof(functions_.checkSweepPortPower) == sizeof(checkPower), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.checkSweepPortPower, &checkPower, sizeof(checkPower));
}

void NativeLibraryProbe::ResolveAcquisition(std::vector<std::string> &missingSymbols, std::string &errorMessage)
{
    const SymbolAddress getSweep = ResolveAddress(VnaNative::g_drvGetSweepDataSymbol, missingSymbols, errorMessage);
    static_assert(sizeof(functions_.getSweepData) == sizeof(getSweep), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.getSweepData, &getSweep, sizeof(getSweep));
    const SymbolAddress startSweep = ResolveAddress(VnaNative::g_drvStartSweepSymbol, missingSymbols, errorMessage);
    static_assert(sizeof(functions_.startSweep) == sizeof(startSweep), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.startSweep, &startSweep, sizeof(startSweep));
    const SymbolAddress download =
        ResolveAddress(VnaNative::g_drvDataFrameDownloadSymbol, missingSymbols, errorMessage);
    static_assert(
        sizeof(functions_.dataFrameDownload) == sizeof(download), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.dataFrameDownload, &download, sizeof(download));
}

void NativeLibraryProbe::ResolveConfiguration(std::vector<std::string> &missingSymbols, std::string &errorMessage)
{
    const SymbolAddress activateRx = ResolveAddress(VnaNative::g_drvActivateRxLinkSymbol, missingSymbols, errorMessage);
    static_assert(
        sizeof(functions_.activateRxLink) == sizeof(activateRx), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.activateRxLink, &activateRx, sizeof(activateRx));
    const SymbolAddress rfPower = ResolveAddress(VnaNative::g_drvSetRfPowerOnSymbol, missingSymbols, errorMessage);
    static_assert(sizeof(functions_.setRfPowerOn) == sizeof(rfPower), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.setRfPowerOn, &rfPower, sizeof(rfPower));
    const SymbolAddress fastMode = ResolveAddress(VnaNative::g_drvSetFastSweepModeSymbol, missingSymbols, errorMessage);
    static_assert(
        sizeof(functions_.setFastSweepMode) == sizeof(fastMode), "Unsupported platform function pointer layout");
    std::memcpy(&functions_.setFastSweepMode, &fastMode, sizeof(fastMode));
}

bool NativeLibraryProbe::ResolveAll(std::vector<std::string> &missingSymbols, std::string &errorMessage)
{
    missingSymbols.clear();
    errorMessage.clear();
    if (handle_ == nullptr) {
        errorMessage = "The native library has not been opened";
        return false;
    }
    ResolveInitialization(missingSymbols, errorMessage);
    ResolveAcquisition(missingSymbols, errorMessage);
    ResolveConfiguration(missingSymbols, errorMessage);
    return missingSymbols.empty();
}

const NativeFunctions &NativeLibraryProbe::Functions() const noexcept
{
    return functions_;
}

}  // namespace VnaTest
