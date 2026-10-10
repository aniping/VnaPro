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

namespace vna_web {
// Owns transport state on the same executor as InstrumentService and SCPI.
class WebEndpoint final {
public:
    // Bind the listener; service must outlive this endpoint. Throws on failure.
    WebEndpoint(vna_service::InstrumentService &service, const WebOptions &options);
    // Closes all owned transports; accepted API work must be drained before destruction.
    ~WebEndpoint() noexcept = default;
    WebEndpoint(const WebEndpoint &) = delete;
    WebEndpoint &operator=(const WebEndpoint &) = delete;
    WebEndpoint(WebEndpoint &&) = delete;
    WebEndpoint &operator=(WebEndpoint &&) = delete;
    // Perform one bounded receive/encode/send pass without waiting for device I/O.
    void Poll();
    // Stop admission, close transports, and drain accepted API operations.
    void BeginShutdown();
    // Report whether all transports and finite operations have drained.
    bool Drained() const;
    // Return the actual bound port, including OS-assigned ephemeral ports.
    std::uint16_t Port() const noexcept;

private:
    struct HttpConnection final {
        // Take ownership of an accepted socket; all other state begins idle.
        explicit HttpConnection(vna_server::network::SocketOwner ownedSocket)
            : socket(std::move(ownedSocket)), started(std::chrono::steady_clock::now()), progress(started)
        {
        }

        // Closes transport and file handles even when request parsing throws.
        ~HttpConnection() noexcept = default;
        HttpConnection(const HttpConnection &) = delete;
        HttpConnection &operator=(const HttpConnection &) = delete;
        HttpConnection(HttpConnection &&) = delete;
        HttpConnection &operator=(HttpConnection &&) = delete;
        vna_server::network::SocketOwner socket;
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
    vna_server::network::NetworkRuntime runtime_;
    vna_server::network::SocketOwner listener_;
    HttpApi api_;
    const WebOptions options_;
    std::list<HttpConnection> clients_;
    std::uint16_t actualPort_ = 0;
    bool closing_ = false;
};
}  // namespace vna_web
#endif
