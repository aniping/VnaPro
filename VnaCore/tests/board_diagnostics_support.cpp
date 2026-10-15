/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "board_diagnostics_support.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "hardware_board/test_support.h"
#include "vna_log/logger.h"

namespace BoardDiagnosticsTest {
namespace {
using HardwareBoardTest::Require;

bool Matches(const std::string &record, const std::vector<std::string> &needles)
{
    return std::all_of(needles.begin(), needles.end(), [&record](const std::string &needle) {
        return record.find(needle) != std::string::npos;
    });
}

std::vector<double> Values(const std::string &record)
{
    const auto first = record.find("values=[");
    Require(first != std::string::npos, "Diagnostic values missing: " + record);
    const auto end = record.find(']', first);
    Require(end != std::string::npos, "Diagnostic values not closed: " + record);
    std::istringstream stream(record.substr(first + 8U, end - first - 8U));
    std::vector<double> result;
    std::string item;
    while (std::getline(stream, item, ',')) {
        std::size_t parsed = 0U;
        result.push_back(std::stod(item, &parsed));
        Require(item.find_first_not_of(" \t", parsed) == std::string::npos, "Unparsed diagnostic value suffix");
    }
    return result;
}
}  // 匿名命名空间

Capture::Capture(std::string path, VnaLog::Level level) : path_(std::move(path))
{
    std::remove(path_.c_str());
    VnaLog::LogConfig config;
    config.level = level;
    config.consoleEnabled = false;
    config.filePath = path_;
    config.pattern = "%v";
    config.flushLevel = VnaLog::Level::TRACE;
    VnaLog::Logger::Instance().Configure(config);
}

Capture::~Capture()
{
    VnaLog::Logger::Instance().Shutdown();
}

Records Capture::Read() const
{
    VnaLog::Logger::Instance().Flush();
    std::ifstream stream(path_);
    Records result;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("[VN10]") == 0U) {
            result.push_back(line);
        }
    }
    return result;
}

std::string Field(const std::string &record, const std::string &key)
{
    const auto prefix = " " + key + "=";
    const auto start = record.find(prefix);
    Require(start != std::string::npos, "Diagnostic field missing: " + key + " in " + record);
    const auto first = start + prefix.size();
    const auto end = record.find(' ', first);
    return record.substr(first, end == std::string::npos ? end : end - first);
}

std::size_t Find(const Records &records, const std::vector<std::string> &needles)
{
    for (std::size_t index = 0U; index < records.size(); ++index) {
        if (Matches(records[index], needles)) {
            return index;
        }
    }
    throw std::runtime_error("Required [VN10] diagnostic missing: " + (needles.empty() ? "record" : needles.back()));
}

void VerifyStatus(const std::string &record, std::uint32_t status)
{
    auto signedStatus = static_cast<std::int64_t>(status);
    if (status > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
        signedStatus -= std::int64_t{1} << 32U;
    }
    Require(std::stoull(Field(record, "status_u32")) == status, "Diagnostic unsigned status changed");
    Require(std::stoll(Field(record, "status_i32")) == signedStatus, "Diagnostic signed status changed");
    const auto hex = Field(record, "status_hex");
    Require(hex.find("0x") == 0U && std::stoull(hex, nullptr, 16) == status, "Diagnostic hexadecimal status changed");
    const char *result = status == 0U                                            ? "SUCCESS"
                         : status == (std::numeric_limits<std::uint32_t>::max)() ? "FAILURE"
                                                                                 : "UNRECOGNIZED_NONZERO";
    Require(Field(record, "result") == result, "Native status interpretation exceeded the confirmed contract");
}

void VerifySummary(const Records &records, std::uint64_t config, const Summary &expected)
{
    const auto &record = records[Find(
        records, {"phase=configure", "config=" + std::to_string(config) + " ", "name=" + expected.name + " ",
                     "element=" + expected.element + " ", "unit=" + expected.unit + " "})];
    Require(std::stoull(Field(record, "count")) == expected.values.size(), "Diagnostic summary count changed");
    Require(std::stod(Field(record, "first")) == expected.values.front() &&
                std::stod(Field(record, "last")) == expected.values.back(),
        "Diagnostic summary endpoints changed");
    Require(std::stod(Field(record, "min")) == *std::min_element(expected.values.begin(), expected.values.end()) &&
                std::stod(Field(record, "max")) == *std::max_element(expected.values.begin(), expected.values.end()),
        "Diagnostic summary extrema changed");
    const bool uniform = std::all_of(expected.values.begin(), expected.values.end(), [&expected](double value) {
        return value == expected.values.front();
    });
    Require(Field(record, "uniform") == (uniform ? "true" : "false"), "Diagnostic uniform metadata changed");
    if (expected.name == "ports") {
        const auto &ports = records[Find(
            records, {"phase=configure", "config=" + std::to_string(config) + " ", "event=ports", "unit=zero_based"})];
        Require(Values(ports) == expected.values, "Diagnostic ports must be complete and zero based");
    }
}

void VerifyValues(const Records &records, std::uint64_t config, const Summary &expected)
{
    std::vector<double> actual;
    for (const auto &record : records) {
        if (!Matches(record, {"phase=configure", "config=" + std::to_string(config) + " ", "event=values",
                                 "name=" + expected.name + " "})) {
            continue;
        }
        const auto chunk = Values(record);
        Require(std::stoull(Field(record, "offset")) == actual.size(), "Diagnostic chunk omitted or duplicated");
        Require(std::stoull(Field(record, "count")) == chunk.size() && !chunk.empty() && chunk.size() <= 16U,
            "Diagnostic chunk size differs from the declared count or exceeds sixteen");
        actual.insert(actual.end(), chunk.begin(), chunk.end());
    }
    Require(actual == expected.values, "Full diagnostic list cannot reconstruct actual " + expected.name);
}
}  // 命名空间 BoardDiagnosticsTest
