/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SCPI_CONNECTION_H
#define VNAPRO_SERVER_SCPI_CONNECTION_H

#include <chrono>
#include <cstddef>
#include <string>

#include "network/socket_owner.h"
#include "scpi/scpi_engine.h"

namespace vna_server {
// Owns one controller transport and its accepted SCPI command lifetime.
class ScpiConnection final {
public:
    // Takes ownership of socket. The service must outlive this connection.
    ScpiConnection(vna_service::InstrumentService &service, network::SocketOwner socket);
    // The owner must continue Poll until Drained before normal destruction.
    ~ScpiConnection() noexcept = default;
    ScpiConnection(const ScpiConnection &) = delete;
    ScpiConnection &operator=(const ScpiConnection &) = delete;
    ScpiConnection(ScpiConnection &&) = delete;
    ScpiConnection &operator=(ScpiConnection &&) = delete;
    // Returns a borrowed socket handle; invalid after disconnection.
    network::Socket GetSocket() const noexcept;
    // Applies backpressure when unsent output consumes half the budget.
    bool WantsRead() const noexcept;
    // Reports unsent output for selecting writable sockets.
    bool WantsWrite() const noexcept;
    // Advances finite operations and encoding; closes stalled writers.
    void Poll();
    // Reads available bytes and admits complete LF-terminated program messages.
    void Read();
    // Writes available response bytes; partial sends retain the remaining bytes.
    void Write();
    // Detaches the transport while retaining accepted commands until completion.
    void Disconnect() noexcept;
    // True only after disconnection and the engine's exclusive grant is released.
    bool Drained() const noexcept;

private:
    void CollectResponses();
    void SubmitLines();

    network::SocketOwner socket_;
    std::string input_;
    std::string output_;
    std::size_t sent_ = 0;
    std::chrono::steady_clock::time_point lastSendProgress_ = std::chrono::steady_clock::now();
    vna_scpi::ScpiEngine engine_;
};
}  // namespace vna_server

#endif
