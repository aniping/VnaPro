/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_PARSED_COMMAND_H
#define VNAPRO_SCPI_PARSED_COMMAND_H

#include <string>
#include <vector>

namespace vna_scpi {
struct ParsedCommand final {
    std::string path;
    std::vector<std::string> arguments;
    bool query = false;
    int errorCode = 0;
    std::string error;
};
}  // namespace vna_scpi

#endif
