#ifndef VNA_HARDWARE_POSIX_DRIVER_FILE_H
#define VNA_HARDWARE_POSIX_DRIVER_FILE_H

#include <string>
#include <vector>

#include "Result.h"

namespace vna::detail {

// 文件描述符和动态库句柄必须成对释放，这两个类型只负责 POSIX 资源所有权。
class PosixFileDescriptor final {
public:
    explicit PosixFileDescriptor(int value);
    ~PosixFileDescriptor();
    PosixFileDescriptor(PosixFileDescriptor &&other) noexcept;
    PosixFileDescriptor &operator=(PosixFileDescriptor &&other) noexcept;
    PosixFileDescriptor(const PosixFileDescriptor &) = delete;
    PosixFileDescriptor &operator=(const PosixFileDescriptor &) = delete;

    int get() const;

private:
    int value_{-1};
};

class PosixDynamicLibrary final {
public:
    explicit PosixDynamicLibrary(void *handle);
    ~PosixDynamicLibrary();
    PosixDynamicLibrary(PosixDynamicLibrary &&other) noexcept;
    PosixDynamicLibrary &operator=(PosixDynamicLibrary &&other) noexcept;
    PosixDynamicLibrary(const PosixDynamicLibrary &) = delete;
    PosixDynamicLibrary &operator=(const PosixDynamicLibrary &) = delete;

    void *get() const;
    // release 把 dlclose 责任交给 DriverModule，返回后本对象变为空所有者。
    void *release();

private:
    void *handle_{nullptr};
};

// 目录校验、枚举和最终 dlopen 都围绕同一目录描述符进行，避免路径检查后被替换。
Result<PosixFileDescriptor> openTrustedDriverDirectory(const std::string &directory);
Result<std::vector<std::string>> collectTrustedDriverNames(int rootDescriptor);
Result<PosixDynamicLibrary> openTrustedDriverFile(int rootDescriptor, const std::string &name);

}  // namespace vna::detail

#endif
