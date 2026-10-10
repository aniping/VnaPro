#include "network/NetworkRuntime.h"

#include <stdexcept>

#include "network/SocketPlatform.h"

namespace vna_server {
namespace network {
NetworkRuntime::NetworkRuntime(const char *failureMessage)
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error(failureMessage);
    }
#else
    (void)failureMessage;
#endif
}

NetworkRuntime::~NetworkRuntime()
{
#ifdef _WIN32
    WSACleanup();
#endif
}
}  // namespace network
}  // namespace vna_server
