/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "network/socket_owner.h"

namespace vna_server {
namespace network {
SocketOwner::SocketOwner(Socket value) noexcept : value_(value)
{
}

SocketOwner::~SocketOwner() noexcept
{
    Reset();
}

SocketOwner::SocketOwner(SocketOwner &&other) noexcept : value_(other.value_)
{
    other.value_ = INVALID_SOCKET_HANDLE;
}

SocketOwner &SocketOwner::operator=(SocketOwner &&other) noexcept
{
    if (this != &other) {
        Reset(other.value_);
        other.value_ = INVALID_SOCKET_HANDLE;
    }
    return *this;
}

Socket SocketOwner::Get() const noexcept
{
    return value_;
}

void SocketOwner::Reset(Socket value) noexcept
{
    if (value == value_) {
        return;
    }
    if (value_ != INVALID_SOCKET_HANDLE) {
        CloseSocket(value_);
    }
    value_ = value;
}
}  // namespace network
}  // namespace vna_server
