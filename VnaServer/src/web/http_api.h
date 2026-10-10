/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_HTTP_API_H
#define VNAPRO_WEB_HTTP_API_H
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "vna_service/instrument_service.h"
#include "web/http_types.h"

namespace vna_web {
// Owns bounded HTTP sessions and operation records on the service executor.
class HttpApi final {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    // The service must outlive this API; the owned callback must keep its captures alive.
    explicit HttpApi(VnaService::InstrumentService &service, Now clock = Clock::now);
    // Releases owned session/operation records; the executor must drain accepted work first.
    ~HttpApi() noexcept = default;
    HttpApi(const HttpApi &) = delete;
    HttpApi &operator=(const HttpApi &) = delete;
    HttpApi(HttpApi &&) = delete;
    HttpApi &operator=(HttpApi &&) = delete;
    // Dispatch one request; input errors become a typed HTTP error response.
    HttpResponse Handle(const HttpRequest &request);
    // Observe device completion and enforce session expiry without blocking I/O.
    void Poll();
    // Release admission and drain already accepted finite operations.
    void BeginShutdown();
    // Return true once this API owns neither pending operations nor control.
    bool Drained() const;

private:
    struct HttpRetry final {
        std::string requestId;
        std::string fingerprint;
        std::string operationId;
    };

    struct HttpSession final {
        std::chrono::steady_clock::time_point expires{};
        std::chrono::steady_clock::time_point touched{};
        bool releasing = false;
        std::deque<HttpRetry> retries;
    };

    struct HttpOperationRecord final {
        VnaService::Operation operation;
        std::string owner;
        std::chrono::steady_clock::time_point created{};
        std::chrono::steady_clock::time_point completed{};
        bool done = false;
        VnaService::OperationResult result;
    };

    struct HttpWriteIdentity final {
        std::string requestId;
        std::string fingerprint;
        std::string owner;
    };

    struct HttpWriteResult final {
        VnaService::Operation operation;
        HttpResponse response;
        bool admitted = false;
    };

    std::string Token();
    std::string HeaderToken(const HttpRequest &request) const;
    static Json ConfigurationJson(const VnaService::InstrumentSettings &settings);
    Json ControlJson(const VnaService::InstrumentSnapshot &snapshot, const HttpRequest &request) const;
    HttpResponse Status(const HttpRequest &request) const;
    HttpResponse Frame() const;
    static Json TraceJson(const VnaCore::MeasurementFrame &frame, const VnaService::Measurement &measurement,
        const std::vector<std::size_t> &indexes);
    HttpResponse OperationStatus(const std::string &id, const std::string &requester) const;
    HttpResponse Accepted(const std::string &id, const std::string &owner) const;
    HttpResponse Claim(const Json &body);
    HttpResponse Release(const std::string &id);
    HttpWriteResult StartWrite(const HttpRequest &request, const Json &body, const std::string &id);
    static VnaService::ConfigPatch ConfigurationPatch(const Json &body);
    static VnaService::Measurement MeasurementDefinition(const Json &body);
    VnaService::Operation StartAction(const Json &body, const std::string &id);
    bool RetryResponse(const HttpSession &session, const HttpWriteIdentity &request, HttpResponse &response) const;
    HttpResponse RecordWrite(
        const HttpRequest &request, const Json &body, HttpSession &session, const HttpWriteIdentity &identity);
    void RollbackWrite(HttpSession &session, const std::string &operationId) noexcept;
    HttpResponse Write(const HttpRequest &request, const Json &body);
    HttpResponse Route(const HttpRequest &request);
    VnaService::InstrumentService &service_;
    const Now now_;
    std::map<std::string, HttpSession> sessions_;
    std::map<std::string, HttpOperationRecord> records_;
    std::uint64_t sequence_ = 0;
    bool shuttingDown_ = false;
    std::random_device random_;
};

// Build a JSON error response with the supplied status and diagnostic.
HttpResponse HttpError(unsigned status, const std::string &code, const std::string &message);
}  // namespace vna_web
#endif
