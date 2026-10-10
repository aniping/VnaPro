/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_SOCKET_PLATFORM_H
#define VNAPRO_NETWORK_SOCKET_PLATFORM_H

#include <cstddef>
#include <cstdint>
#include <string>

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

namespace VnaServer {
namespace Network {
#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket INVALID_SOCKET_HANDLE = -1;
#endif

// 返回调用线程最近一次套接字操作的平台错误码。
int SocketError() noexcept;
// 也包含被中断的调用，执行线程可以重试这些调用。
bool WouldBlock(int error) noexcept;
// 关闭有效的借用句柄；调用前必须完成所有权移交，避免重复关闭。
void CloseSocket(Socket socket) noexcept;
// 启用非阻塞 I/O；平台调用失败时返回假。
bool SetNonBlocking(Socket socket) noexcept;
// 绑定 IPv4 流式监听套接字并返回实际端口；失败时抛出异常。
std::uint16_t BindListener(Socket socket, const std::string &bindAddress, std::uint16_t port);
// 配置低延迟发送，并在平台支持时屏蔽 SIGPIPE。
void ConfigureStreamSocket(Socket socket) noexcept;
// 将已有的流式套接字连接到回环地址；失败时返回假。
bool ConnectLoopback(Socket socket, std::uint16_t port) noexcept;
// 最多接收 capacity 个字节；到达流末尾时返回零，平台调用失败时返回 -1。
int ReceiveSocket(Socket socket, char *buffer, std::size_t capacity) noexcept;
// 发送长度受限的字节区间并屏蔽 SIGPIPE，返回平台报告的发送字节数。
int SendSocket(Socket socket, const char *buffer, std::size_t length) noexcept;
// POSIX 描述符必须落在 select 位集合范围内；Windows 使用带计数的套接字数组。
bool FitsSelect(Socket socket) noexcept;
}  // 命名空间 Network
}  // 命名空间 VnaServer

#endif
