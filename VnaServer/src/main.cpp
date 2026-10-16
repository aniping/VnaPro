/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "calibration_store.h"
#include "driver_options.h"
#include "scpi_tcp_server.h"
#include "server_config_loader.h"
#include "server_defaults.h"
#include "simulator_environment.h"
#include "startup_paths.h"
#include "vna_core/acquisition_core.h"
#include "vna_log/logger.h"
#include "vna_service/instrument_service.h"

namespace {
// 信号处理函数只能为 volatile sig_atomic_t 赋值，不能访问服务状态。
volatile std::sig_atomic_t g_signalReceived = 0;

void HandleSignal(int) noexcept
{
    g_signalReceived = 1;
}

bool IsAbsolutePath(const std::string &path)
{
    return !path.empty() && (path.front() == '/' || path.front() == '\\' ||
                                (path.size() >= 3U && path[1] == ':' && (path[2] == '/' || path[2] == '\\')));
}

void ApplyStartupConfiguration(const VnaServer::ServerConfig &config, const std::string &directory,
    VnaServer::ListenOptions &options, VnaServer::DriverOptions &driver)
{
    options.bindAddress = config.server.scpiIp.empty() ? config.server.ip : config.server.scpiIp;
    options.port = static_cast<std::uint16_t>(config.server.scpiPort);
    options.web.bindAddress = config.server.webIp.empty() ? config.server.ip : config.server.webIp;
    options.web.port = static_cast<std::uint16_t>(config.server.webPort);
    options.web.enabled = config.server.webEnabled;
    const auto &webRoot = config.server.webRoot;
    options.web.staticRoot = IsAbsolutePath(webRoot) ? webRoot : directory + "/" + webRoot;
    if (webRoot == "WebUI") {
        options.web.staticRoot = VnaServer::DefaultStartupPath(
            directory, webRoot, VnaServer::DEFAULT_WEB_ROOT_PATH, VnaServer::StartupPathKind::DIRECTORY);
    }
    if (options.web.enabled &&
        !VnaServer::StartupResourceExists(options.web.staticRoot + "/index.html", VnaServer::StartupPathKind::FILE)) {
        throw std::invalid_argument("WebUI 目录中没有 index.html：" + options.web.staticRoot);
    }
    driver.api = config.driver.api;
    if (config.driver.type == VnaServer::DriverType::HARDWARE && config.driver.library.empty()) {
        throw std::invalid_argument("真实驱动必须通过 driver.library 明确指定库文件名");
    }
    const char *defaultLibrary = driver.api == VnaCore::DriverApi::TDC1VN10 ? VnaServer::DEFAULT_VN10_DRIVER_FILENAME
                                                                            : VnaServer::DEFAULT_VN08_DRIVER_FILENAME;
    driver.path = directory + "/" + (config.driver.library.empty() ? defaultLibrary : config.driver.library);
    if (config.driver.type == VnaServer::DriverType::SIMULATOR) {
        VnaServer::ConfigureSimulatorEnvironment(config.driver.simulator);
        VnaLog::Info(VnaLog::Module::SERVER, "模拟驱动启动配置，mode={}，seed={}，noiseAmplitude={}",
            config.driver.simulator.mode, config.driver.simulator.seed, config.driver.simulator.noiseAmplitude);
    }
}

bool LoadConfiguration(const std::string &path, VnaServer::ServerConfig &config)
{
    try {
        config = VnaServer::LoadServerConfig(path);
        VnaLog::Logger::Instance().Configure(config.log);
        VnaLog::Info(VnaLog::Module::SERVER, "服务配置已加载，config={}", path);
        return true;
    } catch (const std::exception &error) {
        // 此时日志后端尚未就绪，启动诊断直接写终端。
        std::cerr << "Service configuration failed: " << error.what() << '\n';
        return false;
    }
}

bool ApplyStartupSweepMode(VnaService::InstrumentService &service, VnaService::SweepMode mode)
{
    if (mode == VnaService::SweepMode::HOLD) {
        return true;
    }
    const std::string owner = "STARTUP";
    if (mode != VnaService::SweepMode::CONTINUOUS || !service.TryAcquireControl(owner)) {
        return false;
    }
    const auto operation = service.RunContinuous(owner);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto result = operation.get();
    service.ReleaseControl(owner);
    return result.ok;
}

bool ConfigureCalibrationStore(VnaService::InstrumentService &service, const VnaServer::ServerConfig &config)
{
    if (config.server.calibrationStore.empty()) {
        return true;
    }
    const auto &configured = config.server.calibrationStore;
    const auto directory =
        IsAbsolutePath(configured) ? configured : VnaServer::ExecutableDirectory() + "/" + configured;
    auto store = std::make_shared<VnaServer::CalibrationStore>(directory);
    const auto started = std::chrono::steady_clock::now();
    VnaLog::Info(VnaLog::Module::SERVER, "正在恢复 Ch1 校准池，path={}", directory);
    std::vector<VnaService::CalibrationSetPtr> restored;
    std::uint64_t selected = 0U;
    std::string error;
    if (!store->Load(1U, restored, selected, error) || !service.RestoreCalibration(restored, selected, error)) {
        VnaLog::Error(VnaLog::Module::SERVER, "校准池恢复失败，path={}，error={}", directory, error);
        return false;
    }
    for (auto &set : restored) {
        set = service.FindCalibrationSet(set->id, 1U);
    }
    if (!store->AdoptValidated(1U, restored, error)) {
        VnaLog::Error(VnaLog::Module::SERVER, "校准池冻结对象绑定失败，path={}，error={}", directory, error);
        return false;
    }
    service.SetCalibrationSaveHandler(
        [store](unsigned channelId, std::vector<VnaService::CalibrationSetPtr> sets, std::uint64_t selectedId) {
            return std::async(std::launch::async, [store, channelId, sets = std::move(sets), selectedId] {
                VnaService::OperationResult result;
                // 逻辑通道目录尚未跨重启恢复；只持久化启动时始终存在的 Ch1。
                result.ok = channelId != 1U || store->Save(channelId, sets, selectedId, result.message);
                result.errorCode = result.ok ? 0 : -310;
                if (result.ok && !result.message.empty()) {
                    VnaLog::Warn(VnaLog::Module::SERVER, "校准 manifest 已提交，channel={}，warning={}", channelId,
                        result.message);
                }
                return result;
            }).share();
        });
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    VnaLog::Info(VnaLog::Module::SERVER, "校准持久化已就绪，path={}，Ch1 恢复 {} 集，耗时 {} ms，自动校正保持关闭",
        directory, service.Calibration(1U).sets.size(), elapsed);
    return true;
}

int StartService(const VnaServer::DriverOptions &driver, const VnaServer::ListenOptions &options,
    const VnaServer::ServerConfig &config)
{
    VnaLog::Info(VnaLog::Module::SERVER, "服务启动，board={}，driver={}",
        driver.api == VnaCore::DriverApi::TDC1VN10 ? "TDC1VN10" : "TDC1VN08", driver.path);
    VnaCore::AcquisitionCore core(driver.path, driver.api);
    const auto initialized = core.Initialize().get();
    if (!initialized.ok) {
        VnaLog::Error(VnaLog::Module::SERVER, "Driver initialization failed: {}", initialized.message);
        return 1;
    }
    const bool simulator = config.driver.type == VnaServer::DriverType::SIMULATOR;
    VnaService::InstrumentService service(core, config.instrument, simulator);
    std::string bootstrapError;
    if (!service.BootstrapProfile(bootstrapError)) {
        VnaLog::Error(VnaLog::Module::SERVER, "设备初始配置失败：{}", bootstrapError);
        core.Close();
        return 1;
    }
    if (!ConfigureCalibrationStore(service, config)) {
        core.Close();
        return 1;
    }
    // 校准只恢复池与选择；扫描配置仍使用启动默认，不由浏览器读取触发。
    if (!ApplyStartupSweepMode(service, config.defaultSweepMode)) {
        VnaLog::Error(VnaLog::Module::SERVER, "启动缺省扫描模式失败");
        core.Close();
        return 1;
    }
    const int result = VnaServer::RunScpiTcpServer(service, options, [] {
        return g_signalReceived != 0;
    });
    const auto closed = core.Close();
    if (!closed.ok) {
        VnaLog::Error(VnaLog::Module::SERVER, "Driver shutdown failed: {}", closed.message);
        return 1;
    }
    return result;
}
}  // 匿名命名空间

int main(int argc, char **)
{
    if (argc != 1) {
        std::cerr << "不再支持启动参数，请修改程序所在目录的 vnapro.yml\n";
        return 2;
    }
    VnaServer::ListenOptions options;
    VnaServer::DriverOptions driver;
    VnaServer::ServerConfig config;
    try {
        const std::string directory = VnaServer::ExecutableDirectory();
        if (!LoadConfiguration(directory + "/vnapro.yml", config)) {
            return 2;
        }
        ApplyStartupConfiguration(config, directory, options, driver);
    } catch (const std::exception &error) {
        std::cerr << "Startup configuration failed: " << error.what() << '\n';
        VnaLog::Logger::Instance().Shutdown();
        return 2;
    }
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    int result = 1;
    try {
        result = StartService(driver, options, config);
    } catch (const std::exception &error) {
        VnaLog::Error(VnaLog::Module::SERVER, "Service failed: {}", error.what());
    }
    VnaLog::Info(VnaLog::Module::SERVER, "服务退出，exit_code={}", result);
    // StartService 的局部对象已析构，设备 worker 已停止，然后刷新并关闭日志。
    VnaLog::Logger::Instance().Shutdown();
    return result;
}
