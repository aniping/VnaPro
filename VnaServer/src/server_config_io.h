/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_CONFIG_IO_H
#define VNAPRO_SERVER_SERVER_CONFIG_IO_H

#include <string>

namespace VnaServer {
namespace ConfigIo {

// 读取不超过 64 KiB 的配置并校验 UTF-8，支持字节序标记和 Windows 中文路径。
std::string ReadUtf8(const std::string &path);

// 将相对文件路径解释为配置目录下的路径，绝对路径保持原值。
std::string ResolveFilePath(const std::string &filePath, const std::string &configPath);

}  // 命名空间 ConfigIo
}  // 命名空间 VnaServer
#endif
