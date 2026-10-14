/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_STARTUP_PATHS_H
#define VNAPRO_SERVER_STARTUP_PATHS_H

#include <string>

namespace VnaServer {

enum class StartupPathKind {
    FILE,
    DIRECTORY
};

// 返回正在运行的可执行文件目录，采用 UTF-8；不依赖当前工作目录或命令行中的程序名称。
std::string ExecutableDirectory();

// 检查 UTF-8 启动资源路径的类型，不依赖当前工作目录。
bool StartupResourceExists(const std::string &path, StartupPathKind kind);

// 优先使用程序旁的资源；有发布清单时禁止回退开发路径，使缺失发布文件按启动错误报告。
std::string DefaultStartupPath(const std::string &executableDirectory, const std::string &adjacentRelativePath,
    const std::string &developmentFallback, StartupPathKind kind = StartupPathKind::FILE);

}  // 命名空间 VnaServer

#endif
