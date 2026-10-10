#ifndef VNAPRO_WEB_HTTP_API_H
#define VNAPRO_WEB_HTTP_API_H
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "web/HttpRequest.h"
#include "web/HttpResponse.h"

namespace vna_service {
class InstrumentService;
}

namespace vna_web {
HttpResponse httpError(unsigned status, const std::string& code, const std::string& message);

class HttpApi {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    explicit HttpApi(vna_service::InstrumentService& service, Now now = Clock::now);
    ~HttpApi();
    HttpResponse handle(const HttpRequest& request);
    void poll();
    void beginShutdown();
    bool drained() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vna_web
#endif
