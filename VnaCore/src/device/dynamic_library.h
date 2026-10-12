/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_DYNAMIC_LIBRARY_H
#define VNAPRO_VNA_CORE_DYNAMIC_LIBRARY_H

#include <cstring>
#include <string>

namespace VnaCore {
namespace Detail {

// 仅管理操作系统句柄；两个互不兼容的板卡接口各自解析自己的函数表。
class DynamicLibrary final {
public:
    DynamicLibrary() = default;
    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    DynamicLibrary(DynamicLibrary &&) = delete;
    DynamicLibrary &operator=(DynamicLibrary &&) = delete;
    ~DynamicLibrary() noexcept;

    // 只接受绝对 UTF-8 路径，打开已持有的库时保持原句柄。
    void Open(const std::string &path);
    // 调用者必须先停止库内的设备工作线程，再释放句柄。
    void Close() noexcept;

    template <typename Function>
    Function Resolve(const char *name) const
    {
        void *symbol = FindSymbol(name);
        static_assert(sizeof(Function) == sizeof(symbol), "Native function pointer size mismatch");
        Function function = nullptr;
        std::memcpy(&function, &symbol, sizeof(function));
        return function;
    }

private:
    void *FindSymbol(const char *name) const;
    void *handle_ = nullptr;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
