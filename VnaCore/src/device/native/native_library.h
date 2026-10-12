/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_NATIVE_LIBRARY_H
#define VNAPRO_VNA_CORE_NATIVE_LIBRARY_H

#include <string>

#include "device/dynamic_library.h"
#include "device/native/native_functions.h"

namespace VnaCore {
namespace Detail {

class NativeLibrary final {
public:
    // 此对象独占一个操作系统加载器句柄；复制或移动会重复持有其所有权。
    NativeLibrary() = default;
    NativeLibrary(const NativeLibrary &) = delete;
    NativeLibrary &operator=(const NativeLibrary &) = delete;
    NativeLibrary(NativeLibrary &&) = delete;
    NativeLibrary &operator=(NativeLibrary &&) = delete;
    // 设备会话尝试关闭射频后，再释放加载器句柄。
    ~NativeLibrary() noexcept;
    // 从绝对路径加载并解析全部必需 ABI；失败时关闭已部分加载的句柄。
    void Open(const std::string &path);
    // 释放操作系统句柄前先清空函数表；重复调用无副作用。
    void Close() noexcept;

    // 返回的函数表仅在此动态库保持打开期间有效。
    const NativeFunctions &Functions() const noexcept
    {
        return functions_;
    }

private:
    DynamicLibrary library_;
    NativeFunctions functions_;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
