#ifndef VNAPRO_NETWORK_SOCKET_OWNER_H
#define VNAPRO_NETWORK_SOCKET_OWNER_H

#include "network/SocketPlatform.h"

namespace vna_server {
namespace network {
class SocketOwner {
public:
    explicit SocketOwner(Socket value = kInvalidSocket);
    ~SocketOwner();
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    Socket get() const;
    void reset(Socket value = kInvalidSocket);

private:
    Socket value_;
};
}  // namespace network
}  // namespace vna_server

#endif
