/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_WEB_ENDPOINT_H
#define VNAPRO_WEB_WEB_ENDPOINT_H
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <utility>

#include "network/network_runtime.h"
#include "network/socket_owner.h"
#include "vna_service/instrument_service.h"
#include "web/http_api.h"
#include "web/http_types.h"
#include "web/json_encoder.h"
#include "web/static_file.h"

namespace VnaServer {
namespace Network {
class SocketPollSet;
}  // 命名空间 Network
}  // 命名空间 VnaServer

namespace VnaWeb {
// 在与 InstrumentService 和 SCPI 相同的执行器上管理传输状态。
class WebEndpoint final {
public:
    // 绑定监听套接字；服务的生命周期必须覆盖本端点，失败时抛出异常。
    WebEndpoint(VnaService::InstrumentService &service, const WebOptions &options);
    // 关闭持有的全部连接；析构前必须等待已接受的 API 操作完成。
    ~WebEndpoint() noexcept = default;
    WebEndpoint(const WebEndpoint &) = delete;
    WebEndpoint &operator=(const WebEndpoint &) = delete;
    WebEndpoint(WebEndpoint &&) = delete;
    WebEndpoint &operator=(WebEndpoint &&) = delete;
    // 推进一轮有界的接收、编码和发送，不等待设备 I/O。
    void Poll();
    // 将 HTTP I/O 与可推进的编码工作加入同一执行器的就绪检测集合。
    void AddPollEvents(VnaServer::Network::SocketPollSet &events) const noexcept;
    // 停止接受新请求，关闭连接，并等待已接受的 API 操作完成。
    void BeginShutdown();
    // 返回所有连接和有限操作是否均已结束。
    bool Drained() const;
    // 返回实际绑定的端口，包括操作系统分配的临时端口。
    std::uint16_t Port() const noexcept;

private:
    struct HttpConnection final {
        // 接管已接受的套接字，其余状态初始化为空闲。
        HttpConnection(VnaServer::Network::SocketOwner ownedSocket, std::uint64_t identifier)
            : socket(std::move(ownedSocket)),
              started(std::chrono::steady_clock::now()),
              progress(started),
              connectionId(identifier)
        {
        }

        // 即使请求解析抛出异常，也关闭连接和文件句柄。
        ~HttpConnection() noexcept = default;
        HttpConnection(const HttpConnection &) = delete;
        HttpConnection &operator=(const HttpConnection &) = delete;
        HttpConnection(HttpConnection &&) = delete;
        HttpConnection &operator=(HttpConnection &&) = delete;
        VnaServer::Network::SocketOwner socket;
        const std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point progress{};
        std::string input;
        std::string output;
        std::string fileOutput;
        HttpRequest request;
        std::size_t headerLength = 0;
        std::size_t bodyLength = 0;
        std::size_t sent = 0;
        bool parsed = false;
        bool dispatched = false;
        unsigned status = 200;
        std::string contentType = "application/json; charset=utf-8";
        JsonEncoder encoder;
        bool encoderActive = false;
        StaticFile file;
        bool fileActive = false;
        // 仅在 DEBUG 请求中关联阶段，不保存控制令牌或原始采样数组。
        const std::uint64_t connectionId;
        bool frameDiagnostics = false;
        std::string frameSeq;
        std::string runId;
        std::string displayRevision;
        std::chrono::steady_clock::time_point dispatchStarted{};
        std::chrono::steady_clock::time_point encodingStarted{};
        std::chrono::steady_clock::time_point encodedAt{};
        std::chrono::microseconds encodingCpu{0};
        std::size_t responseBytes = 0U;
        std::size_t writeBlocked = 0U;
    };

    void Respond(HttpConnection &client, HttpResponse response);
    void Error(HttpConnection &client, unsigned status, const std::string &message);
    bool ParseHeaders(HttpConnection &client);
    bool ParseContentLength(HttpConnection &client);
    bool ValidateContentType(HttpConnection &client);
    void Dispatch(HttpConnection &client);
    bool Receive(HttpConnection &client);
    bool SendOutput(HttpConnection &client);
    void EncodeOutput(HttpConnection &client);
    void AcceptConnections();
    void PollClients();
    VnaServer::Network::NetworkRuntime runtime_;
    VnaServer::Network::SocketOwner listener_;
    HttpApi api_;
    const WebOptions options_;
    std::list<HttpConnection> clients_;
    std::uint16_t actualPort_ = 0;
    bool closing_ = false;
    std::uint64_t nextConnectionId_ = 0U;
};
}  // 命名空间 VnaWeb
#endif
