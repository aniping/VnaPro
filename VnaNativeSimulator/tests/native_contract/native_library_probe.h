/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H
#define VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>
#include <vector>

#include "vna_native/native_api_contract.h"

namespace vna_test {

struct NativeFunctions final {
    VnaNative::DrvHwInitFn hwInit = nullptr;
    VnaNative::DrvGetSweepDataFn getSweepData = nullptr;
    VnaNative::DrvStartSweepFn startSweep = nullptr;
    VnaNative::DrvActivateRxLinkFn activateRxLink = nullptr;
    VnaNative::DrvSetRfPowerOnFn setRfPowerOn = nullptr;
    VnaNative::DrvSetFastSweepModeFn setFastSweepMode = nullptr;
    VnaNative::DrvGetIDNFn getIDN = nullptr;
    VnaNative::DrvCheckSweepPortPowerFn checkSweepPortPower = nullptr;
    VnaNative::DrvDataFrameDownloadFn dataFrameDownload = nullptr;
};

class NativeLibraryProbe final {
public:
    // Owns no library until Open succeeds; destruction releases an open platform handle.
    NativeLibraryProbe() noexcept = default;
    ~NativeLibraryProbe() noexcept;
    NativeLibraryProbe(const NativeLibraryProbe &) = delete;
    NativeLibraryProbe &operator=(const NativeLibraryProbe &) = delete;
    NativeLibraryProbe(NativeLibraryProbe &&) = delete;
    NativeLibraryProbe &operator=(NativeLibraryProbe &&) = delete;

    // Rejects relative paths, replaces any previous library, and reports loader failures in errorMessage.
    bool Open(const std::string &absolutePath, std::string &errorMessage);
    // Clears callable pointers before unloading. Returns false if the platform rejects unloading.
    bool Close() noexcept;
    // Tests may call this before using any table entry; a failed Open leaves it false.
    bool IsOpen() const noexcept;
    // Resolves every frozen ABI entry; records all missing names and the first diagnostic.
    bool ResolveAll(std::vector<std::string> &missingSymbols, std::string &errorMessage);
    // The borrowed table remains valid only until Close/Open or probe destruction.
    const NativeFunctions &Functions() const noexcept;

private:
#if defined(_WIN32)
    using SymbolAddress = FARPROC;
    HMODULE handle_ = nullptr;
#else
    using SymbolAddress = void *;
    void *handle_ = nullptr;
#endif
    SymbolAddress ResolveAddress(
        const char *symbolName, std::vector<std::string> &missingSymbols, std::string &errorMessage) const;
    void ResolveInitialization(std::vector<std::string> &missingSymbols, std::string &errorMessage);
    void ResolveAcquisition(std::vector<std::string> &missingSymbols, std::string &errorMessage);
    void ResolveConfiguration(std::vector<std::string> &missingSymbols, std::string &errorMessage);

    NativeFunctions functions_;
};

}  // namespace vna_test
#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H
