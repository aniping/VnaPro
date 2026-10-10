/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "network/socket_platform.h"

#include <cerrno>
#include <limits>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vna_server {
namespace network {
int SocketError() noexcept
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool WouldBlock(int error) noexcept
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
#endif
}

void CloseSocket(Socket socket) noexcept
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SetNonBlocking(Socket socket) noexcept
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

std::uint16_t BindListener(Socket socket, const std::string &bindAddress, std::uint16_t port)
{
    constexpr int listenBacklog = 8;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("Bind address must be an IPv4 address");
    }
#ifndef _WIN32
    const int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    // Socket APIs require the generic address view of the same IPv4 structure.
    if (bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(socket, listenBacklog) != 0 || !SetNonBlocking(socket)) {
        throw std::runtime_error("Cannot bind/listen on " + bindAddress + ":" + std::to_string(port));
    }
    SocketLength addressLength = sizeof(address);
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&address), &addressLength) != 0) {
        throw std::runtime_error("Cannot inspect listening socket");
    }
    return ntohs(address.sin_port);
}

void ConfigureStreamSocket(Socket socket) noexcept
{
    const int noDelay = 1;
    // Windows accepts option values as bytes; POSIX accepts the same byte view.
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    const int noSigPipe = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
}

bool ConnectLoopback(Socket socket, std::uint16_t port) noexcept
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    return connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0;
}

int ReceiveSocket(Socket socket, char *buffer, std::size_t capacity) noexcept
{
    if (capacity > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return -1;
    }
    return static_cast<int>(recv(socket, buffer, static_cast<int>(capacity), 0));
}

int SendSocket(Socket socket, const char *buffer, std::size_t length) noexcept
{
    if (length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return -1;
    }
#if defined(MSG_NOSIGNAL) && !defined(_WIN32)
    constexpr int sendFlags = MSG_NOSIGNAL;
#else
    constexpr int sendFlags = 0;
#endif
    return static_cast<int>(send(socket, buffer, static_cast<int>(length), sendFlags));
}

bool FitsSelect(Socket socket) noexcept
{
#ifdef _WIN32
    static_cast<void>(socket);
    return true;
#else
    return socket >= 0 && socket < FD_SETSIZE;
#endif
}
}  // namespace network
}  // namespace vna_server
