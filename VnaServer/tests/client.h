/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_TEST_CLIENT_H
#define VNAPRO_SERVER_TEST_CLIENT_H

#include <cstdint>
#include <string>

#include "network/network_runtime.h"
#include "network/socket_owner.h"

namespace VnaServerTest {

class Client final {
public:
    // 连接本机回环地址的 SCPI 监听端口；构造失败时释放资源。
    explicit Client(std::uint16_t port);
    // 先关闭套接字，再清理对应的网络运行环境。
    ~Client() noexcept = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(Client &&) = delete;
    // 发送全部字节；连接失败时抛出异常。
    void Write(const std::string &text);
    // 返回一行不含 LF 的响应；连接提前结束时抛出异常。
    std::string ReadLine();

private:
    VnaServer::Network::NetworkRuntime runtime_;
    VnaServer::Network::SocketOwner socket_;
};

}  // 命名空间 VnaServerTest

#endif
