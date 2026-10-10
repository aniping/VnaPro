/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "network/network_runtime.h"

#include <stdexcept>

#include "network/socket_platform.h"

namespace VnaServer {
namespace Network {
NetworkRuntime::NetworkRuntime(const std::string &failureMessage)
{
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error(failureMessage);
    }
#else
    static_cast<void>(failureMessage);
#endif
}

NetworkRuntime::~NetworkRuntime() noexcept
{
#ifdef _WIN32
    WSACleanup();
#endif
}
}  // 命名空间 Network
}  // 命名空间 VnaServer
