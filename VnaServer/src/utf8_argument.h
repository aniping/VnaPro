/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_UTF8_ARGUMENT_H
#define VNAPRO_SERVER_UTF8_ARGUMENT_H

#include <string>

namespace VnaServer {
// Windows 从原始宽命令行取路径，避免主函数窄参数丢失中文；其他平台直接使用原参数。
std::string Utf8Argument(int index, const char *fallback);
}  // 命名空间 VnaServer

#endif
