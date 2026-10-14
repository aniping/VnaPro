/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "server_startup_config_reader.h"

#include <string>

#include "yaml_config_reader.h"

namespace VnaServer {
namespace {
constexpr unsigned MAX_TCP_PORT = 65535U;

bool IsIpv4Address(const std::string &text)
{
    std::size_t offset = 0;
    for (unsigned group = 0; group < 4U; ++group) {
        const auto start = offset;
        unsigned value = 0;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
            value = value * 10U + static_cast<unsigned>(text[offset] - '0');
            ++offset;
            if (offset - start > 3U || value > 255U) {
                return false;
            }
        }
        // 拒绝前导零，避免被网络接口按八进制地址解释。
        if (offset == start || (offset - start > 1U && text[start] == '0')) {
            return false;
        }
        if (group == 3U) {
            return offset == text.size();
        }
        if (offset >= text.size() || text[offset] != '.') {
            return false;
        }
        ++offset;
    }
    return false;
}

void ReadPort(const YAML::Node &node, const std::string &key, unsigned &value)
{
    YamlConfig::ReadUnsigned(node, key, "server", value);
    if (value > MAX_TCP_PORT) {
        YamlConfig::Invalid(node[key], "server." + key, "TCP 端口必须位于 0..65535");
    }
}

void ReadIpv4(const YAML::Node &node, const std::string &key, std::string &value)
{
    YamlConfig::ReadString(node, key, "server", value);
    if (!IsIpv4Address(value)) {
        YamlConfig::Invalid(node[key], "server." + key, "必须为点分十进制 IPv4 地址，例如 127.0.0.1 或 0.0.0.0");
    }
}

bool ContainsControlCharacters(const std::string &text)
{
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 32U || byte == 127U) {
            return true;
        }
    }
    return false;
}

bool IsLibraryFilename(const std::string &text)
{
    if (text.empty() || text == "." || text == ".." || text.front() == ' ' || text.back() == ' ' ||
        text.back() == '.') {
        return false;
    }
    // 使用跨平台合法文件名，禁止目录分隔符、盘符及 Windows 替代数据流。
    if (text.find_first_of("/\\:<>\"|?*") != std::string::npos) {
        return false;
    }
    return !ContainsControlCharacters(text);
}

void ReadSimulator(const YAML::Node &node, SimulatorConfig &config)
{
    YamlConfig::CheckMap(node, "driver.simulator", {"mode", "seed"});
    if (!node) {
        return;
    }
    YamlConfig::ReadString(node, "mode", "driver.simulator", config.mode);
    YamlConfig::ReadUint64(node, "seed", "driver.simulator", config.seed);
    if (config.mode != "golden" && config.mode != "noise") {
        YamlConfig::Invalid(node["mode"], "driver.simulator.mode", "必须为 golden 或 noise");
    }
}
}  // 匿名命名空间

void ReadListenerConfig(const YAML::Node &node, ListenerConfig &config)
{
    YamlConfig::CheckMap(node, "server", {"ip", "scpiIp", "webIp", "scpiPort", "webPort", "webEnabled", "webRoot"});
    if (!node) {
        return;
    }
    ReadIpv4(node, "ip", config.ip);
    if (node["scpiIp"]) {
        ReadIpv4(node, "scpiIp", config.scpiIp);
    }
    if (node["webIp"]) {
        ReadIpv4(node, "webIp", config.webIp);
    }
    ReadPort(node, "scpiPort", config.scpiPort);
    ReadPort(node, "webPort", config.webPort);
    YamlConfig::ReadBool(node, "webEnabled", "server", config.webEnabled);
    YamlConfig::ReadString(node, "webRoot", "server", config.webRoot);
    if (config.webRoot.empty() || ContainsControlCharacters(config.webRoot)) {
        YamlConfig::Invalid(node["webRoot"], "server.webRoot", "目录不能为空或包含控制字符");
    }
}

void ReadDriverConfig(const YAML::Node &node, DriverConfig &config)
{
    YamlConfig::CheckMap(node, "driver", {"board", "library", "simulator"});
    if (!node) {
        return;
    }
    std::string board = "tdc1vn10";
    YamlConfig::ReadString(node, "board", "driver", board);
    if (board == "tdc1vn10") {
        config.api = VnaCore::DriverApi::TDC1VN10;
    } else if (board == "tdc1vn08") {
        config.api = VnaCore::DriverApi::TDC1VN08;
    } else {
        YamlConfig::Invalid(node["board"], "driver.board", "必须为 tdc1vn10 或 tdc1vn08");
    }
    YamlConfig::ReadString(node, "library", "driver", config.library);
    if (node["library"] && !IsLibraryFilename(config.library)) {
        YamlConfig::Invalid(node["library"], "driver.library", "必须为动态库文件名，不能为空或包含路径");
    }
    ReadSimulator(node["simulator"], config.simulator);
}

}  // 命名空间 VnaServer
