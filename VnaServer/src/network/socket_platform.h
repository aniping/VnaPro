#ifndef VNAPRO_NETWORK_SOCKET_PLATFORM_H
#define VNAPRO_NETWORK_SOCKET_PLATFORM_H

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
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket kInvalidSocket = -1;
#endif

int SocketError();
bool WouldBlock(int error);
void CloseSocket(Socket socket);
bool SetNonBlocking(Socket socket);
}  // namespace network
}  // namespace vna_server

#endif
