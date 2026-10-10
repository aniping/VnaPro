/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_TEST_CLIENT_H
#define VNAPRO_SERVER_TEST_CLIENT_H

#include <cstdint>
#include <string>

#include "network/network_runtime.h"
#include "network/socket_owner.h"

namespace vna_server_test {

class Client final {
public:
    // Connects to a loopback SCPI listener; construction failure releases resources.
    explicit Client(std::uint16_t port);
    // The socket closes before its matching network runtime cleanup.
    ~Client() noexcept = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(Client &&) = delete;
    // Sends all bytes or throws if the connection fails.
    void Write(const std::string &text);
    // Returns one response line without LF; throws on premature EOF.
    std::string ReadLine();

private:
    vna_server::network::NetworkRuntime runtime_;
    vna_server::network::SocketOwner socket_;
};

}  // namespace vna_server_test

#endif
