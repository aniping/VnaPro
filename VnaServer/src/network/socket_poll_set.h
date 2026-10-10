/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_SOCKET_POLL_SET_H
#define VNAPRO_NETWORK_SOCKET_POLL_SET_H

#include "network/socket_platform.h"

namespace VnaServer {
namespace Network {
class SocketPollSet final {
public:
    // Starts an empty readiness set for one executor turn.
    SocketPollSet() noexcept;
    SocketPollSet(const SocketPollSet &) = delete;
    SocketPollSet &operator=(const SocketPollSet &) = delete;
    SocketPollSet(SocketPollSet &&) = delete;
    SocketPollSet &operator=(SocketPollSet &&) = delete;
    // Adds a selectable descriptor; invalid or out-of-capacity handles are ignored.
    void AddReadable(Socket socket) noexcept;
    // Adds a selectable descriptor to the writable set; other handles are ignored.
    void AddWritable(Socket socket) noexcept;
    // Waits for one short executor interval; throws on unrecoverable select errors.
    void Wait();
    // Queries the last Wait result; invalid or out-of-capacity handles return false.
    bool IsReadable(Socket socket) const noexcept;
    bool IsWritable(Socket socket) const noexcept;

private:
    void Track(Socket socket) noexcept;

    fd_set readable_{};
    fd_set writable_{};
    Socket maximum_ = 0;
    bool anySocket_ = false;
};
}  // namespace Network
}  // namespace VnaServer

#endif
