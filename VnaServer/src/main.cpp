#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "ScpiTcpServer.h"
#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace {
volatile std::sig_atomic_t signalReceived = 0;

void HandleSignal(int)
{
    signalReceived = 1;
}
}  // namespace

int main(int argc, char** argv)
{
    vna_server::ListenOptions options;
    std::string driver = VNA_DEFAULT_DRIVER;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            std::cout << "VnaPro mock TCP SCPI service\n"
                         "Usage: vna_server [--bind IPv4] [--port 0..65535] [--driver absolute-simulator-path]\n"
                         "Port 0 selects an available port. Only the VnaPro mock profile is implemented.\n";
            return 0;
        }
        if (index + 1 >= argc) {
            std::cerr << "Missing option value: " << argument << '\n';
            return 2;
        }
        const std::string value = argv[++index];
        if (argument == "--bind") {
            options.bindAddress = value;
        } else if (argument == "--driver") {
            driver = value;
        } else if (argument == "--port") {
            char* end = nullptr;
            const long port = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0' || port < 0 || port > 65535) {
                std::cerr << "Invalid port\n";
                return 2;
            }
            options.port = static_cast<std::uint16_t>(port);
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
            return 2;
        }
    }
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    try {
        vna_core::AcquisitionCore core(driver);
        const auto initialized = core.initialize().get();
        if (!initialized.ok) {
            std::cerr << "Driver initialization failed: " << initialized.message << '\n';
            return 1;
        }
        if (core.readView().deviceIdn.find("VnaPro,NativeSimulator,") != 0) {
            std::cerr << "The service only supports the verified VnaPro mock unit/receiver profile\n";
            core.close();
            return 1;
        }
        vna_service::InstrumentService service(core);
        std::string bootstrapError;
        if (!service.bootstrapMockProfile(bootstrapError)) {
            std::cerr << "Mock configuration failed: " << bootstrapError << '\n';
            core.close();
            return 1;
        }
        const int result = vna_server::RunScpiTcpServer(service, options, [] { return signalReceived != 0; });
        const auto closed = core.close();
        if (!closed.ok) {
            std::cerr << "Driver shutdown failed: " << closed.message << '\n';
            return 1;
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Service failed: " << error.what() << '\n';
        return 1;
    }
}
