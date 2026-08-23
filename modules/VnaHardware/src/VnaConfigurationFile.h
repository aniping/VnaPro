#ifndef VNA_HARDWARE_VNA_CONFIGURATION_FILE_H
#define VNA_HARDWARE_VNA_CONFIGURATION_FILE_H

#include <cstddef>
#include <map>
#include <string>

#include "Result.h"

namespace vna::detail {

using ConfigurationValues = std::map<std::string, std::string>;

struct TrustedConfigurationFile {
    std::string path;
    std::string content;
};

Result<TrustedConfigurationFile> readTrustedConfigurationFile(const std::string &source);
Result<ConfigurationValues> readConfigurationSection(const std::string &content, const std::string &section);
Result<std::size_t> parseConfigurationSize(
    const std::string &text, std::size_t minimum, std::size_t maximum, const char *field);

}  // namespace vna::detail

#endif
