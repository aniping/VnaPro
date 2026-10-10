#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../../VnaNativeSimulator/tests/SimulatorEnvironment.h"
#include "Client.h"
#include "ScpiTcpServer.h"
#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace {
using vna_server_test::Client;

void expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        return 2;
    }
    try {
        vna_test::ScopedSimulatorEnvironment environment("golden", NULL);
        vna_core::AcquisitionCore core(argv[1]);
        expect(core.initialize().get().ok, "Core initialization failed");
        vna_service::InstrumentService service(core);
        std::string error;
        expect(service.bootstrapMockProfile(error), "Service bootstrap failed");
        std::promise<std::uint16_t> listening;
        auto boundPort = listening.get_future();
        std::atomic<bool> stop{false};
        vna_server::ListenOptions options;
        options.port = 0;
        options.web.port = 0;
        options.onListening = [&](std::uint16_t port) { listening.set_value(port); };
        auto server = std::async(std::launch::async,
            [&] { return vna_server::RunScpiTcpServer(service, options, [&] { return stop.load(); }); });
        try {
            expect(boundPort.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "Listener did not start");
            Client client(boundPort.get());
            client.write("SENS1:SWE:POIN 10001\nSENS1:SWE:MODE SING;*OPC?\n");
            expect(std::stoi(client.readLine()) == 1, "Finite measurement did not finish");
            // Request a bounded large encoding task, then stop with a controller
            // still connected. Shutdown must detach it and finish accepted work.
            client.write("CALC1:DATA? RDATA\n");
            stop.store(true);
            expect(server.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Network shutdown stalled");
            expect(server.get() == 0, "Network shutdown failed");
        } catch (...) {
            stop.store(true);
            if (server.valid()) {
                server.wait();
            }
            throw;
        }
        expect(core.close().ok, "Core close failed");
        expect(core.readView().state == vna_core::CoreState::Closed, "Core was not joined/closed");
        std::cout << "Network orderly shutdown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
