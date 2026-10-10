#include "network/socket_platform.h"

#include <cerrno>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vna_server {
namespace network {
int SocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool WouldBlock(int error)
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
#endif
}

void CloseSocket(Socket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SetNonBlocking(Socket socket)
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
}  // namespace network
}  // namespace vna_server
