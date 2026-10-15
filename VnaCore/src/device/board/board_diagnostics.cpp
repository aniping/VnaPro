/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/board/board_diagnostics.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "vna_log/logger.h"

namespace VnaCore {
namespace Detail {
namespace {

constexpr std::size_t VALUES_PER_RECORD = 16U;

template <typename Value>
std::string ValueText(Value value)
{
    return spdlog::fmt_lib::format("{}", value);
}

std::string ValueText(double value)
{
    return spdlog::fmt_lib::format("{:.17g}", value);
}

template <typename Value>
std::string ValuesText(const std::vector<Value> &values, std::size_t first, std::size_t count)
{
    std::string text = "[";
    for (std::size_t index = first; index < first + count; ++index) {
        if (index != first) {
            text += ',';
        }
        text += ValueText(values[index]);
    }
    return text + ']';
}

template <typename Value>
void Summary(
    std::uint64_t config, const char *name, const char *element, const char *unit, const std::vector<Value> &values)
{
    if (values.empty()) {
        VnaLog::Info(VnaLog::Module::CORE,
            "[VN10] phase=configure config={} event=summary name={} element={} bytes={} unit={} count=0", config, name,
            element, sizeof(Value), unit);
        return;
    }
    const auto bounds = std::minmax_element(values.begin(), values.end());
    VnaLog::Info(VnaLog::Module::CORE,
        "[VN10] phase=configure config={} event=summary name={} element={} bytes={} unit={} count={} "
        "first={} last={} min={} max={} uniform={}",
        config, name, element, sizeof(Value), unit, values.size(), ValueText(values.front()), ValueText(values.back()),
        ValueText(*bounds.first), ValueText(*bounds.second), *bounds.first == *bounds.second);
}

template <typename Value>
void Dump(VnaLog::Level level, std::uint64_t config, const char *name, const std::vector<Value> &values)
{
    for (std::size_t first = 0U; first < values.size(); first += VALUES_PER_RECORD) {
        const auto count = (std::min)(VALUES_PER_RECORD, values.size() - first);
        VnaLog::Log(level, VnaLog::Module::CORE,
            "[VN10] phase=configure config={} event=values name={} offset={} count={} values={}", config, name, first,
            count, ValuesText(values, first, count));
    }
}

void DumpConfiguration(VnaLog::Level level, std::uint64_t config, const BoardDownloadLists &lists)
{
    Dump(level, config, "frequency", lists.frequencies);
    Dump(level, config, "power", lists.powers);
    Dump(level, config, "ifbw", lists.bandwidths);
    Dump(level, config, "ports", lists.ports);
}

std::int64_t SignedStatus(std::uint32_t status) noexcept
{
    return status > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
               ? static_cast<std::int64_t>(status) - (std::int64_t{1} << 32U)
               : static_cast<std::int64_t>(status);
}

const char *StatusName(std::uint32_t status) noexcept
{
    if (status == 0U) {
        return "SUCCESS";
    }
    if (status == (std::numeric_limits<std::uint32_t>::max)()) {
        return "FAILURE";
    }
    return "UNRECOGNIZED_NONZERO";
}

}  // 匿名命名空间

void LogBoardBinding(const std::string &path, const char *event, const char *error) noexcept
{
    const auto level = std::strcmp(event, "failed") == 0 ? VnaLog::Level::ERROR_LEVEL : VnaLog::Level::INFO;
    VnaLog::Log(level, VnaLog::Module::CORE,
        "[VN10] phase=initialize config=0 step=BindLibrary event={} driver_path={} "
        "abi=frequency:uint64_t,power:double,ifbw:uint32_t,ports:uint32_t error={}",
        event, path, error);
}

void LogBoardCallBegin(const char *phase, std::uint64_t config, const char *step) noexcept
{
    VnaLog::Info(VnaLog::Module::CORE, "[VN10] phase={} config={} step={} event=begin", phase, config, step);
}

std::uint32_t LogBoardCallReturned(
    const char *phase, std::uint64_t config, const char *step, std::uint32_t status) noexcept
{
    VnaLog::Log(status == 0U ? VnaLog::Level::INFO : VnaLog::Level::ERROR_LEVEL, VnaLog::Module::CORE,
        "[VN10] phase={} config={} step={} event=returned status_u32={} status_i32={} status_hex=0x{:08x} result={}",
        phase, config, step, status, SignedStatus(status), status, StatusName(status));
    return status;
}

void LogBoardSetterReturned(std::uint64_t config, const char *step) noexcept
{
    VnaLog::Info(VnaLog::Module::CORE,
        "[VN10] phase=configure config={} step={} event=returned status=void validation=DrvDataFrameDownload", config,
        step);
}

void LogBoardConfiguration(std::uint64_t config, const BoardDownloadLists &lists) noexcept
{
    if (!VnaLog::Logger::Instance().Enabled(VnaLog::Level::INFO)) {
        return;
    }
    try {
        Summary(config, "frequency", "uint64_t", "Hz", lists.frequencies);
        Summary(config, "power", "double", "dBm", lists.powers);
        Summary(config, "ifbw", "uint32_t", "Hz", lists.bandwidths);
        Summary(config, "ports", "uint32_t", "one_based", lists.ports);
        VnaLog::Info(VnaLog::Module::CORE, "[VN10] phase=configure config={} event=ports unit=one_based values={}",
            config, ValuesText(lists.ports, 0U, lists.ports.size()));
        if (VnaLog::Logger::Instance().Enabled(VnaLog::Level::DEBUG)) {
            DumpConfiguration(VnaLog::Level::DEBUG, config, lists);
        }
    } catch (...) {
        VnaLog::Logger::ReportFailure();
    }
}

void LogBoardConfigurationFailure(std::uint64_t config, const BoardDownloadLists &lists) noexcept
{
    if (!VnaLog::Logger::Instance().Enabled(VnaLog::Level::ERROR_LEVEL)) {
        return;
    }
    try {
        DumpConfiguration(VnaLog::Level::ERROR_LEVEL, config, lists);
    } catch (...) {
        VnaLog::Logger::ReportFailure();
    }
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
