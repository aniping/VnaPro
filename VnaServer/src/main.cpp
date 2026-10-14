/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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
    const char *defaultLibrary = driver.api == VnaCore::DriverApi::TDC1VN10 ? VnaServer::DEFAULT_VN10_DRIVER_FILENAME
                                                                            : VnaServer::DEFAULT_VN08_DRIVER_FILENAME;
    driver.path = directory + "/" + (config.driver.library.empty() ? defaultLibrary : config.driver.library);
    VnaServer::ConfigureSimulatorEnvironment(config.driver.simulator);
    VnaLog::Info(VnaLog::Module::SERVER, "模拟驱动启动配置，mode={}，seed={}", config.driver.simulator.mode,
        config.driver.simulator.seed);
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
    if (core.ReadView().deviceIdn.find("VnaPro,NativeSimulator,") != 0U) {
        VnaLog::Error(VnaLog::Module::SERVER, "设备身份不符合已验证的 VnaPro 模拟器合同");
        core.Close();
        return 1;
    }
    VnaService::InstrumentService service(core, config.instrument);
    std::string bootstrapError;
    if (!service.BootstrapMockProfile(bootstrapError)) {
        VnaLog::Error(VnaLog::Module::SERVER, "Mock configuration failed: {}", bootstrapError);
        core.Close();
        return 1;
    }
    // 当前尚未持久化仪器状态；缺省扫描只在监听前执行一次，不由浏览器读取触发。
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
