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
    // 独占接管 value 的所有权；无效句柄表示未持有套接字。
    explicit SocketOwner(Socket value = INVALID_SOCKET_HANDLE) noexcept;
    // 关闭持有的套接字，不抛出异常。
    ~SocketOwner() noexcept;
    SocketOwner(const SocketOwner &) = delete;
    SocketOwner &operator=(const SocketOwner &) = delete;
    // 转移所有权，并将源对象置为无效状态。
    SocketOwner(SocketOwner &&other) noexcept;
    // 先关闭原有套接字，再转移所有权。
    SocketOwner &operator=(SocketOwner &&other) noexcept;
    // 返回借用的句柄，调用方不得关闭该句柄。
    Socket Get() const noexcept;
    // 关闭原有句柄，并独占接管 value 的所有权。
    void Reset(Socket value = INVALID_SOCKET_HANDLE) noexcept;

private:
    Socket value_;
};
}  // 命名空间 Network
}  // 命名空间 VnaServer

#endif
