#include "ScpiTcpServer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "scpi/ScpiEngine.h"

namespace vna_server {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
int SocketError()
{
    return WSAGetLastError();
}
bool WouldBlock(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
void CloseSocket(Socket socket)
{
    closesocket(socket);
}
bool SetNonBlocking(Socket socket)
{
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
using SocketLength = int;
struct NetworkRuntime {
    NetworkRuntime()
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~NetworkRuntime() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
int SocketError()
{
    return errno;
}
bool WouldBlock(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}
void CloseSocket(Socket socket)
{
    close(socket);
}
bool SetNonBlocking(Socket socket)
{
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
using SocketLength = socklen_t;
struct NetworkRuntime {};
#endif

class SocketOwner {
public:
    explicit SocketOwner(Socket value = kInvalidSocket) : value_(value) {}
    ~SocketOwner() { reset(); }
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    Socket get() const { return value_; }
    void reset(Socket value = kInvalidSocket)
    {
        if (value_ != kInvalidSocket) {
            CloseSocket(value_);
        }
        value_ = value;
    }

private:
    Socket value_;
};

constexpr std::size_t kInputLimit = 16U * 1024U;
constexpr std::size_t kOutputLimit = 1024U * 1024U;
using Clock = std::chrono::steady_clock;

bool FitsSelect(Socket value)
{
#ifdef _WIN32
    (void)value;
    return true;
#else
    return value >= 0 && value < FD_SETSIZE;
#endif
}
}  // namespace

int RunScpiTcpServer(vna_service::InstrumentService& service, const ListenOptions& options,
    const std::function<bool()>& shutdownRequested)
{
    NetworkRuntime runtime;
    (void)runtime;
    SocketOwner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener.get() == kInvalidSocket) {
        throw std::runtime_error("Cannot create listening socket");
    }
    if (!FitsSelect(listener.get())) {
        throw std::runtime_error("Listening descriptor exceeds select capacity");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.bindAddress.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("Bind address must be an IPv4 address");
    }
#ifndef _WIN32
    const int reuse = 1;
    setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.get(), 8) != 0 || !SetNonBlocking(listener.get())) {
        throw std::runtime_error("Cannot bind/listen on " + options.bindAddress + ":" + std::to_string(options.port));
    }
    SocketLength addressLength = sizeof(address);
    if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
        throw std::runtime_error("Cannot inspect listening socket");
    }
    std::cout << "LISTENING " << options.bindAddress << ':' << ntohs(address.sin_port) << std::endl;
    if (options.onListening) {
        options.onListening(ntohs(address.sin_port));
    }
    vna_web::WebEndpoint web(service, options.web);
    SocketOwner controller;
    std::unique_ptr<vna_scpi::ScpiEngine> engine;
    std::string input;
    std::string output;
    std::size_t sent = 0;
    auto lastSendProgress = Clock::now();
    bool shuttingDown = false;
    auto disconnect = [&] {
        controller.reset();
        input.clear();
        output.clear();
        sent = 0;
        if (engine) {
            engine->disconnect();
        }
    };
    while (true) {
        if (!shuttingDown && shutdownRequested()) {
            shuttingDown = true;
            listener.reset();
            disconnect();
            web.beginShutdown();
        }
        web.poll();
        if (engine) {
            engine->setTransportOutputPending(output.size() != sent);
            engine->poll();
            while (true) {
                std::string response = engine->takeOutput();
                if (response.empty()) {
                    break;
                }
                if (controller.get() == kInvalidSocket) {
                    continue;
                }
                if (response.size() > kOutputLimit - (output.size() - sent)) {
                    disconnect();
                    break;
                }
                if (sent != 0) {
                    output.erase(0, sent);
                    sent = 0;
                }
                if (output.empty()) {
                    lastSendProgress = Clock::now();
                }
                output += response;
            }
            if (controller.get() == kInvalidSocket && engine->canReleaseControl()) {
                engine.reset();
            }
        }
        if (shuttingDown && !engine && web.drained()) {
            return 0;
        }
        if (controller.get() != kInvalidSocket && output.size() != sent &&
            Clock::now() - lastSendProgress > std::chrono::seconds(10)) {
            disconnect();
        }
        fd_set readable;
        fd_set writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        Socket maximum = 0;
        bool anySocket = false;
        auto addReadable = [&](Socket value) {
            if (value != kInvalidSocket) {
                FD_SET(value, &readable);
                if (value > maximum) {
                    maximum = value;
                }
                anySocket = true;
            }
        };
        addReadable(listener.get());
        if (output.size() - sent < kOutputLimit / 2) {
            addReadable(controller.get());
        }
        if (controller.get() != kInvalidSocket && output.size() > sent) {
            FD_SET(controller.get(), &writable);
            maximum = controller.get() > maximum ? controller.get() : maximum;
            anySocket = true;
        }
        if (!anySocket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        timeval timeout{};
        timeout.tv_usec = 5000;
        const int ready = select(static_cast<int>(maximum + 1), &readable, &writable, nullptr, &timeout);
        if (ready < 0) {
            if (WouldBlock(SocketError())) {
                continue;
            }
            throw std::runtime_error("Socket select failed");
        }
        if (listener.get() != kInvalidSocket && FD_ISSET(listener.get(), &readable)) {
            const Socket incoming = accept(listener.get(), nullptr, nullptr);
            if (incoming != kInvalidSocket) {
                if (engine || !FitsSelect(incoming) || !SetNonBlocking(incoming)) {
                    CloseSocket(incoming);
                } else {
                    controller.reset(incoming);
                    const int noDelay = 1;
                    setsockopt(
                        incoming, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
                    const int noSigPipe = 1;
                    setsockopt(incoming, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
                    engine.reset(new vna_scpi::ScpiEngine(service));
                    if (!engine->hasControl()) {
                        disconnect();
                    }
                }
            }
        }
        if (controller.get() != kInvalidSocket && FD_ISSET(controller.get(), &readable)) {
            char buffer[4096];
            const int received = recv(controller.get(), buffer, sizeof(buffer), 0);
            if (received > 0) {
                input.append(buffer, static_cast<std::size_t>(received));
                std::size_t boundary;
                while ((boundary = input.find('\n')) != std::string::npos) {
                    if (boundary > kInputLimit) {
                        disconnect();
                        break;
                    }
                    std::string line = input.substr(0, boundary);
                    input.erase(0, boundary + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty() && !engine->submitLine(line)) {
                        disconnect();
                        break;
                    }
                }
                if (input.size() > kInputLimit) {
                    disconnect();
                }
            } else if (received == 0 || !WouldBlock(SocketError())) {
                disconnect();
            }
        }
        if (controller.get() != kInvalidSocket && FD_ISSET(controller.get(), &writable)) {
            const auto remaining = output.size() - sent;
#if defined(MSG_NOSIGNAL) && !defined(_WIN32)
            constexpr int sendFlags = MSG_NOSIGNAL;
#else
            constexpr int sendFlags = 0;
#endif
            const int written = send(controller.get(), output.data() + sent, static_cast<int>(remaining), sendFlags);
            if (written > 0) {
                sent += static_cast<std::size_t>(written);
                lastSendProgress = Clock::now();
                if (sent == output.size()) {
                    output.clear();
                    sent = 0;
                }
            } else if (written == 0 || !WouldBlock(SocketError())) {
                disconnect();
            }
        }
    }
}
}  // namespace vna_server
