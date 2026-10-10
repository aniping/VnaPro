/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_SOCKET_OWNER_H
#define VNAPRO_NETWORK_SOCKET_OWNER_H

#include "network/socket_platform.h"

namespace VnaServer {
namespace Network {
class SocketOwner final {
public:
    // Takes sole ownership of value; an invalid handle represents no socket.
    explicit SocketOwner(Socket value = INVALID_SOCKET_HANDLE) noexcept;
    // Closes an owned socket without throwing.
    ~SocketOwner() noexcept;
    SocketOwner(const SocketOwner &) = delete;
    SocketOwner &operator=(const SocketOwner &) = delete;
    // Transfers ownership, leaving the source invalid.
    SocketOwner(SocketOwner &&other) noexcept;
    // Closes the prior socket before transferring ownership.
    SocketOwner &operator=(SocketOwner &&other) noexcept;
    // Returns a borrowed handle; the caller must not close it.
    Socket Get() const noexcept;
    // Closes the prior handle and takes sole ownership of value.
    void Reset(Socket value = INVALID_SOCKET_HANDLE) noexcept;

private:
    Socket value_;
};
}  // namespace Network
}  // namespace VnaServer

#endif
