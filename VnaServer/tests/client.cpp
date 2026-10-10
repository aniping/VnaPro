/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "client.h"

#include <cstddef>
#include <stdexcept>

namespace VnaServerTest {
namespace {
void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}  // namespace

Client::Client(std::uint16_t port)
    : runtime_("Test network initialization failed"), socket_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
{
    Expect(socket_.Get() != VnaServer::Network::INVALID_SOCKET_HANDLE, "Test socket creation failed");
    Expect(VnaServer::Network::ConnectLoopback(socket_.Get(), port), "Test controller connection failed");
}

void Client::Write(const std::string &text)
{
    std::size_t offset = 0;
    while (offset != text.size()) {
        const int count = VnaServer::Network::SendSocket(socket_.Get(), text.data() + offset, text.size() - offset);
        Expect(count > 0, "Test write failed");
        offset += static_cast<std::size_t>(count);
    }
}

std::string Client::ReadLine()
{
    std::string text;
    char value = '\0';
    while (VnaServer::Network::ReceiveSocket(socket_.Get(), &value, 1U) == 1) {
        if (value == '\n') {
            return text;
        }
        text += value;
    }
    throw std::runtime_error("Test response ended early");
}
}  // namespace VnaServerTest
