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

namespace VnaTest {

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
    // 仅在 Open 成功后持有动态库；析构时释放已打开的平台句柄。
    NativeLibraryProbe() noexcept = default;
    ~NativeLibraryProbe() noexcept;
    NativeLibraryProbe(const NativeLibraryProbe &) = delete;
    NativeLibraryProbe &operator=(const NativeLibraryProbe &) = delete;
    NativeLibraryProbe(NativeLibraryProbe &&) = delete;
    NativeLibraryProbe &operator=(NativeLibraryProbe &&) = delete;

    // 拒绝相对路径，替换此前的动态库，并在 errorMessage 中报告加载器错误。
    bool Open(const std::string &absolutePath, std::string &errorMessage);
    // 卸载前清空可调用指针；平台拒绝卸载时返回 false。
    bool Close() noexcept;
    // 测试可在使用函数表项前调用此方法；Open 失败后返回 false。
    bool IsOpen() const noexcept;
    // 解析全部固定 ABI 项，记录所有缺失名称及首条诊断信息。
    bool ResolveAll(std::vector<std::string> &missingSymbols, std::string &errorMessage);
    // 借用的函数表仅在下一次 Close、Open 或探针析构之前有效。
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

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H
