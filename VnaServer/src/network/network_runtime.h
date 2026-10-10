/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_NETWORK_RUNTIME_H
#define VNAPRO_NETWORK_NETWORK_RUNTIME_H

#include <string>

namespace vna_server {
namespace network {
// Each runtime owns one successful startup and its matching cleanup.
class NetworkRuntime final {
public:
    // Starts the process network subsystem; throws if startup fails.
    explicit NetworkRuntime(const std::string &failureMessage = "WSAStartup failed");
    // Pairs each successful startup with cleanup on Windows.
    ~NetworkRuntime() noexcept;
    NetworkRuntime(const NetworkRuntime &) = delete;
    NetworkRuntime &operator=(const NetworkRuntime &) = delete;
    NetworkRuntime(NetworkRuntime &&) = delete;
    NetworkRuntime &operator=(NetworkRuntime &&) = delete;
};
}  // namespace network
}  // namespace vna_server

#endif
