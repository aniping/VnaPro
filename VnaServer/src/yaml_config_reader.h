/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_YAML_CONFIG_READER_H
#define VNAPRO_SERVER_YAML_CONFIG_READER_H

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <initializer_list>
#include <string>

namespace VnaServer {
namespace YamlConfig {

// 解析单个 YAML 文档；空文件使用默认配置，语法错误保留所在行号。
YAML::Node ParseDocument(const std::string &text);

// 验证可选映射的已知键、重复键和类型；拒绝自定义标签以避免歧义。
void CheckMap(const YAML::Node &node, const std::string &path, std::initializer_list<const char *> keys);

// 读取可选字段，缺失时保留目标默认值；数值和布尔字段必须使用未加引号的标量。
void ReadString(const YAML::Node &node, const std::string &key, const std::string &path, std::string &value);
void ReadBool(const YAML::Node &node, const std::string &key, const std::string &path, bool &value);
void ReadSize(const YAML::Node &node, const std::string &key, const std::string &path, std::size_t &value);
void ReadUnsigned(const YAML::Node &node, const std::string &key, const std::string &path, unsigned &value);
// 频率字段使用十进制整数 Hz，并限制在公开接口可精确表示的整数范围内。
void ReadFrequency(const YAML::Node &node, const std::string &key, const std::string &path, double &value);

// 为语义错误追加节点行号和完整配置路径后抛出 invalid_argument。
[[noreturn]] void Invalid(const YAML::Node &node, const std::string &path, const std::string &reason);

}  // 命名空间 YamlConfig
}  // 命名空间 VnaServer
#endif
