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
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI 连接以只读观察模式接入");
    } else {
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI 连接取得控制权");
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
        VnaLog::Warn(VnaLog::Module::SCPI, "观察查询失败，关闭连接，code={}, message={}", engine_.LocalError().code,
            engine_.LocalError().message);
        Disconnect();
    }
    CollectResponses();
    if (WantsWrite() && (std::chrono::steady_clock::now() - lastSendProgress_) > WRITE_PROGRESS_TIMEOUT) {
        VnaLog::Warn(VnaLog::Module::SCPI, "响应发送超时，关闭 SCPI 连接");
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
        if (socket_.Get() == Network::INVALID_SOCKET_HANDLE) {
            continue;
        }
        if (response.size() > (VnaScpi::TRANSPORT_OUTPUT_BYTE_LIMIT - (output_.size() - sent_))) {
            VnaLog::Warn(VnaLog::Module::SCPI, "响应超过发送预算，关闭 SCPI 连接");
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
    const int received = Network::ReceiveSocket(socket_.Get(), buffer, sizeof(buffer));
    if (received > 0) {
        input_.append(buffer, static_cast<std::size_t>(received));
        SubmitLines();
        if (input_.size() > VnaScpi::LINE_BUDGET) {
            Disconnect();
        }
    } else if (received == 0 || !Network::WouldBlock(Network::SocketError())) {
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
        if (boundary > VnaScpi::LINE_BUDGET) {
            Disconnect();
            return;
        }
        std::string line = input_.substr(0, boundary);
        input_.erase(0, boundary + 1U);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && !engine_.SubmitLine(line)) {
            if (!engine_.HasControl()) {
                VnaLog::Warn(VnaLog::Module::SCPI, "观察消息整行拒绝，code={}, message={}", engine_.LocalError().code,
                    engine_.LocalError().message);
            }
            Disconnect();
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
            output_.clear();
            sent_ = 0;
        }
    } else if (written == 0 || !Network::WouldBlock(Network::SocketError())) {
        Disconnect();
    }
}

void ScpiConnection::Disconnect() noexcept
{
    if (socket_.Get() != Network::INVALID_SOCKET_HANDLE) {
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI 连接关闭，等待已接受操作排空");
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
