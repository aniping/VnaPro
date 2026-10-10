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
    // 为一轮执行创建空的就绪检测集合。
    SocketPollSet() noexcept;
    SocketPollSet(const SocketPollSet &) = delete;
    SocketPollSet &operator=(const SocketPollSet &) = delete;
    SocketPollSet(SocketPollSet &&) = delete;
    SocketPollSet &operator=(SocketPollSet &&) = delete;
    // 添加可供 select 检测的描述符；忽略无效或超出集合容量的句柄。
    void AddReadable(Socket socket) noexcept;
    // 将可供 select 检测的描述符加入可写集合；忽略其他句柄。
    void AddWritable(Socket socket) noexcept;
    // 等待一个较短的执行周期；select 出现不可恢复错误时抛出异常。
    void Wait();
    // 查询上次 Wait 的结果；句柄无效或超出集合容量时返回假。
    bool IsReadable(Socket socket) const noexcept;
    bool IsWritable(Socket socket) const noexcept;

private:
    void Track(Socket socket) noexcept;

    fd_set readable_{};
    fd_set writable_{};
    Socket maximum_ = 0;
    bool anySocket_ = false;
};
}  // 命名空间 Network
}  // 命名空间 VnaServer

#endif
