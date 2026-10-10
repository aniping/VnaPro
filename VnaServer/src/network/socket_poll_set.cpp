/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "network/socket_poll_set.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace VnaServer {
namespace Network {
namespace {
constexpr int EXECUTOR_INTERVAL_MICROSECONDS = 5000;
}

SocketPollSet::SocketPollSet() noexcept
{
    FD_ZERO(&readable_);
    FD_ZERO(&writable_);
}

void SocketPollSet::Track(Socket socket) noexcept
{
    if (socket > maximum_) {
        maximum_ = socket;
    }
    anySocket_ = true;
}

void SocketPollSet::AddReadable(Socket socket) noexcept
{
    if (socket != INVALID_SOCKET_HANDLE && FitsSelect(socket)) {
        FD_SET(socket, &readable_);
        Track(socket);
    }
}

void SocketPollSet::AddWritable(Socket socket) noexcept
{
    if (socket != INVALID_SOCKET_HANDLE && FitsSelect(socket)) {
        FD_SET(socket, &writable_);
        Track(socket);
    }
}

void SocketPollSet::Wait()
{
    if (!anySocket_) {
        std::this_thread::sleep_for(std::chrono::microseconds(EXECUTOR_INTERVAL_MICROSECONDS));
        return;
    }
    timeval timeout{};
    timeout.tv_usec = EXECUTOR_INTERVAL_MICROSECONDS;
#ifdef _WIN32
    // Winsock ignores nfds; avoid narrowing its pointer-width socket handles.
    const int descriptorCount = 0;
#else
    const int descriptorCount = maximum_ + 1;
#endif
    const int ready = select(descriptorCount, &readable_, &writable_, nullptr, &timeout);
    if (ready < 0) {
        if (!WouldBlock(SocketError())) {
            throw std::runtime_error("Socket select failed");
        }
        FD_ZERO(&readable_);
        FD_ZERO(&writable_);
    }
}

bool SocketPollSet::IsReadable(Socket socket) const noexcept
{
    return socket != INVALID_SOCKET_HANDLE && FitsSelect(socket) && FD_ISSET(socket, &readable_);
}

bool SocketPollSet::IsWritable(Socket socket) const noexcept
{
    return socket != INVALID_SOCKET_HANDLE && FitsSelect(socket) && FD_ISSET(socket, &writable_);
}
}  // namespace Network
}  // namespace VnaServer
