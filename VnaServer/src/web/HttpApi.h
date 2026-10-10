#ifndef VNAPRO_WEB_HTTP_API_H
#define VNAPRO_WEB_HTTP_API_H
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "web/Json.h"

namespace vna_service {
class InstrumentService;
}
namespace vna_web {
struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;  // Lowercase names.
    std::string body;
};
struct HttpResponse {
    unsigned status = 200;
    Json json;
};
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
