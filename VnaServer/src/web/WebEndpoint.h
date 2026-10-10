#ifndef VNAPRO_WEB_WEB_ENDPOINT_H
#define VNAPRO_WEB_WEB_ENDPOINT_H

#include <cstdint>
#include <memory>
#include <string>

namespace vna_service {
class InstrumentService;
}
namespace vna_web {
struct WebOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string staticRoot;
    bool enabled = true;
};

// All calls belong to the same executor as InstrumentService and SCPI.
// poll never waits on a device future; accepted writes outlive HTTP connections.
class WebEndpoint {
public:
    WebEndpoint(vna_service::InstrumentService& service, const WebOptions& options);
    ~WebEndpoint();
    WebEndpoint(const WebEndpoint&) = delete;
    WebEndpoint& operator=(const WebEndpoint&) = delete;
    void poll();
    void beginShutdown();
    bool drained() const;
    std::uint16_t port() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vna_web
#endif
