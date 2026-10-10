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

namespace VnaWeb {
// 在服务执行器上管理有界的 HTTP 会话和操作记录。
class HttpApi final {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    // 服务的生命周期必须覆盖本 API；持有的回调须确保其捕获对象持续有效。
    explicit HttpApi(VnaService::InstrumentService &service, Now clock = Clock::now);
    // 释放持有的会话和操作记录；执行器须先等待已接受的操作完成。
    ~HttpApi() noexcept = default;
    HttpApi(const HttpApi &) = delete;
    HttpApi &operator=(const HttpApi &) = delete;
    HttpApi(HttpApi &&) = delete;
    HttpApi &operator=(HttpApi &&) = delete;
    // 分派一个请求；输入错误转换为结构化的 HTTP 错误响应。
    HttpResponse Handle(const HttpRequest &request);
    // 检查设备操作是否完成，并处理会话过期，不阻塞 I/O。
    void Poll();
    // 停止接受新操作，并等待已接受的有限操作完成。
    void BeginShutdown();
    // 本 API 不再持有待完成操作或控制权时，返回 true。
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

// 根据给定状态码和诊断信息构造 JSON 错误响应。
HttpResponse HttpError(unsigned status, const std::string &code, const std::string &message);
}  // 命名空间 VnaWeb
#endif
