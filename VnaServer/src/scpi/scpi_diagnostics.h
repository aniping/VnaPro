/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_SCPI_DIAGNOSTICS_H
#define VNAPRO_SCPI_DIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "scpi/parsed_command.h"
#include "vna_service/operation_result.h"

namespace VnaScpi {
// 只旁观命令生命周期；容量或日志故障不改变协议、错误队列和完成屏障。
// 与引擎共用单执行线程；原子编号仅保证连接身份分配，不提供对象并发访问。
class ScpiDiagnostics final {
public:
    using Token = std::uint64_t;
    ScpiDiagnostics() noexcept;
    std::uint64_t ConnectionId() const noexcept;
    bool Enabled() const noexcept;
    std::uint64_t Received(const std::string &line, bool control) noexcept;
    void Reject(
        std::uint64_t message, std::size_t unit, const ParsedCommand &command, int code, const char *reason) noexcept;
    void RejectProgram(std::uint64_t message, const std::string &line, int code, const char *reason) noexcept;
    void Begin(std::uint64_t message, std::size_t unit, const ParsedCommand &command, unsigned channel) noexcept;
    Token Current() const noexcept;
    void AddOperation(const VnaService::Operation &operation, bool prior = false) noexcept;
    void Hold(const char *stage) noexcept;
    void Values(std::size_t count) noexcept;
    void EndDispatch() noexcept;
    void Fail(int code, const char *reason, const char *stage = "command") noexcept;
    void Release(Token token, std::size_t bytes = 0U) noexcept;
    void Cancel(Token token, const char *reason) noexcept;
    void Poll() noexcept;

private:
    static constexpr std::size_t PENDING_LIMIT = 128U;
    static constexpr std::size_t OPERATION_LIMIT = 32U;

    struct Entry final {
        Token token = 0U;
        std::uint64_t message = 0U;
        std::size_t unit = 0U;
        std::string path;
        bool query = false;
        unsigned channel = 0U;
        const char *stage = "sync";
        bool sealed = false;
        bool held = false;
        std::size_t values = 0U;
        std::size_t bytes = 0U;
        std::size_t count = 0U;
        std::array<VnaService::Operation, OPERATION_LIMIT> operations;
        std::array<bool, OPERATION_LIMIT> prior{};
    };

    Entry *Find(Token token) noexcept;
    void Evaluate(Entry &entry) noexcept;
    void Failed(Entry &entry, int code, const char *stage, const char *reason) noexcept;
    void Retire(Entry &entry) noexcept;

    std::uint64_t connection_ = 0U;
    std::uint64_t nextMessage_ = 0U;
    Token nextToken_ = 0U;
    Token current_ = 0U;
    std::array<Entry, PENDING_LIMIT> pending_;
};
}  // 命名空间 VnaScpi
#endif
