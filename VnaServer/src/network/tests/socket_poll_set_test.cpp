/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "network/socket_poll_set.h"

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include "network/network_runtime.h"
#include "network/socket_owner.h"

namespace {
using VnaServer::Network::INVALID_SOCKET_HANDLE;
using VnaServer::Network::SocketOwner;
using VnaServer::Network::SocketPollSet;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void ImmediateWorkWithoutSocketsHasNoFabricatedEvents()
{
    SocketPollSet events;
    events.AddReadable(INVALID_SOCKET_HANDLE);
    events.AddWritable(INVALID_SOCKET_HANDLE);
    events.RequestImmediate();
    events.Wait();
    Require(!events.IsReadable(INVALID_SOCKET_HANDLE) && !events.IsWritable(INVALID_SOCKET_HANDLE),
        "立即推进 CPU 工作不能伪造无效套接字的就绪事件");
}

void ListenerAndClientShareActualReadiness()
{
    VnaServer::Network::NetworkRuntime runtime;
    SocketOwner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    Require(listener.Get() != INVALID_SOCKET_HANDLE, "创建临时监听套接字");
    const auto port = VnaServer::Network::BindListener(listener.Get(), "127.0.0.1", 0U);
    SocketOwner client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    Require(client.Get() != INVALID_SOCKET_HANDLE && VnaServer::Network::ConnectLoopback(client.Get(), port),
        "连接临时监听器");
    SocketPollSet accepted;
    accepted.AddReadable(listener.Get());
    accepted.Wait();
    Require(accepted.IsReadable(listener.Get()), "监听器必须以真实连接事件唤醒执行器");
    SocketOwner connection(accept(listener.Get(), nullptr, nullptr));
    Require(connection.Get() != INVALID_SOCKET_HANDLE && VnaServer::Network::SetNonBlocking(connection.Get()),
        "接管临时非阻塞连接");
    SocketPollSet idle;
    idle.AddReadable(connection.Get());
    idle.RequestImmediate();
    idle.Wait();
    Require(!idle.IsReadable(connection.Get()), "编码工作可立即推进时，空连接仍不可读");
    SocketPollSet writable;
    writable.AddWritable(connection.Get());
    writable.Wait();
    Require(writable.IsWritable(connection.Get()), "发送待处理响应时应使用真实可写事件");
    Require(VnaServer::Network::SendSocket(client.Get(), "wake", 4U) == 4, "发送真实请求数据");
    SocketPollSet readable;
    readable.AddReadable(connection.Get());
    readable.Wait();
    Require(readable.IsReadable(connection.Get()), "客户端请求必须唤醒共享检测集合");
    char buffer[4]{};
    Require(VnaServer::Network::ReceiveSocket(connection.Get(), buffer, sizeof(buffer)) == 4 &&
                std::string(buffer, sizeof(buffer)) == "wake",
        "就绪检测不能消费或修改原始请求字节");
}

void BackpressureWaitsForPeerConsumption()
{
    VnaServer::Network::NetworkRuntime runtime;
    SocketOwner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    Require(listener.Get() != INVALID_SOCKET_HANDLE, "创建背压测试监听器");
    const auto port = VnaServer::Network::BindListener(listener.Get(), "127.0.0.1", 0U);
    SocketOwner peer(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    Require(peer.Get() != INVALID_SOCKET_HANDLE && VnaServer::Network::ConnectLoopback(peer.Get(), port),
        "建立背压测试接收端");
    SocketOwner sender(accept(listener.Get(), nullptr, nullptr));
    Require(sender.Get() != INVALID_SOCKET_HANDLE && VnaServer::Network::SetNonBlocking(sender.Get()) &&
                VnaServer::Network::SetNonBlocking(peer.Get()),
        "发送端与接收端均使用非阻塞套接字");
    const int bufferSize = 16384;
    Require(setsockopt(sender.Get(), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&bufferSize),
                sizeof(bufferSize)) == 0 &&
                setsockopt(peer.Get(), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&bufferSize),
                    sizeof(bufferSize)) == 0,
        "限制两端缓冲区，使未消费的数据产生真实内核背压");
    VnaServer::Network::ConfigureStreamSocket(sender.Get());
    std::array<char, 16384U> payload{};
    payload.fill('x');
    constexpr std::size_t FILL_BUDGET = 8U * 1024U * 1024U;
    std::size_t sent = 0U;
    bool blocked = false;
    const auto fillDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blocked) {
        Require(std::chrono::steady_clock::now() < fillDeadline && sent < FILL_BUDGET,
            "在有限数据和时间预算内填满真实发送缓冲区");
        const int count = VnaServer::Network::SendSocket(sender.Get(), payload.data(), payload.size());
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        Require(count < 0 && VnaServer::Network::WouldBlock(VnaServer::Network::SocketError()),
            "缓冲区填满后必须实际返回 WouldBlock，而不是连接失败");
        SocketPollSet pending;
        pending.AddWritable(sender.Get());
        pending.Wait();
        // 内核可能先把部分排队数据送入对端缓冲区；继续填充，直到真实可写事件消失。
        blocked = !pending.IsWritable(sender.Get());
    }
    Require(sent > 0U, "背压前确实发送过数据，对端始终没有消费");
    std::size_t consumed = 0U;
    bool resumed = false;
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!resumed) {
        Require(std::chrono::steady_clock::now() < drainDeadline, "对端消费后应恢复发送就绪");
        const int count = VnaServer::Network::ReceiveSocket(peer.Get(), payload.data(), payload.size());
        if (count > 0) {
            consumed += static_cast<std::size_t>(count);
        } else {
            Require(count < 0 && VnaServer::Network::WouldBlock(VnaServer::Network::SocketError()),
                "排空等待期间连接应保持有效");
        }
        SocketPollSet writable;
        writable.AddWritable(sender.Get());
        writable.Wait();
        resumed = consumed > 0U && writable.IsWritable(sender.Get());
    }
    Require(VnaServer::Network::SendSocket(sender.Get(), "r", 1U) == 1, "接收端实际消费后，发送端必须恢复真实字节发送");
}
}  // 匿名命名空间

int main()
{
    try {
        ImmediateWorkWithoutSocketsHasNoFabricatedEvents();
        ListenerAndClientShareActualReadiness();
        BackpressureWaitsForPeerConsumption();
        std::cout << "PASS 立即编码工作、共享就绪唤醒与真实内核背压\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
