/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "server_config_loader.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "server_config_io.h"
#include "server_startup_config_reader.h"
#include "yaml_config_reader.h"

namespace VnaServer {
namespace {
void ReadLevel(const YAML::Node &node, const std::string &path, VnaLog::Level &level)
{
    if (!node || !node["level"]) {
        return;
    }
    std::string text;
    YamlConfig::ReadString(node, "level", path, text);
    const std::array<std::pair<const char *, VnaLog::Level>, 7> levels{{{"TRACE", VnaLog::Level::TRACE},
        {"DEBUG", VnaLog::Level::DEBUG}, {"INFO", VnaLog::Level::INFO}, {"WARN", VnaLog::Level::WARN},
        {"ERROR", VnaLog::Level::ERROR_LEVEL}, {"CRITICAL", VnaLog::Level::CRITICAL}, {"OFF", VnaLog::Level::OFF}}};
    for (const auto &entry : levels) {
        if (text == entry.first) {
            level = entry.second;
            return;
        }
    }
    YamlConfig::Invalid(node["level"], path + ".level", "日志级别不合法，必须使用大写名称");
}

void ReadLogFile(const YAML::Node &node, VnaLog::LogConfig &config)
{
    YamlConfig::CheckMap(
        node, "log.file", {"enabled", "path", "maxSizeBytes", "maxFiles", "maxRecordBytes", "rotateOnOpen"});
    if (!node) {
        return;
    }
    YamlConfig::ReadBool(node, "enabled", "log.file", config.fileEnabled);
    YamlConfig::ReadString(node, "path", "log.file", config.filePath);
    YamlConfig::ReadSize(node, "maxSizeBytes", "log.file", config.maxFileBytes);
    YamlConfig::ReadSize(node, "maxFiles", "log.file", config.maxFiles);
    YamlConfig::ReadSize(node, "maxRecordBytes", "log.file", config.maxRecordBytes);
    YamlConfig::ReadBool(node, "rotateOnOpen", "log.file", config.rotateOnOpen);
    if (config.filePath.empty()) {
        YamlConfig::Invalid(node["path"], "log.file.path", "文件路径不能为空");
    }
}

void ReadLog(const YAML::Node &node, VnaLog::LogConfig &config)
{
    YamlConfig::CheckMap(node, "log", {"level", "pattern", "console", "file", "flush"});
    if (!node) {
        return;
    }
    ReadLevel(node, "log", config.level);
    YamlConfig::ReadString(node, "pattern", "log", config.pattern);
    const auto console = node["console"];
    YamlConfig::CheckMap(console, "log.console", {"enabled"});
    YamlConfig::ReadBool(console, "enabled", "log.console", config.consoleEnabled);
    ReadLogFile(node["file"], config);
    const auto flush = node["flush"];
    YamlConfig::CheckMap(flush, "log.flush", {"level"});
    ReadLevel(flush, "log.flush", config.flushLevel);
    try {
        VnaLog::ValidateConfig(config);
    } catch (const std::invalid_argument &error) {
        YamlConfig::Invalid(node, "log", error.what());
    }
}

void ReadInstrument(const YAML::Node &node, ServerConfig &config)
{
    YamlConfig::CheckMap(node, "instrument", {"portCount", "frequency", "sweep"});
    if (!node) {
        return;
    }
    auto &profile = config.instrument;
    YamlConfig::ReadUnsigned(node, "portCount", "instrument", profile.portCount);
    const auto frequency = node["frequency"];
    YamlConfig::CheckMap(frequency, "instrument.frequency", {"minHz", "maxHz"});
    YamlConfig::ReadFrequency(frequency, "minHz", "instrument.frequency", profile.minFrequencyHz);
    YamlConfig::ReadFrequency(frequency, "maxHz", "instrument.frequency", profile.maxFrequencyHz);
    const auto sweep = node["sweep"];
    YamlConfig::CheckMap(sweep, "instrument.sweep",
        {"defaultStartHz", "defaultStopHz", "defaultPoints", "defaultIfBandwidthHz", "defaultPowerRaw",
            "defaultSweepMode"});
    YamlConfig::ReadFrequency(sweep, "defaultStartHz", "instrument.sweep", profile.defaultStartHz);
    YamlConfig::ReadFrequency(sweep, "defaultStopHz", "instrument.sweep", profile.defaultStopHz);
    YamlConfig::ReadUnsigned(sweep, "defaultPoints", "instrument.sweep", profile.defaultPoints);
    YamlConfig::ReadFrequency(sweep, "defaultIfBandwidthHz", "instrument.sweep", profile.defaultIfBandwidthHz);
    YamlConfig::ReadUint64(sweep, "defaultPowerRaw", "instrument.sweep", profile.defaultPowerRaw);
    std::string mode = "CONT";
    YamlConfig::ReadString(sweep, "defaultSweepMode", "instrument.sweep", mode);
    if (mode == "CONT") {
        config.defaultSweepMode = VnaService::SweepMode::CONTINUOUS;
    } else if (mode == "HOLD") {
        config.defaultSweepMode = VnaService::SweepMode::HOLD;
    } else {
        YamlConfig::Invalid(sweep["defaultSweepMode"], "instrument.sweep.defaultSweepMode", "必须为 CONT 或 HOLD");
    }
    try {
        VnaService::ValidateInstrumentProfile(profile);
    } catch (const std::invalid_argument &error) {
        YamlConfig::Invalid(node, "instrument", error.what());
    }
}
}  // 匿名命名空间

ServerConfig LoadServerConfig(const std::string &path)
{
    const auto root = YamlConfig::ParseDocument(ConfigIo::ReadUtf8(path));
    YamlConfig::CheckMap(root, "", {"server", "driver", "log", "instrument"});
    ServerConfig config;
    ReadListenerConfig(root["server"], config.server);
    ReadDriverConfig(root["driver"], config.driver);
    ReadLog(root["log"], config.log);
    ReadInstrument(root["instrument"], config);
    config.log.filePath = ConfigIo::ResolveFilePath(config.log.filePath, path);
    return config;
}

}  // 命名空间 VnaServer
