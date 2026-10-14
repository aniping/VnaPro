/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "yaml_config_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace VnaServer {
namespace YamlConfig {
namespace {
constexpr std::uint64_t MAX_EXACT_FREQUENCY_HZ = 9007199254740991ULL;

YAML::Node Scalar(const YAML::Node &node, const std::string &key, const std::string &path, bool typed)
{
    if (!node) {
        return YAML::Node(YAML::NodeType::Undefined);
    }
    const auto item = node[key];
    if (!item) {
        return item;
    }
    if (!item.IsScalar()) {
        Invalid(item, path + "." + key, "必须为标量，不能为 null、数组或映射");
    }
    const auto tag = item.Tag();
    if ((typed && tag != "?") || (!typed && tag != "?" && tag != "!" && tag != "tag:yaml.org,2002:str")) {
        Invalid(item, path + "." + key, "类型不合法，数值和布尔值不能加引号或使用显式标签");
    }
    return item;
}

std::uint64_t ParseInteger(const YAML::Node &item, const std::string &path)
{
    const auto text = item.Scalar();
    if (text.empty()) {
        Invalid(item, path, "无符号十进制整数不能为空");
    }
    std::uint64_t result = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            Invalid(item, path, "必须为无符号十进制整数");
        }
        const auto numeric = static_cast<std::uint64_t>(digit - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - numeric) / 10U) {
            Invalid(item, path, "整数溢出");
        }
        result = result * 10U + numeric;
    }
    return result;
}
}  // 匿名命名空间

[[noreturn]] void Invalid(const YAML::Node &node, const std::string &path, const std::string &reason)
{
    const auto mark = node.Mark();
    const std::string location = mark.is_null() ? "" : "第 " + std::to_string(mark.line + 1) + " 行，";
    throw std::invalid_argument("服务配置" + location + "键 '" + path + "'：" + reason);
}

YAML::Node ParseDocument(const std::string &text)
{
    try {
        const auto documents = YAML::LoadAll(text);
        if (documents.empty()) {
            return YAML::Load("{}");
        }
        if (documents.size() != 1U) {
            throw std::invalid_argument("服务配置只能包含一个 YAML 文档");
        }
        return documents.front();
    } catch (const YAML::Exception &error) {
        throw std::invalid_argument("服务配置第 " + std::to_string(error.mark.line + 1) + " 行：" + error.msg);
    }
}

void CheckMap(const YAML::Node &node, const std::string &path, std::initializer_list<const char *> keys)
{
    if (!node) {
        return;
    }
    if (!node.IsMap() || (node.Tag() != "?" && node.Tag() != "tag:yaml.org,2002:map")) {
        Invalid(node, path, "必须为映射，不能为 null、数组、标量或自定义标签");
    }
    std::set<std::string> seen;
    for (const auto &entry : node) {
        if (!entry.first.IsScalar()) {
            Invalid(entry.first, path, "配置键必须为字符串");
        }
        const auto key = entry.first.Scalar();
        const auto fullPath = path.empty() ? key : path + "." + key;
        if (!seen.insert(key).second) {
            Invalid(entry.first, fullPath, "配置键重复");
        }
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            Invalid(entry.first, fullPath, "未知配置键");
        }
    }
}

void ReadString(const YAML::Node &node, const std::string &key, const std::string &path, std::string &value)
{
    const auto item = Scalar(node, key, path, false);
    if (item) {
        value = item.Scalar();
        if (value.find('\0') != std::string::npos) {
            Invalid(item, path + "." + key, "不能包含空字符");
        }
    }
}

void ReadBool(const YAML::Node &node, const std::string &key, const std::string &path, bool &value)
{
    const auto item = Scalar(node, key, path, true);
    if (!item) {
        return;
    }
    const auto text = item.Scalar();
    if (text != "true" && text != "false") {
        Invalid(item, path + "." + key, "布尔值必须为 true 或 false");
    }
    value = text == "true";
}

void ReadSize(const YAML::Node &node, const std::string &key, const std::string &path, std::size_t &value)
{
    const auto item = Scalar(node, key, path, true);
    if (item) {
        const auto parsed = ParseInteger(item, path + "." + key);
        if (parsed > std::numeric_limits<std::size_t>::max()) {
            Invalid(item, path + "." + key, "整数溢出");
        }
        value = static_cast<std::size_t>(parsed);
    }
}

void ReadUnsigned(const YAML::Node &node, const std::string &key, const std::string &path, unsigned &value)
{
    const auto item = Scalar(node, key, path, true);
    if (item) {
        const auto parsed = ParseInteger(item, path + "." + key);
        if (parsed > std::numeric_limits<unsigned>::max()) {
            Invalid(item, path + "." + key, "整数溢出");
        }
        value = static_cast<unsigned>(parsed);
    }
}

void ReadFrequency(const YAML::Node &node, const std::string &key, const std::string &path, double &value)
{
    const auto item = Scalar(node, key, path, true);
    if (!item) {
        return;
    }
    const auto parsed = ParseInteger(item, path + "." + key);
    if (parsed == 0U || parsed > MAX_EXACT_FREQUENCY_HZ) {
        Invalid(item, path + "." + key, "整数 Hz 必须位于 1..9007199254740991");
    }
    value = static_cast<double>(parsed);
}

void ReadUint64(const YAML::Node &node, const std::string &key, const std::string &path, std::uint64_t &value)
{
    const auto item = Scalar(node, key, path, true);
    if (item) {
        value = ParseInteger(item, path + "." + key);
    }
}

void ReadDouble(const YAML::Node &node, const std::string &key, const std::string &path, double &value)
{
    const auto item = Scalar(node, key, path, true);
    if (!item) {
        return;
    }
    const auto text = item.Scalar();
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    if (!(input >> std::noskipws >> parsed) || input.peek() != std::char_traits<char>::eof() ||
        !std::isfinite(parsed)) {
        Invalid(item, path + "." + key, "必须为有限十进制数，可使用科学计数法");
    }
    // 部分运行库会静默把非零极小值解析为零；不能把用户配置误解释成关闭噪声。
    const auto nonzero = text.find_first_of("123456789");
    const auto exponent = text.find_first_of("eE");
    if (parsed == 0.0 && nonzero != std::string::npos && nonzero < exponent) {
        Invalid(item, path + "." + key, "数值下溢，不能表示为非零 double");
    }
    value = parsed;
}

}  // 命名空间 YamlConfig
}  // 命名空间 VnaServer
