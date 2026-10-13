/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "driver_options.h"
#include "scpi_tcp_server.h"
#include "server_config_loader.h"
#include "server_defaults.h"
#include "startup_paths.h"
#include "utf8_argument.h"
#include "vna_core/acquisition_core.h"
#include "vna_log/logger.h"
#include "vna_service/instrument_service.h"

namespace {
// 信号处理函数只能为 volatile sig_atomic_t 赋值，不能访问服务状态。
volatile std::sig_atomic_t g_signalReceived = 0;
constexpr long TCP_PORT_MAXIMUM = 65535;
enum class ArgumentResult {
    READY,
    HELP,
    ERROR
};

void HandleSignal(int) noexcept
{
    g_signalReceived = 1;
}

void PrintHelp()
{
    std::cout << "VnaPro mock SCPI and Web service\n"
                 "Usage: vna_server [--bind IPv4] [--port 0..65535] [--driver absolute-simulator-path]\n"
                 "                  [--board tdc1vn10|tdc1vn08]\n"
                 "                  [--web-bind IPv4] [--web-port 0..65535] [--web-root directory] [--no-web]\n"
                 "                  [--config yaml-file]\n"
                 "Defaults: TDC1VN10 simulator, SCPI 127.0.0.1:5025, Web http://127.0.0.1:8080.\n"
                 "Simulators: tdc1vn08_drv_sim and tdc1vn10_drv_sim.\n"
                 "--board selects the API and default library; an explicit --driver path overrides that library.\n"
                 "Packaged config, WebUI, and libraries are resolved beside vna_server; explicit paths take priority.\n"
                 "Port 0 selects an available port. Only the VnaPro mock profile is implemented.\n";
}

bool ParsePort(const std::string &value, std::uint16_t &port)
{
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (value.empty() || *end != '\0' || parsed < 0 || parsed > TCP_PORT_MAXIMUM) {
        std::cerr << "Invalid port\n";
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseOption(const std::string &argument, const std::string &value, VnaServer::ListenOptions &options,
    VnaServer::DriverOptions &driver, std::string &configPath)
{
    if (argument == "--bind") {
        options.bindAddress = value;
    } else if (argument == "--web-bind") {
        options.web.bindAddress = value;
    } else if (argument == "--web-root") {
        options.web.staticRoot = value;
    } else if (argument == "--driver") {
        driver.path = value;
    } else if (argument == "--board") {
        if (value == "tdc1vn10") {
            driver.api = VnaCore::DriverApi::TDC1VN10;
        } else if (value == "tdc1vn08") {
            driver.api = VnaCore::DriverApi::TDC1VN08;
        } else {
            std::cerr << "Invalid board: " << value << '\n';
            return false;
        }
    } else if (argument == "--config") {
        configPath = value;
    } else if (argument == "--port") {
        return ParsePort(value, options.port);
    } else if (argument == "--web-port") {
        return ParsePort(value, options.web.port);
    } else {
        std::cerr << "Unknown option: " << argument << '\n';
        return false;
    }
    return true;
}

ArgumentResult ParseArguments(int argc, const char * const *argv, VnaServer::ListenOptions &options,
    VnaServer::DriverOptions &driver, std::string &configPath)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            return ArgumentResult::HELP;
        }
        if (argument == "--no-web") {
            options.web.enabled = false;
            continue;
        }
        if (index + 1 >= argc) {
            std::cerr << "Missing option value: " << argument << '\n';
            return ArgumentResult::ERROR;
        }
        ++index;
        try {
            const std::string value = (argument == "--config" || argument == "--driver" || argument == "--web-root")
                                          ? VnaServer::Utf8Argument(index, argv[index])
                                          : argv[index];
            if (!ParseOption(argument, value, options, driver, configPath)) {
                return ArgumentResult::ERROR;
            }
        } catch (const std::exception &error) {
            // 日志初始化前的路径编码错误仍按参数错误返回，避免直接终止进程。
            std::cerr << "Invalid option value: " << argument << ": " << error.what() << '\n';
            return ArgumentResult::ERROR;
        }
    }
    return ArgumentResult::READY;
}

void ApplyDefaultPaths(VnaServer::ListenOptions &options, VnaServer::DriverOptions &driver, std::string &configPath)
{
    const std::string directory = VnaServer::ExecutableDirectory();
    if (configPath.empty()) {
        configPath = VnaServer::DefaultStartupPath(directory, "vnapro.yml", VnaServer::DEFAULT_CONFIG_PATH);
    }
    if (options.web.staticRoot.empty()) {
        options.web.staticRoot = VnaServer::DefaultStartupPath(
            directory, "WebUI", VnaServer::DEFAULT_WEB_ROOT_PATH, VnaServer::StartupPathKind::DIRECTORY);
    }
    // 最后选择默认库，保证 --board 与 --driver 的参数顺序不影响结果。
    if (driver.path.empty()) {
        const bool vn10 = driver.api == VnaCore::DriverApi::TDC1VN10;
        driver.path = VnaServer::DefaultStartupPath(directory,
            vn10 ? VnaServer::DEFAULT_VN10_DRIVER_FILENAME : VnaServer::DEFAULT_VN08_DRIVER_FILENAME,
            vn10 ? VnaServer::DEFAULT_VN10_DRIVER_PATH : VnaServer::DEFAULT_VN08_DRIVER_PATH);
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

int main(int argc, char **argv)
{
    VnaServer::ListenOptions options;
    VnaServer::DriverOptions driver;
    std::string configPath;
    const ArgumentResult parsed = ParseArguments(argc, argv, options, driver, configPath);
    if (parsed == ArgumentResult::HELP) {
        PrintHelp();
        return 0;
    }
    if (parsed == ArgumentResult::ERROR) {
        return 2;
    }
    try {
        ApplyDefaultPaths(options, driver, configPath);
    } catch (const std::exception &error) {
        std::cerr << "Startup resource resolution failed: " << error.what() << '\n';
        return 2;
    }
    VnaServer::ServerConfig config;
    if (!LoadConfiguration(configPath, config)) {
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
