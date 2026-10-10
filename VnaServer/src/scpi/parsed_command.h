/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_PARSED_COMMAND_H
#define VNAPRO_SCPI_PARSED_COMMAND_H

#include <string>
#include <vector>

namespace VnaScpi {
struct ParsedCommand final {
    std::string path;
    std::vector<std::string> arguments;
    bool query = false;
    int errorCode = 0;
    std::string error;
};
}  // 命名空间 VnaScpi

#endif
