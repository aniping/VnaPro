/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_store_files.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace VnaServer {
namespace CalibrationStoreFiles {
namespace {
std::string UpperAscii(std::string text)
{
    for (auto &value : text) {
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - 'a' + 'A');
        }
    }
    return text;
}

bool NumberedDevice(const std::string &stem)
{
    if (stem.compare(0U, 3U, "COM") != 0 && stem.compare(0U, 3U, "LPT") != 0) {
        return false;
    }
    const auto suffix = stem.substr(3U);
    const std::array<std::string, 3U> superscripts{{"\xc2\xb9", "\xc2\xb2", "\xc2\xb3"}};
    return (suffix.size() == 1U && suffix[0U] >= '0' && suffix[0U] <= '9') ||
           std::find(superscripts.begin(), superscripts.end(), suffix) != superscripts.end();
}

bool ReservedDevice(std::string stem)
{
    stem.resize(stem.find('.') == std::string::npos ? stem.size() : stem.find('.'));
    while (!stem.empty() && stem.back() == ' ') {
        stem.pop_back();
    }
    stem = UpperAscii(std::move(stem));
    const std::array<std::string, 7U> devices{{"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"}};
    return std::find(devices.begin(), devices.end(), stem) != devices.end() || NumberedDevice(stem);
}
}  // 匿名命名空间

bool InternalFile(const std::string &name, const std::string &prefix, const std::string &ending)
{
    if (name.size() <= prefix.size() + ending.size() || name.size() > 160U ||
        name.compare(0U, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - ending.size(), ending.size(), ending) != 0) {
        return false;
    }
    const auto body = name.substr(prefix.size(), name.size() - prefix.size() - ending.size());
    return body.find_first_not_of("0123456789-") == std::string::npos;
}

bool NamedFile(const std::string &name)
{
    constexpr std::size_t EXTENSION_BYTES = 5U;
    if (name.size() <= EXTENSION_BYTES || name.size() > 128U + EXTENSION_BYTES ||
        name.compare(name.size() - EXTENSION_BYTES, EXTENSION_BYTES, ".vcal") != 0) {
        return false;
    }
    const auto stem = name.substr(0U, name.size() - EXTENSION_BYTES);
    if (UpperAscii(stem.substr(0U, 8U)) == "CHANNEL-" || stem.back() == '.' || stem.back() == ' ' ||
        ReservedDevice(stem)) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](unsigned char value) {
        return value < 0x20U || value == 0x7fU || std::strchr("<>:\"/\\|?*", value) != nullptr;
    });
}
}  // 命名空间 CalibrationStoreFiles
}  // 命名空间 VnaServer
