#include "VnaConfigurationFile.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "PosixDriverFile.h"

namespace vna::detail {
namespace {

    constexpr std::size_t maximumConfigurationBytes = 64U * 1024U;
    constexpr std::size_t maximumLineBytes = 4096;

    VnaError configurationError(std::string description)
    {
        VnaError error;
        error.code = VnaErrorCode::ConfigurationRejected;
        error.description = std::move(description);
        return error;
    }

    std::string trim(std::string value)
    {
        const auto visible = [](unsigned char character) { return std::isspace(character) == 0; };
        const auto first = std::find_if(value.begin(), value.end(), visible);
        const auto last = std::find_if(value.rbegin(), value.rend(), visible).base();
        return first < last ? std::string(first, last) : std::string{};
    }

    bool trustedConfigurationAttributes(const struct stat &attributes)
    {
        const bool trustedOwner = (attributes.st_uid == 0) || (attributes.st_uid == geteuid());
        const bool externallyWritable = (attributes.st_mode & (S_IWGRP | S_IWOTH)) != 0;
        const bool validSize =
            (attributes.st_size >= 0) && (static_cast<std::size_t>(attributes.st_size) <= maximumConfigurationBytes);
        return S_ISREG(attributes.st_mode) && trustedOwner && !externallyWritable && validSize;
    }

    Result<std::string> readContent(int descriptor)
    {
        std::array<char, 4096> buffer{};
        std::string content;
        for (;;) {
            const ssize_t count = read(descriptor, buffer.data(), buffer.size());
            if ((count < 0) && (errno == EINTR)) {
                continue;
            }
            if (count < 0) {
                return Result<std::string>::failure(configurationError("configuration cannot be read"));
            }
            if (count == 0) {
                return Result<std::string>::success(std::move(content));
            }
            const std::size_t received = static_cast<std::size_t>(count);
            if (received > (maximumConfigurationBytes - content.size())) {
                return Result<std::string>::failure(configurationError("configuration is too large"));
            }
            content.append(buffer.data(), received);
        }
    }

    Result<bool> insertEntry(const std::string &line, ConfigurationValues &values)
    {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return Result<bool>::failure(configurationError("configuration entry is malformed"));
        }
        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (key.empty() || value.empty() || !values.emplace(std::move(key), std::move(value)).second) {
            return Result<bool>::failure(configurationError("configuration contains an empty or duplicate entry"));
        }
        return Result<bool>::success(true);
    }

    Result<bool> consumeLine(
        std::string line, const std::string &selectedSection, std::string &section, ConfigurationValues &values)
    {
        line = trim(std::move(line));
        if (line.empty() || (line.front() == '#') || (line.front() == ';')) {
            return Result<bool>::success(true);
        }
        if (line.front() == '[') {
            if ((line.size() < 3) || (line.back() != ']')) {
                return Result<bool>::failure(configurationError("configuration section is malformed"));
            }
            section = trim(line.substr(1, line.size() - 2));
            if (section.empty()) {
                return Result<bool>::failure(configurationError("configuration section is empty"));
            }
            return Result<bool>::success(true);
        }
        if (line.back() == ']') {
            return Result<bool>::failure(configurationError("configuration section is malformed"));
        }
        return section == selectedSection ? insertEntry(line, values) : Result<bool>::success(true);
    }

}  // namespace

Result<TrustedConfigurationFile> readTrustedConfigurationFile(const std::string &source)
{
    char resolved[PATH_MAX]{};
    if (realpath(source.c_str(), resolved) == nullptr) {
        return Result<TrustedConfigurationFile>::failure(configurationError("configuration cannot be resolved"));
    }
    PosixFileDescriptor descriptor(open(resolved, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat attributes{};
    if ((fstat(descriptor.get(), &attributes) != 0) || !trustedConfigurationAttributes(attributes)) {
        return Result<TrustedConfigurationFile>::failure(configurationError("configuration is not trusted"));
    }
    auto content = readContent(descriptor.get());
    if (!content.hasValue()) {
        return Result<TrustedConfigurationFile>::failure(content.error());
    }
    return Result<TrustedConfigurationFile>::success(TrustedConfigurationFile{resolved, content.takeValue()});
}

Result<ConfigurationValues> readConfigurationSection(const std::string &content, const std::string &selectedSection)
{
    std::istringstream input(content);
    ConfigurationValues values;
    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > maximumLineBytes) {
            return Result<ConfigurationValues>::failure(configurationError("configuration line is too long"));
        }
        auto consumed = consumeLine(std::move(line), selectedSection, section, values);
        if (!consumed.hasValue()) {
            return Result<ConfigurationValues>::failure(consumed.error());
        }
    }
    if (!input.eof()) {
        return Result<ConfigurationValues>::failure(configurationError("configuration cannot be read"));
    }
    return Result<ConfigurationValues>::success(std::move(values));
}

Result<std::size_t> parseConfigurationSize(
    const std::string &text, std::size_t minimum, std::size_t maximum, const char *field)
{
    std::size_t value = 0;
    if (text.empty()) {
        return Result<std::size_t>::failure(configurationError(std::string{"configuration has an invalid "} + field));
    }
    for (const char character : text) {
        if ((character < '0') || (character > '9')) {
            return Result<std::size_t>::failure(
                configurationError(std::string{"configuration has an invalid "} + field));
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        const bool exceedsMaximum = (value > (maximum / 10)) || ((value == (maximum / 10)) && (digit > (maximum % 10)));
        if (exceedsMaximum) {
            return Result<std::size_t>::failure(
                configurationError(std::string{"configuration has an invalid "} + field));
        }
        value = (value * 10) + digit;
    }
    if (value < minimum) {
        return Result<std::size_t>::failure(configurationError(std::string{"configuration has an invalid "} + field));
    }
    return Result<std::size_t>::success(value);
}

}  // namespace vna::detail
