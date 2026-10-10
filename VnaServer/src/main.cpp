/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "scpi_tcp_server.h"
#include "server_defaults.h"
#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"

namespace {
// Signal handlers may only assign a volatile sig_atomic_t, not access service state.
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
                 "                  [--web-bind IPv4] [--web-port 0..65535] [--web-root directory] [--no-web]\n"
                 "Defaults: SCPI 127.0.0.1:5025, Web http://127.0.0.1:8080.\n"
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

bool ParseOption(
    const std::string &argument, const std::string &value, vna_server::ListenOptions &options, std::string &driver)
{
    if (argument == "--bind") {
        options.bindAddress = value;
    } else if (argument == "--web-bind") {
        options.web.bindAddress = value;
    } else if (argument == "--web-root") {
        options.web.staticRoot = value;
    } else if (argument == "--driver") {
        driver = value;
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

ArgumentResult ParseArguments(
    int argc, const char * const *argv, vna_server::ListenOptions &options, std::string &driver)
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
        if (!ParseOption(argument, argv[index], options, driver)) {
            return ArgumentResult::ERROR;
        }
    }
    return ArgumentResult::READY;
}

int StartService(const std::string &driver, const vna_server::ListenOptions &options)
{
    vna_core::AcquisitionCore core(driver);
    const auto initialized = core.Initialize().get();
    if (!initialized.ok) {
        std::cerr << "Driver initialization failed: " << initialized.message << '\n';
        return 1;
    }
    if (core.ReadView().deviceIdn.find("VnaPro,NativeSimulator,") != 0U) {
        std::cerr << "The service only supports the verified VnaPro mock unit/receiver profile\n";
        core.Close();
        return 1;
    }
    vna_service::InstrumentService service(core);
    std::string bootstrapError;
    if (!service.BootstrapMockProfile(bootstrapError)) {
        std::cerr << "Mock configuration failed: " << bootstrapError << '\n';
        core.Close();
        return 1;
    }
    const int result = vna_server::RunScpiTcpServer(service, options, [] {
        return g_signalReceived != 0;
    });
    const auto closed = core.Close();
    if (!closed.ok) {
        std::cerr << "Driver shutdown failed: " << closed.message << '\n';
        return 1;
    }
    return result;
}
}  // namespace

int main(int argc, char **argv)
{
    vna_server::ListenOptions options;
    options.web.staticRoot = vna_server::DEFAULT_WEB_ROOT_PATH;
    std::string driver = vna_server::DEFAULT_DRIVER_PATH;
    const ArgumentResult parsed = ParseArguments(argc, argv, options, driver);
    if (parsed == ArgumentResult::HELP) {
        PrintHelp();
        return 0;
    }
    if (parsed == ArgumentResult::ERROR) {
        return 2;
    }
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    try {
        return StartService(driver, options);
    } catch (const std::exception &error) {
        std::cerr << "Service failed: " << error.what() << '\n';
        return 1;
    }
}
