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
// Keep the verified handle open throughout incremental reading to prevent a
// path replacement between validation and a later reopening of the file.
class StaticFile final {
public:
    // Create a closed reader; Open is required before Advance.
    StaticFile() noexcept = default;
    // Open a verified regular file beneath root, or throw on invalid input.
    StaticFile(const std::string &root, const std::string &path);
    ~StaticFile() noexcept;
    StaticFile(const StaticFile &) = delete;
    StaticFile &operator=(const StaticFile &) = delete;
    StaticFile(StaticFile &&) = delete;
    StaticFile &operator=(StaticFile &&) = delete;
    // Replace the current handle; failed opening leaves this object closed.
    void Open(const std::string &root, const std::string &path);
    // Close the owned handle without throwing; repeated calls are harmless.
    void Close() noexcept;
    // Return the verified byte count of the current file.
    std::size_t Size() const noexcept;
    // Append one bounded block to the same initially empty buffer; true means the verified size was reached.
    // Invalid state, mismatched buffer length, read or allocation failure throw and close the reader.
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
}  // namespace VnaWeb

#endif
