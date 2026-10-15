/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "server_config_expectations.h"

#include <stdexcept>

#include "server_config.h"

namespace ServerConfigTest {
namespace {

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void MappedLog(const VnaServer::ServerConfig &config, const std::string &directory)
{
    Expect(config.log.level == VnaLog::Level::DEBUG && config.log.flushLevel == VnaLog::Level::ERROR_LEVEL,
        "级别映射错误");
    Expect(!config.log.consoleEnabled && config.log.fileEnabled && config.log.rotateOnOpen, "布尔值映射错误");
    Expect(config.log.maxFileBytes == 8192U && config.log.maxFiles == 3U && config.log.maxRecordBytes == 1024U,
        "滚动日志数值映射错误");
    Expect(config.log.filePath == directory + "/output/service.log", "相对路径未使用配置文件目录");
    Expect(config.log.pattern == "%Y [%K] %n - value=%v", "格式中的等号未被保留");
}

void MappedInstrument(const VnaServer::ServerConfig &config)
{
    const auto &profile = config.instrument;
    Expect(profile.portCount == 2U && profile.minFrequencyHz == 1e7 && profile.maxFrequencyHz == 8e9,
        "二端口能力映射错误");
    Expect(profile.defaultStartHz == 2e7 && profile.defaultStopHz == 6e9 && profile.defaultPoints == 401U &&
               profile.defaultIfBandwidthHz == 100.0 && profile.defaultPowerDbm == -17.5 &&
               config.defaultSweepMode == VnaService::SweepMode::HOLD,
        "扫描默认配置映射错误");
}

void DefaultLog(const VnaServer::ServerConfig &config, const std::string &directory)
{
    Expect(config.log.level == VnaLog::Level::INFO && config.log.flushLevel == VnaLog::Level::INFO, "默认级别错误");
    Expect(config.log.consoleEnabled && config.log.fileEnabled && !config.log.rotateOnOpen, "默认输出开关错误");
    Expect(config.log.maxFileBytes == 10485760U && config.log.maxFiles == 5U && config.log.maxRecordBytes == 65536U,
        "默认滚动上限错误");
    Expect(config.log.filePath == directory + "/logs/vnapro.log", "默认文件路径未使用配置文件目录");
}

void DefaultInstrument(const VnaServer::ServerConfig &config)
{
    const auto &profile = config.instrument;
    Expect(profile.portCount == 4U && profile.minFrequencyHz == 1e7 && profile.maxFrequencyHz == 8e9,
        "默认四端口能力错误");
    Expect(profile.defaultStartHz == profile.minFrequencyHz && profile.defaultStopHz == profile.maxFrequencyHz &&
               profile.defaultPoints == 201U && profile.defaultIfBandwidthHz == 1000.0 &&
               profile.defaultPowerDbm == 0.0 && config.defaultSweepMode == VnaService::SweepMode::CONTINUOUS,
        "默认扫描未覆盖完整频段");
}

void DefaultListener(const VnaServer::ServerConfig &config)
{
    Expect(config.server.ip == "127.0.0.1" && config.server.scpiPort == 5025U && config.server.webPort == 8080U &&
               config.server.webEnabled && config.server.scpiIp.empty() && config.server.webIp.empty() &&
               config.server.webRoot == "WebUI",
        "默认监听配置错误");
}

void DefaultDriver(const VnaServer::ServerConfig &config)
{
    Expect(config.driver.type == VnaServer::DriverType::HARDWARE && config.driver.api == VnaCore::DriverApi::TDC1VN10 &&
               config.driver.library.empty() && config.driver.simulator.mode == "golden" &&
               config.driver.simulator.seed == 42U && config.driver.simulator.noiseAmplitude == 0.002,
        "默认驱动配置错误");
}

}  // 匿名命名空间

void CheckMapped(const VnaServer::ServerConfig &config, const std::string &directory)
{
    MappedLog(config, directory);
    MappedInstrument(config);
}

void CheckDefaults(const VnaServer::ServerConfig &config, const std::string &directory)
{
    DefaultLog(config, directory);
    DefaultInstrument(config);
    DefaultListener(config);
    DefaultDriver(config);
}

}  // 命名空间 ServerConfigTest
