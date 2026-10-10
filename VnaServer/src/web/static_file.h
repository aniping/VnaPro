/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_STATIC_FILE_H
#define VNAPRO_WEB_STATIC_FILE_H

#include <cstddef>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <string>

namespace VnaWeb {
// 增量读取期间始终持有已验证的文件句柄，避免验证后重新打开时路径被替换。
class StaticFile final {
public:
    // 创建处于关闭状态的读取器；调用 Advance 前必须先调用 Open。
    StaticFile() noexcept = default;
    // 打开 root 下经过验证的普通文件，输入无效时抛出异常。
    StaticFile(const std::string &root, const std::string &path);
    ~StaticFile() noexcept;
    StaticFile(const StaticFile &) = delete;
    StaticFile &operator=(const StaticFile &) = delete;
    StaticFile(StaticFile &&) = delete;
    StaticFile &operator=(StaticFile &&) = delete;
    // 替换当前句柄；打开失败时保持关闭状态。
    void Open(const std::string &root, const std::string &path);
    // 关闭持有的句柄，不抛出异常；允许重复调用。
    void Close() noexcept;
    // 返回当前文件经过验证的字节数。
    std::size_t Size() const noexcept;
    // 向同一个初始为空的缓冲区追加一块有界数据；返回 true 表示已读取全部已验证字节。
    // 状态无效、缓冲区长度不匹配、读取或分配失败时，抛出异常并关闭读取器。
    bool Advance(std::string &output);

private:
    std::size_t size_ = 0U;
    std::size_t readOffset_ = 0U;
    std::size_t ReadAvailable(char *buffer, unsigned maximum);
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    static std::string FinalPath(HANDLE file);
    void OpenVerified(const std::string &root, std::string relative);
    void VerifyFile(const std::string &rootPath);
#else
    int file_ = -1;
    void OpenVerified(const std::string &root, const std::string &relative);
#endif
};
}  // 命名空间 VnaWeb

#endif
