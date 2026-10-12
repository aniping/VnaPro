/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi_tcp_server.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "network/network_runtime.h"
#include "network/socket_owner.h"
#include "network/socket_poll_set.h"
#include "scpi/scpi_constants.h"
#include "scpi_connection.h"
#include "vna_log/logger.h"
#include "web/web_endpoint.h"

namespace VnaServer {
namespace {
using ScpiConnections = std::vector<std::unique_ptr<ScpiConnection>>;

void AdmitConnection(VnaService::InstrumentService &service, Network::Socket listener, ScpiConnections &connections)
{
    Network::SocketOwner incoming(accept(listener, nullptr, nullptr));
    if (incoming.Get() == Network::INVALID_SOCKET_HANDLE) {
        return;
    }
    if (connections.size() >= VnaScpi::CONNECTION_LIMIT || !Network::FitsSelect(incoming.Get()) ||
        !Network::SetNonBlocking(incoming.Get())) {
        VnaLog::Debug(VnaLog::Module::SCPI, "SCPI 连接未获准接入");
        return;
    }
    connections.push_back(std::make_unique<ScpiConnection>(service, std::move(incoming)));
}

void PollTransports(Network::Socket listener, VnaService::InstrumentService &service, ScpiConnections &connections,
    const VnaWeb::WebEndpoint &web)
{
    Network::SocketPollSet events;
    events.AddReadable(listener);
    for (const auto &connection : connections) {
        if (connection->WantsRead()) {
            events.AddReadable(connection->GetSocket());
        }
        if (connection->WantsWrite()) {
            events.AddWritable(connection->GetSocket());
        }
    }
    web.AddPollEvents(events);
    events.Wait();
    if (events.IsReadable(listener)) {
        AdmitConnection(service, listener, connections);
    }
    for (const auto &connection : connections) {
        if (events.IsReadable(connection->GetSocket())) {
            connection->Read();
        }
        if (events.IsWritable(connection->GetSocket())) {
            connection->Write();
        }
    }
}

void PollConnections(ScpiConnections &connections)
{
    for (const auto &connection : connections) {
        connection->Poll();
    }
    connections.erase(std::remove_if(connections.begin(), connections.end(),
                          [](const auto &connection) {
                              return connection->Drained();
                          }),
        connections.end());
}

void AnnounceListener(const ListenOptions &options, std::uint16_t port)
{
    VnaLog::Info(VnaLog::Module::SCPI, "TCP SCPI 已监听，address={}:{}", options.bindAddress, port);
    if (options.onListening) {
        options.onListening(port);
    }
}
}  // 匿名命名空间

int RunScpiTcpServer(VnaService::InstrumentService &service, const ListenOptions &options,
    const std::function<bool()> &shutdownRequested)
{
    Network::NetworkRuntime runtime;
    Network::SocketOwner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener.Get() == Network::INVALID_SOCKET_HANDLE) {
        throw std::runtime_error("Cannot create listening socket");
    }
    if (!Network::FitsSelect(listener.Get())) {
        throw std::runtime_error("Listening descriptor exceeds select capacity");
    }
    AnnounceListener(options, Network::BindListener(listener.Get(), options.bindAddress, options.port));
    VnaWeb::WebEndpoint web(service, options.web);
    ScpiConnections connections;
    bool shuttingDown = false;
    while (true) {
        if (!shuttingDown && shutdownRequested()) {
            VnaLog::Info(VnaLog::Module::SERVER, "收到退出请求，开始排空已接受操作");
            shuttingDown = true;
            listener.Reset();
            for (const auto &connection : connections) {
                connection->Disconnect();
            }
            web.BeginShutdown();
        }
        web.Poll();
        PollConnections(connections);
        if (shuttingDown && connections.empty() && web.Drained()) {
            return 0;
        }
        PollTransports(listener.Get(), service, connections, web);
    }
}
}  // 命名空间 VnaServer
