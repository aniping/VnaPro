/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "scpi/scpi_diagnostics.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <vector>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_parser.h"
#include "vna_log/logger.h"

namespace VnaScpi {
namespace {
std::atomic<std::uint64_t> g_nextConnection{0U};
constexpr int DIAGNOSTIC_SERVICE_ERROR = -310;
}  // 匿名命名空间

ScpiDiagnostics::ScpiDiagnostics() noexcept : connection_(++g_nextConnection)
{
}

std::uint64_t ScpiDiagnostics::ConnectionId() const noexcept
{
    return connection_;
}

bool ScpiDiagnostics::Enabled() const noexcept
{
    return VnaLog::Logger::Instance().Enabled(VnaLog::Level::WARN);
}

std::uint64_t ScpiDiagnostics::Received(const std::string &line, bool control) noexcept
{
    const auto message = ++nextMessage_;
    if (line.size() <= LINE_BUDGET) {
        VnaLog::Info(VnaLog::Module::SCPI, "SCPI event=received connection={} message={} role={} bytes={} program={}",
            connection_, message, control ? "controller" : "observer", line.size(), line);
    } else {
        VnaLog::Info(VnaLog::Module::SCPI,
            "SCPI event=received connection={} message={} role={} bytes={} program=oversized", connection_, message,
            control ? "controller" : "observer", line.size());
    }
    return message;
}

void ScpiDiagnostics::Reject(
    std::uint64_t message, std::size_t unit, const ParsedCommand &command, int code, const char *reason) noexcept
{
    VnaLog::Warn(VnaLog::Module::SCPI,
        "SCPI event=rejected connection={} message={} unit={} path={} query={} channel={} code={} stage=admission "
        "reason={}",
        connection_, message, unit, command.path.empty() ? "<program>" : command.path.c_str(), command.query ? 1 : 0,
        command.channelId, code, reason);
}

void ScpiDiagnostics::RejectProgram(
    std::uint64_t message, const std::string &line, int code, const char *reason) noexcept
{
    if (!Enabled()) {
        return;
    }
    try {
        if (line.size() <= LINE_BUDGET) {
            const auto units = SplitProgram(line);
            if (!units.empty()) {
                std::vector<std::string> parent;
                const auto command = RouteChannelCommand(ParseCommand(units.front(), parent));
                Reject(message, 1U, command, code, reason);
                return;
            }
        }
    } catch (...) {
        // 仅诊断解析失败时保留程序消息级原因，不影响原拒绝结果。
    }
    Reject(message, 0U, ParsedCommand{}, code, reason);
}

ScpiDiagnostics::Entry *ScpiDiagnostics::Find(Token token) noexcept
{
    if (token != 0U) {
        for (auto &entry : pending_) {
            if (entry.token == token) {
                return &entry;
            }
        }
    }
    return nullptr;
}

void ScpiDiagnostics::Begin(
    std::uint64_t message, std::size_t unit, const ParsedCommand &command, unsigned channel) noexcept
{
    current_ = 0U;
    if (!Enabled()) {
        return;
    }
    Poll();
    try {
        for (auto &entry : pending_) {
            if (entry.token != 0U) {
                continue;
            }
            entry.path = command.path;
            entry.message = message;
            entry.unit = unit;
            entry.query = command.query;
            entry.channel = channel;
            entry.token = ++nextToken_;
            current_ = entry.token;
            VnaLog::Info(VnaLog::Module::SCPI,
                "SCPI event=started connection={} message={} unit={} path={} query={} channel={} code=0", connection_,
                message, unit, entry.path, entry.query ? 1 : 0, channel);
            return;
        }
        VnaLog::Warn(VnaLog::Module::SCPI,
            "SCPI event=diagnostic_dropped connection={} message={} unit={} path={} query={} channel={} code=-363 "
            "stage=diagnostic reason=Pending diagnostic capacity exhausted; protocol is unchanged",
            connection_, message, unit, command.path, command.query ? 1 : 0, channel);
    } catch (...) {
        VnaLog::Logger::ReportFailure();
    }
}

ScpiDiagnostics::Token ScpiDiagnostics::Current() const noexcept
{
    return current_;
}

void ScpiDiagnostics::AddOperation(const VnaService::Operation &operation, bool prior) noexcept
{
    auto *entry = Find(current_);
    if (entry == nullptr) {
        return;
    }
    if (entry->count == OPERATION_LIMIT) {
        VnaLog::Warn(VnaLog::Module::SCPI,
            "SCPI event=diagnostic_dropped connection={} message={} unit={} path={} query={} channel={} code=-363 "
            "stage=diagnostic reason=Operation diagnostic capacity exhausted; protocol is unchanged",
            connection_, entry->message, entry->unit, entry->path, entry->query ? 1 : 0, entry->channel);
        Retire(*entry);
        return;
    }
    entry->operations[entry->count] = operation;
    entry->prior[entry->count++] = prior;
    if (!prior) {
        entry->stage = "service";
    }
}

void ScpiDiagnostics::Hold(const char *stage) noexcept
{
    if (auto *entry = Find(current_)) {
        entry->stage = stage;
        entry->held = true;
    }
}

void ScpiDiagnostics::Values(std::size_t count) noexcept
{
    if (auto *entry = Find(current_)) {
        entry->values = count;
    }
}

void ScpiDiagnostics::EndDispatch() noexcept
{
    if (auto *entry = Find(current_)) {
        entry->sealed = true;
        Evaluate(*entry);
    }
}

void ScpiDiagnostics::Failed(Entry &entry, int code, const char *stage, const char *reason) noexcept
{
    VnaLog::Warn(VnaLog::Module::SCPI,
        "SCPI event=failed connection={} message={} unit={} path={} query={} channel={} code={} stage={} reason={}",
        connection_, entry.message, entry.unit, entry.path, entry.query ? 1 : 0, entry.channel, code, stage, reason);
    Retire(entry);
}

void ScpiDiagnostics::Fail(int code, const char *reason, const char *stage) noexcept
{
    if (auto *entry = Find(current_)) {
        Failed(*entry, code, stage, reason);
    }
}

void ScpiDiagnostics::Retire(Entry &entry) noexcept
{
    for (std::size_t index = 0U; index < entry.count; ++index) {
        entry.operations[index] = {};
    }
    entry.token = 0U;
    entry.sealed = false;
    entry.held = false;
    entry.stage = "sync";
    entry.values = 0U;
    entry.bytes = 0U;
    entry.count = 0U;
}

void ScpiDiagnostics::Evaluate(Entry &entry) noexcept
{
    if (!entry.sealed || entry.held) {
        return;
    }
    try {
        for (std::size_t index = 0U; index < entry.count; ++index) {
            const auto &operation = entry.operations[index];
            if (!operation.valid()) {
                Failed(entry, DIAGNOSTIC_SERVICE_ERROR, "service", "Invalid service completion");
                return;
            }
            if (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
        }
        unsigned priorFailures = 0U;
        for (std::size_t index = 0U; index < entry.count; ++index) {
            const auto &result = entry.operations[index].get();
            if (!result.ok && !entry.prior[index]) {
                Failed(entry, result.errorCode, "service", result.message.c_str());
                return;
            }
            priorFailures += !result.ok ? 1U : 0U;
        }
        VnaLog::Info(VnaLog::Module::SCPI,
            "SCPI event=completed connection={} message={} unit={} path={} query={} channel={} code=0 stage={} "
            "values={} bytes={} priorFailures={}",
            connection_, entry.message, entry.unit, entry.path, entry.query ? 1 : 0, entry.channel, entry.stage,
            entry.values, entry.bytes, priorFailures);
        Retire(entry);
    } catch (const std::exception &error) {
        // shared_future::get 只旁观结果；异常同样不得逃逸到协议执行器。
        Failed(entry, DIAGNOSTIC_SERVICE_ERROR, "service", error.what());
    } catch (...) {
        Failed(entry, DIAGNOSTIC_SERVICE_ERROR, "service", "Unknown service completion exception");
    }
}

void ScpiDiagnostics::Release(Token token, std::size_t bytes) noexcept
{
    if (auto *entry = Find(token)) {
        entry->bytes = bytes;
        entry->held = false;
        Evaluate(*entry);
    }
}

void ScpiDiagnostics::Cancel(Token token, const char *reason) noexcept
{
    if (auto *entry = Find(token)) {
        VnaLog::Info(VnaLog::Module::SCPI,
            "SCPI event=cancelled connection={} message={} unit={} path={} query={} channel={} code=0 stage={} "
            "reason={}",
            connection_, entry->message, entry->unit, entry->path, entry->query ? 1 : 0, entry->channel, entry->stage,
            reason);
        Retire(*entry);
    }
}

void ScpiDiagnostics::Poll() noexcept
{
    for (auto &entry : pending_) {
        if (entry.token != 0U) {
            Evaluate(entry);
        }
    }
}
}  // 命名空间 VnaScpi
