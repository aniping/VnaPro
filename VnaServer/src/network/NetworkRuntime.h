#ifndef VNAPRO_NETWORK_NETWORK_RUNTIME_H
#define VNAPRO_NETWORK_NETWORK_RUNTIME_H

namespace vna_server {
namespace network {
// Each runtime owns one successful startup and its matching cleanup.
class NetworkRuntime {
public:
    explicit NetworkRuntime(const char* failureMessage = "WSAStartup failed");
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
};
}  // namespace network
}  // namespace vna_server

#endif
