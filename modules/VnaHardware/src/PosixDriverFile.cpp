#include "PosixDriverFile.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vna::detail {
namespace {

    VnaError fileError(std::string description)
    {
        VnaError error;
        error.code = VnaErrorCode::DriverLoadFailed;
        error.description = std::move(description);
        return error;
    }

    bool hasTrustedPermissions(const struct stat &attributes)
    {
        // 插件若可被其他用户替换，进程内 dlopen 等同于执行未授权代码。
        const bool trustedOwner = (attributes.st_uid == 0) || (attributes.st_uid == geteuid());
        const bool externallyWritable = (attributes.st_mode & (S_IWGRP | S_IWOTH)) != 0;
        return trustedOwner && !externallyWritable;
    }

    bool hasSuffix(const std::string &name, const char *suffix)
    {
        const std::size_t length = std::strlen(suffix);
        if (name.size() < length) {
            return false;
        }
        return name.compare(name.size() - length, length, suffix) == 0;
    }

    bool hasPluginName(const std::string &name)
    {
        // 先收窄候选命名空间，避免尝试 dlopen 目录内无关共享库。
        if (name.rfind("VnaDriver", 0) != 0) {
            return false;
        }
        return hasSuffix(name, ".so") || hasSuffix(name, ".dylib");
    }

    bool trustedDirectoryDescriptor(int descriptor)
    {
        if (descriptor < 0) {
            return false;
        }
        struct stat attributes{};
        if (fstat(descriptor, &attributes) != 0) {
            return false;
        }
        // 基于已打开描述符复核类型和权限，避免只信任 realpath 得到的文本路径。
        return S_ISDIR(attributes.st_mode) && hasTrustedPermissions(attributes);
    }

    bool trustedCandidateEntry(int rootDescriptor, const std::string &name)
    {
        // AT_SYMLINK_NOFOLLOW 检查目录项本身，候选符号链接不会被当作普通文件接受。
        struct stat attributes{};
        if (fstatat(rootDescriptor, name.c_str(), &attributes, AT_SYMLINK_NOFOLLOW) != 0) {
            return false;
        }
        return S_ISREG(attributes.st_mode) && hasTrustedPermissions(attributes);
    }

    bool trustedCandidateDescriptor(int descriptor)
    {
        if (descriptor < 0) {
            return false;
        }
        struct stat attributes{};
        if (fstat(descriptor, &attributes) != 0) {
            return false;
        }
        // openat 后再次检查实际打开对象，封闭“检查后替换”的竞态窗口。
        return S_ISREG(attributes.st_mode) && hasTrustedPermissions(attributes);
    }

    std::string descriptorPath(int descriptor)
    {
        // dlopen 描述符路径绑定已经打开的 inode，不再重新解析插件文件名。
#if defined(__APPLE__)
        return "/dev/fd/" + std::to_string(descriptor);
#else
        return "/proc/self/fd/" + std::to_string(descriptor);
#endif
    }

    void appendTrustedCandidate(int rootDescriptor, const dirent &entry, std::vector<std::string> &names)
    {
        const std::string name(entry.d_name);
        if (hasPluginName(name) && trustedCandidateEntry(rootDescriptor, name)) {
            names.push_back(name);
        }
    }

    Result<std::vector<std::string>> enumerateTrustedCandidates(DIR *directory, int rootDescriptor)
    {
        std::vector<std::string> names;
        for (;;) {
            errno = 0;
            const dirent *entry = readdir(directory);
            if (entry == nullptr) {
                break;
            }
            appendTrustedCandidate(rootDescriptor, *entry, names);
        }
        // readdir 以空指针同时表示结束和错误，必须在每轮前清 errno 才能区分。
        if (errno != 0) {
            return Result<std::vector<std::string>>::failure(fileError("driver directory cannot be enumerated"));
        }
        std::sort(names.begin(), names.end());
        return Result<std::vector<std::string>>::success(std::move(names));
    }

}  // namespace

PosixFileDescriptor::PosixFileDescriptor(int value) : value_(value)
{
}

PosixFileDescriptor::~PosixFileDescriptor()
{
    // 描述符采用唯一所有权；移动后源对象置为 -1，避免重复 close 误伤复用 fd。
    if (value_ >= 0) {
        close(value_);
    }
}

PosixFileDescriptor::PosixFileDescriptor(PosixFileDescriptor &&other) noexcept : value_(other.value_)
{
    other.value_ = -1;
}

PosixFileDescriptor &PosixFileDescriptor::operator=(PosixFileDescriptor &&other) noexcept
{
    if (this != &other) {
        if (value_ >= 0) {
            close(value_);
        }
        value_ = other.value_;
        other.value_ = -1;
    }
    return *this;
}

int PosixFileDescriptor::get() const
{
    return value_;
}

PosixDynamicLibrary::PosixDynamicLibrary(void *handle) : handle_(handle)
{
}

PosixDynamicLibrary::~PosixDynamicLibrary()
{
    // release 后句柄所有权交给 DriverModule，本包装器不再 dlclose。
    if (handle_ != nullptr) {
        dlclose(handle_);
    }
}

PosixDynamicLibrary::PosixDynamicLibrary(PosixDynamicLibrary &&other) noexcept : handle_(other.handle_)
{
    other.handle_ = nullptr;
}

PosixDynamicLibrary &PosixDynamicLibrary::operator=(PosixDynamicLibrary &&other) noexcept
{
    if (this != &other) {
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

void *PosixDynamicLibrary::get() const
{
    return handle_;
}

void *PosixDynamicLibrary::release()
{
    void *result = handle_;
    handle_ = nullptr;
    return result;
}

Result<PosixFileDescriptor> openTrustedDriverDirectory(const std::string &directory)
{
    // 先规范化根目录，再打开为描述符；后续操作不再依赖可变的原始路径。
    std::array<char, PATH_MAX> resolved{};
    if (realpath(directory.c_str(), resolved.data()) == nullptr) {
        return Result<PosixFileDescriptor>::failure(fileError("driver directory cannot be resolved"));
    }
    PosixFileDescriptor descriptor(open(resolved.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY));
    if (!trustedDirectoryDescriptor(descriptor.get())) {
        return Result<PosixFileDescriptor>::failure(fileError("driver directory is not trusted"));
    }
    return Result<PosixFileDescriptor>::success(std::move(descriptor));
}

Result<std::vector<std::string>> collectTrustedDriverNames(int rootDescriptor)
{
    using DirectoryPointer = std::unique_ptr<DIR, int (*)(DIR *)>;
    // fdopendir 会接管传入 fd，因此复制一份，保留根描述符供后续 openat 使用。
    const int duplicated = dup(rootDescriptor);
    if (duplicated < 0) {
        return Result<std::vector<std::string>>::failure(fileError("driver directory descriptor cannot be duplicated"));
    }
    DirectoryPointer directory(fdopendir(duplicated), &closedir);
    if (!directory) {
        close(duplicated);
        return Result<std::vector<std::string>>::failure(fileError("driver directory cannot be opened"));
    }
    return enumerateTrustedCandidates(directory.get(), rootDescriptor);
}

Result<PosixDynamicLibrary> openTrustedDriverFile(int rootDescriptor, const std::string &name)
{
    // openat 与 O_NOFOLLOW 把最终加载对象绑定到已校验目录，阻止候选符号链接跳转。
    PosixFileDescriptor descriptor(openat(rootDescriptor, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!trustedCandidateDescriptor(descriptor.get())) {
        return Result<PosixDynamicLibrary>::failure(fileError("driver file is not trusted"));
    }
    // RTLD_NOW 在发布模块前解析全部依赖，RTLD_LOCAL 防止插件符号污染全局命名空间。
    PosixDynamicLibrary library(dlopen(descriptorPath(descriptor.get()).c_str(), RTLD_NOW | RTLD_LOCAL));
    if (library.get() == nullptr) {
        return Result<PosixDynamicLibrary>::failure(fileError("dynamic driver cannot be loaded"));
    }
    return Result<PosixDynamicLibrary>::success(std::move(library));
}

}  // namespace vna::detail
