/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi_connection.h"

#include <utility>

#include "scpi/scpi_constants.h"

namespace vna_server {
namespace {
constexpr std::size_t READ_BUFFER_CAPACITY = 4096U;
const std::chrono::seconds WRITE_PROGRESS_TIMEOUT{10};
}  // namespace

ScpiConnection::ScpiConnection(vna_service::InstrumentService &service, network::SocketOwner socket)
    : socket_(std::move(socket)), engine_(service)
{
    network::ConfigureStreamSocket(socket_.Get());
    if (!engine_.HasControl()) {
        Disconnect();
    }
}

network::Socket ScpiConnection::GetSocket() const noexcept
{
    return socket_.Get();
}

bool ScpiConnection::WantsRead() const noexcept
{
    return socket_.Get() != network::INVALID_SOCKET_HANDLE &&
           (output_.size() - sent_) < (vna_scpi::TRANSPORT_OUTPUT_BYTE_LIMIT / 2U);
}

bool ScpiConnection::WantsWrite() const noexcept
{
    return socket_.Get() != network::INVALID_SOCKET_HANDLE && output_.size() != sent_;
}

void ScpiConnection::Poll()
{
    engine_.SetTransportOutputPending(output_.size() != sent_);
    engine_.Poll();
    CollectResponses();
    if (WantsWrite() && (std::chrono::steady_clock::now() - lastSendProgress_) > WRITE_PROGRESS_TIMEOUT) {
        Disconnect();
    }
}

void ScpiConnection::CollectResponses()
{
    while (true) {
        std::string response = engine_.TakeOutput();
        if (response.empty()) {
            return;
        }
        if (socket_.Get() == network::INVALID_SOCKET_HANDLE) {
            continue;
        }
        if (response.size() > (vna_scpi::TRANSPORT_OUTPUT_BYTE_LIMIT - (output_.size() - sent_))) {
            Disconnect();
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
    }
}

void ScpiConnection::Read()
{
    char buffer[READ_BUFFER_CAPACITY]{};
    const int received = network::ReceiveSocket(socket_.Get(), buffer, sizeof(buffer));
    if (received > 0) {
        input_.append(buffer, static_cast<std::size_t>(received));
        SubmitLines();
        if (input_.size() > vna_scpi::LINE_BUDGET) {
            Disconnect();
        }
    } else if (received == 0 || !network::WouldBlock(network::SocketError())) {
        Disconnect();
    }
}

void ScpiConnection::SubmitLines()
{
    while (true) {
        const std::size_t boundary = input_.find('\n');
        if (boundary == std::string::npos) {
            return;
        }
        if (boundary > vna_scpi::LINE_BUDGET) {
            Disconnect();
            return;
        }
        std::string line = input_.substr(0, boundary);
        input_.erase(0, boundary + 1U);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && !engine_.SubmitLine(line)) {
            Disconnect();
            return;
        }
    }
}

void ScpiConnection::Write()
{
    const int written = network::SendSocket(socket_.Get(), output_.data() + sent_, output_.size() - sent_);
    if (written > 0) {
        sent_ += static_cast<std::size_t>(written);
        lastSendProgress_ = std::chrono::steady_clock::now();
        if (sent_ == output_.size()) {
            output_.clear();
            sent_ = 0;
        }
    } else if (written == 0 || !network::WouldBlock(network::SocketError())) {
        Disconnect();
    }
}

void ScpiConnection::Disconnect() noexcept
{
    socket_.Reset();
    input_.clear();
    output_.clear();
    sent_ = 0;
    engine_.Disconnect();
}

bool ScpiConnection::Drained() const noexcept
{
    return socket_.Get() == network::INVALID_SOCKET_HANDLE && engine_.CanReleaseControl();
}
}  // namespace vna_server
