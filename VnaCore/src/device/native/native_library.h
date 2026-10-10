/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_NATIVE_LIBRARY_H
#define VNAPRO_VNA_CORE_NATIVE_LIBRARY_H

#include <string>

#include "device/native/native_functions.h"

namespace vna_core {
namespace Detail {

class NativeLibrary final {
public:
    // This object owns one OS loader handle; copying or moving would duplicate ownership.
    NativeLibrary() = default;
    NativeLibrary(const NativeLibrary &) = delete;
    NativeLibrary &operator=(const NativeLibrary &) = delete;
    NativeLibrary(NativeLibrary &&) = delete;
    NativeLibrary &operator=(NativeLibrary &&) = delete;
    // Releases the loader handle after the device session has attempted RF shutdown.
    ~NativeLibrary() noexcept;
    // Resolves the complete required ABI from an absolute path; failure closes any partially loaded handle.
    void Open(const std::string &path);
    // Clears the function table before releasing the OS handle; repeated calls are harmless.
    void Close() noexcept;

    // The returned function table is valid only while this library remains open.
    const NativeFunctions &Functions() const noexcept
    {
        return functions_;
    }

private:
    void *handle_ = nullptr;
    NativeFunctions functions_;
};

}  // namespace Detail
}  // namespace vna_core

#endif
