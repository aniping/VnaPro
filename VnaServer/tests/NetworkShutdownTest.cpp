#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ScpiTcpServer.h"
#include "../../VnaNativeSimulator/tests/SimulatorEnvironment.h"
#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace {
void expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Client {
public:
    explicit Client(std::uint16_t port)
    {
#ifdef _WIN32
        WSADATA data;
        expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Test network initialization failed");
#endif
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        expect(connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
            "Test controller connection failed");
    }
    ~Client()
    {
#ifdef _WIN32
        closesocket(socket_);
        WSACleanup();
#else
        close(socket_);
#endif
    }
    void write(const std::string& text)
    {
        std::size_t offset = 0;
        while (offset != text.size()) {
            const int count = send(socket_, text.data() + offset, static_cast<int>(text.size() - offset), 0);
            expect(count > 0, "Test write failed");
            offset += static_cast<std::size_t>(count);
        }
    }
    std::string readLine()
    {
        std::string text;
        char value;
        while (recv(socket_, &value, 1, 0) == 1) {
            if (value == '\n') {
                return text;
            }
            text += value;
        }
        throw std::runtime_error("Test response ended early");
    }

private:
#ifdef _WIN32
    SOCKET socket_ = INVALID_SOCKET;
#else
    int socket_ = -1;
#endif
};
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
