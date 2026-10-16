/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi_connection.h"

#include <utility>

#include "scpi/scpi_constants.h"
#include "vna_log/logger.h"

namespace VnaServer {
namespace {
constexpr std::size_t READ_BUFFER_CAPACITY = 4096U;
const std::chrono::seconds WRITE_PROGRESS_TIMEOUT{10};
}  // 匿名命名空间

ScpiConnection::ScpiConnection(VnaService::InstrumentService &service, Network::SocketOwner socket)
    : socket_(std::move(socket)), engine_(service)
{
    Network::ConfigureStreamSocket(socket_.Get());
    if (!engine_.HasControl()) {
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI event=connected connection={} role=observer", engine_.ConnectionId());
    } else {
        VnaLog::Info(
            VnaLog::Module::SCPI, "SCPI event=connected connection={} role=controller", engine_.ConnectionId());
    }
}

Network::Socket ScpiConnection::GetSocket() const noexcept
{
    return socket_.Get();
}

bool ScpiConnection::WantsRead() const noexcept
{
    return socket_.Get() != Network::INVALID_SOCKET_HANDLE &&
           (output_.size() - sent_) < (VnaScpi::TRANSPORT_OUTPUT_BYTE_LIMIT / 2U);
}

bool ScpiConnection::WantsWrite() const noexcept
{
    return socket_.Get() != Network::INVALID_SOCKET_HANDLE && output_.size() != sent_;
}

void ScpiConnection::Poll()
{
    engine_.SetTransportOutputPending(output_.size() != sent_);
    engine_.Poll();
    if (!engine_.HasControl() && engine_.LocalError().code != 0 && socket_.Get() != Network::INVALID_SOCKET_HANDLE) {
        VnaLog::Warn(VnaLog::Module::SCPI, "SCPI event=transport_error connection={} stage=observer code={} message={}",
            engine_.ConnectionId(), engine_.LocalError().code, engine_.LocalError().message);
        Disconnect("observer_failed");
    }
    CollectResponses();
    if (WantsWrite() && (std::chrono::steady_clock::now() - lastSendProgress_) > WRITE_PROGRESS_TIMEOUT) {
        VnaLog::Warn(VnaLog::Module::SCPI,
            "SCPI event=transport_error connection={} stage=send reason=timeout pendingBytes={}",
            engine_.ConnectionId(), output_.size() - sent_);
        Disconnect("write_timeout");
    }
}

void ScpiConnection::CollectResponses()
{
    while (true) {
        std::string response = engine_.TakeOutput();
        if (response.empty()) {
            return;
        }
        if (socket_.Get() == Network::INVALID_SOCKET_HANDLE) {
            continue;
        }
        if (response.size() > (VnaScpi::TRANSPORT_OUTPUT_BYTE_LIMIT - (output_.size() - sent_))) {
            VnaLog::Warn(VnaLog::Module::SCPI,
                "SCPI event=transport_error connection={} stage=queue reason=output_budget bytes={} pendingBytes={}",
                engine_.ConnectionId(), response.size(), output_.size() - sent_);
            Disconnect("output_budget");
            return;
        }
        if (sent_ != 0U) {
            output_.erase(0, sent_);
            sent_ = 0;
        }
        if (output_.empty()) {
            lastSendProgress_ = std::chrono::steady_clock::now();
        }
        output_ += response;
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI event=tx_queued connection={} bytes={}", engine_.ConnectionId(),
            response.size());
    }
}

void ScpiConnection::Read()
{
    char buffer[READ_BUFFER_CAPACITY]{};
    const int received = Network::ReceiveSocket(socket_.Get(), buffer, sizeof(buffer));
    if (received > 0) {
        input_.append(buffer, static_cast<std::size_t>(received));
        SubmitLines();
        if (input_.size() > VnaScpi::LINE_BUDGET) {
            VnaLog::Warn(VnaLog::Module::SCPI,
                "SCPI event=transport_error connection={} stage=receive reason=line_budget bytes={} limit={}",
                engine_.ConnectionId(), input_.size(), VnaScpi::LINE_BUDGET);
            Disconnect("line_budget");
        }
    } else if (received == 0) {
        Disconnect("peer_closed");
    } else {
        const int error = Network::SocketError();
        if (!Network::WouldBlock(error)) {
            VnaLog::Warn(VnaLog::Module::SCPI, "SCPI event=transport_error connection={} stage=receive socketError={}",
                engine_.ConnectionId(), error);
            Disconnect("receive_failed");
        }
    }
}

void ScpiConnection::SubmitLines()
{
    while (true) {
        const std::size_t boundary = input_.find('\n');
        if (boundary == std::string::npos) {
            return;
        }
        if (boundary > VnaScpi::LINE_BUDGET) {
            VnaLog::Warn(VnaLog::Module::SCPI,
                "SCPI event=transport_error connection={} stage=receive reason=line_budget bytes={} limit={}",
                engine_.ConnectionId(), boundary, VnaScpi::LINE_BUDGET);
            Disconnect("line_budget");
            return;
        }
        std::string line = input_.substr(0, boundary);
        input_.erase(0, boundary + 1U);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && !engine_.SubmitLine(line)) {
            if (!engine_.HasControl()) {
                VnaLog::Warn(VnaLog::Module::SCPI,
                    "SCPI event=transport_error connection={} stage=submit code={} message={}", engine_.ConnectionId(),
                    engine_.LocalError().code, engine_.LocalError().message);
            }
            Disconnect("message_rejected");
            return;
        }
    }
}

void ScpiConnection::Write()
{
    const int written = Network::SendSocket(socket_.Get(), output_.data() + sent_, output_.size() - sent_);
    if (written > 0) {
        sent_ += static_cast<std::size_t>(written);
        lastSendProgress_ = std::chrono::steady_clock::now();
        if (sent_ == output_.size()) {
            VnaLog::Info(
                VnaLog::Module::SCPI, "SCPI event=tx_complete connection={} bytes={}", engine_.ConnectionId(), sent_);
            output_.clear();
            sent_ = 0;
        }
    } else {
        const int error = written == 0 ? 0 : Network::SocketError();
        if (written == 0 || !Network::WouldBlock(error)) {
            VnaLog::Warn(VnaLog::Module::SCPI,
                "SCPI event=transport_error connection={} stage=send socketError={} pendingBytes={}",
                engine_.ConnectionId(), error, output_.size() - sent_);
            Disconnect("send_failed");
        }
    }
}

void ScpiConnection::Disconnect(const char *reason) noexcept
{
    if (socket_.Get() != Network::INVALID_SOCKET_HANDLE) {
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI event=disconnected connection={} reason={} drainPending={}",
            engine_.ConnectionId(), reason, engine_.HasPending());
    }
    socket_.Reset();
    input_.clear();
    output_.clear();
    sent_ = 0;
    engine_.Disconnect();
}

bool ScpiConnection::Drained() const noexcept
{
    return socket_.Get() == Network::INVALID_SOCKET_HANDLE && engine_.CanReleaseControl();
}
}  // 命名空间 VnaServer
