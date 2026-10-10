/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NETWORK_NETWORK_RUNTIME_H
#define VNAPRO_NETWORK_NETWORK_RUNTIME_H

#include <string>

namespace VnaServer {
namespace Network {
// 每个运行时对象负责一次成功初始化及其对应的清理。
class NetworkRuntime final {
public:
    // 初始化进程的网络子系统；失败时抛出异常。
    explicit NetworkRuntime(const std::string &failureMessage = "WSAStartup failed");
    // 在 Windows 上，为每次成功初始化执行对应的清理。
    ~NetworkRuntime() noexcept;
    NetworkRuntime(const NetworkRuntime &) = delete;
    NetworkRuntime &operator=(const NetworkRuntime &) = delete;
    NetworkRuntime(NetworkRuntime &&) = delete;
    NetworkRuntime &operator=(NetworkRuntime &&) = delete;
};
}  // 命名空间 Network
}  // 命名空间 VnaServer

#endif
