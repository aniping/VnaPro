/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../../VnaNativeSimulator/tests/scoped_simulator_environment.h"
#include "client.h"
#include "scpi_tcp_server.h"
#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"

namespace {
using vna_server_test::Client;

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void ExerciseNetworkShutdown(vna_service::InstrumentService &service)
{
    std::promise<std::uint16_t> listening;
    auto boundPort = listening.get_future();
    std::atomic<bool> stop{false};
    vna_server::ListenOptions options;
    options.port = 0;
    options.web.port = 0;
    options.onListening = [listeningPromise = &listening](std::uint16_t port) {
        listeningPromise->set_value(port);
    };
    // Both the success and exception paths join the future before these objects die.
    auto server = std::async(std::launch::async, [servicePointer = &service, options, stopPointer = &stop] {
        return vna_server::RunScpiTcpServer(*servicePointer, options, [stopPointer] {
            return stopPointer->load();
        });
    });
    try {
        Expect(boundPort.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "Listener did not start");
        Client client(boundPort.get());
        client.Write("SENS1:SWE:POIN 10001\nSENS1:SWE:MODE SING;*OPC?\n");
        Expect(std::stoi(client.ReadLine()) == 1, "Finite measurement did not finish");
        // Stop with a connected controller while a bounded large response is encoded.
        client.Write("CALC1:DATA? RDATA\n");
        stop.store(true);
        Expect(server.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Network shutdown stalled");
        Expect(server.get() == 0, "Network shutdown failed");
    } catch (...) {
        stop.store(true);
        if (server.valid()) {
            server.wait();
        }
        throw;
    }
}

void RunNetworkShutdown(const std::string &driver)
{
    VnaTest::ScopedSimulatorEnvironment environment("golden", nullptr);
    vna_core::AcquisitionCore core(driver);
    Expect(core.Initialize().get().ok, "Core initialization failed");
    vna_service::InstrumentService service(core);
    std::string error;
    Expect(service.BootstrapMockProfile(error), "Service bootstrap failed");
    ExerciseNetworkShutdown(service);
    Expect(core.Close().ok, "Core close failed");
    Expect(core.ReadView().state == vna_core::CoreState::CLOSED, "Core was not joined/closed");
}
}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    try {
        RunNetworkShutdown(argv[1]);
        std::cout << "Network orderly shutdown passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
