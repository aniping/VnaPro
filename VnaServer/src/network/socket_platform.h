/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_SOCKET_PLATFORM_H
#define VNAPRO_NETWORK_SOCKET_PLATFORM_H

#include <cstddef>
#include <cstdint>
#include <string>

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
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace vna_server {
namespace network {
#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket INVALID_SOCKET_HANDLE = -1;
#endif

// Reports the most recent platform socket error for the calling thread.
int SocketError() noexcept;
// Includes interrupted calls, which can be retried by the executor.
bool WouldBlock(int error) noexcept;
// Closes a valid borrowed handle; ownership must be transferred before calling.
void CloseSocket(Socket socket) noexcept;
// Enables nonblocking I/O; false indicates a platform call failure.
bool SetNonBlocking(Socket socket) noexcept;
// Binds an IPv4 stream listener and returns its actual port; throws on failure.
std::uint16_t BindListener(Socket socket, const std::string &bindAddress, std::uint16_t port);
// Configures low-latency writes and suppression of SIGPIPE where supported.
void ConfigureStreamSocket(Socket socket) noexcept;
// Connects an existing stream socket to loopback; false indicates failure.
bool ConnectLoopback(Socket socket, std::uint16_t port) noexcept;
// Receives at most capacity bytes. Returns zero on EOF or -1 on platform failure.
int ReceiveSocket(Socket socket, char *buffer, std::size_t capacity) noexcept;
// Sends a bounded byte range without SIGPIPE; returns the platform byte count.
int SendSocket(Socket socket, const char *buffer, std::size_t length) noexcept;
// POSIX descriptors must fit select's bitset; Windows uses a counted socket array.
bool FitsSelect(Socket socket) noexcept;
}  // namespace network
}  // namespace vna_server

#endif
