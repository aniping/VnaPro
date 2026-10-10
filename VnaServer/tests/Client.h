#ifndef VNAPRO_SERVER_TEST_CLIENT_H
#define VNAPRO_SERVER_TEST_CLIENT_H

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace vna_server_test {

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
    static void expect(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

#ifdef _WIN32
    SOCKET socket_ = INVALID_SOCKET;
#else
    int socket_ = -1;
#endif
};

}  // namespace vna_server_test

#endif
