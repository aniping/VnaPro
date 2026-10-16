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

namespace VnaServer {
// 管理一个控制或只读观察连接的传输资源及已接收 SCPI 命令的生命周期。
class ScpiConnection final {
public:
    // 接管 socket 的所有权；服务的生命周期必须覆盖本连接。
    ScpiConnection(VnaService::InstrumentService &service, Network::SocketOwner socket);
    // 正常销毁前，所有者必须持续调用 Poll，直到 Drained 返回真。
    ~ScpiConnection() noexcept = default;
    ScpiConnection(const ScpiConnection &) = delete;
    ScpiConnection &operator=(const ScpiConnection &) = delete;
    ScpiConnection(ScpiConnection &&) = delete;
    ScpiConnection &operator=(ScpiConnection &&) = delete;
    // 返回借用的套接字句柄；断开连接后该句柄无效。
    Network::Socket GetSocket() const noexcept;
    // 待发送数据占用一半容量预算时，通过暂停读取施加背压。
    bool WantsRead() const noexcept;
    // 报告是否存在待发送数据，供选择可写套接字时使用。
    bool WantsWrite() const noexcept;
    // 推进有限操作和响应编码，并关闭发送停滞的连接。
    void Poll();
    // 读取可用字节，并接收以 LF 结尾的完整程序消息。
    void Read();
    // 发送可用的响应字节；部分发送时保留尚未发送的字节。
    void Write();
    // 关闭传输连接，但保留已接收的命令，直到命令执行完成。
    void Disconnect(const char *reason = "requested") noexcept;
    // 仅在连接断开且引擎释放独占控制权后返回真。
    bool Drained() const noexcept;

private:
    void CollectResponses();
    void SubmitLines();

    Network::SocketOwner socket_;
    std::string input_;
    std::string output_;
    std::size_t sent_ = 0;
    std::chrono::steady_clock::time_point lastSendProgress_ = std::chrono::steady_clock::now();
    VnaScpi::ScpiEngine engine_;
};
}  // 命名空间 VnaServer

#endif
