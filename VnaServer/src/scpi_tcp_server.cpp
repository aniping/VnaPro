/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi_tcp_server.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include "network/network_runtime.h"
#include "network/socket_owner.h"
#include "network/socket_poll_set.h"
#include "scpi_connection.h"
#include "web/web_endpoint.h"

namespace vna_server {
namespace {
void AdmitController(
    vna_service::InstrumentService &service, network::Socket listener, std::unique_ptr<ScpiConnection> &connection)
{
    network::SocketOwner incoming(accept(listener, nullptr, nullptr));
    if (incoming.Get() == network::INVALID_SOCKET_HANDLE) {
        return;
    }
    if (connection || !network::FitsSelect(incoming.Get()) || !network::SetNonBlocking(incoming.Get())) {
        return;
    }
    // Nullable ownership reflects a connection that only exists after admission.
    connection = std::make_unique<ScpiConnection>(service, std::move(incoming));
}

void PollTransports(
    network::Socket listener, vna_service::InstrumentService &service, std::unique_ptr<ScpiConnection> &connection)
{
    network::SocketPollSet events;
    events.AddReadable(listener);
    if (connection && connection->WantsRead()) {
        events.AddReadable(connection->GetSocket());
    }
    if (connection && connection->WantsWrite()) {
        events.AddWritable(connection->GetSocket());
    }
    events.Wait();
    if (events.IsReadable(listener)) {
        AdmitController(service, listener, connection);
    }
    if (connection && events.IsReadable(connection->GetSocket())) {
        connection->Read();
    }
    if (connection && events.IsWritable(connection->GetSocket())) {
        connection->Write();
    }
}

void AnnounceListener(const ListenOptions &options, std::uint16_t port)
{
    std::cout << "LISTENING " << options.bindAddress << ':' << port << std::endl;
    if (options.onListening) {
        options.onListening(port);
    }
}
}  // namespace

int RunScpiTcpServer(vna_service::InstrumentService &service, const ListenOptions &options,
    const std::function<bool()> &shutdownRequested)
{
    network::NetworkRuntime runtime;
    network::SocketOwner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener.Get() == network::INVALID_SOCKET_HANDLE) {
        throw std::runtime_error("Cannot create listening socket");
    }
    if (!network::FitsSelect(listener.Get())) {
        throw std::runtime_error("Listening descriptor exceeds select capacity");
    }
    AnnounceListener(options, network::BindListener(listener.Get(), options.bindAddress, options.port));
    vna_web::WebEndpoint web(service, options.web);
    std::unique_ptr<ScpiConnection> connection;
    bool shuttingDown = false;
    while (true) {
        if (!shuttingDown && shutdownRequested()) {
            shuttingDown = true;
            listener.Reset();
            if (connection) {
                connection->Disconnect();
            }
            web.BeginShutdown();
        }
        web.Poll();
        if (connection) {
            connection->Poll();
            if (connection->Drained()) {
                connection.reset();
            }
        }
        if (shuttingDown && !connection && web.Drained()) {
            return 0;
        }
        PollTransports(listener.Get(), service, connection);
    }
}
}  // namespace vna_server
