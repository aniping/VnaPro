/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <cstdint>
#include <limits>

#include "board_diagnostics_support.h"
#include "device/board/board_diagnostics.h"
#include "hardware_board/test_support.h"

namespace BoardDiagnosticsTest {
namespace {

VnaCore::Detail::BoardDownloadLists ExtremeLists()
{
    VnaCore::Detail::BoardDownloadLists lists;
    lists.frequencies = {UINT64_C(10000000), UINT64_C(8000000000), (std::numeric_limits<std::uint64_t>::max)()};
    lists.powers = {-120.0, -17.500000000000004, 100.0};
    lists.bandwidths = {1U, 1000U, (std::numeric_limits<std::uint32_t>::max)()};
    lists.ports = {4U, 1U};
    return lists;
}

std::vector<Summary> Summaries(const VnaCore::Detail::BoardDownloadLists &lists)
{
    return {{"frequency", "uint64_t", "Hz", {lists.frequencies.begin(), lists.frequencies.end()}},
        {"power", "double", "dBm", lists.powers},
        {"ifbw", "uint32_t", "Hz", {lists.bandwidths.begin(), lists.bandwidths.end()}},
        {"ports", "uint32_t", "one_based", {lists.ports.begin(), lists.ports.end()}}};
}

void VerifyExactValues(const Records &records, const VnaCore::Detail::BoardDownloadLists &lists)
{
    for (const auto &summary : Summaries(lists)) {
        VerifySummary(records, 41U, summary);
        VerifyValues(records, 41U, summary);
    }
    Find(records, {"event=values", "name=frequency ", "10000000,8000000000,18446744073709551615"});
    Find(records, {"event=values", "name=ifbw ", "1,1000,4294967295"});
    Find(records, {"event=values", "name=power ", "-17.500000000000004"});
}

VnaCore::Detail::BoardDownloadLists LargeLists()
{
    VnaCore::Detail::BoardDownloadLists lists;
    for (std::size_t index = 0U; index < 4000U; ++index) {
        lists.frequencies.push_back(UINT64_C(10000000) + UINT64_C(1000000) * index);
        lists.powers.push_back(index % 2U == 0U ? -17.5 : 0.25);
        lists.bandwidths.push_back(1000U);
    }
    lists.ports = {1U, 4U};
    return lists;
}

void VerifyLargeLists(const std::string &prefix)
{
    Capture capture(prefix + "-4000.log", VnaLog::Level::DEBUG);
    const auto lists = LargeLists();
    VnaCore::Detail::LogBoardConfiguration(4000U, lists);
    const auto records = capture.Read();
    for (const auto &summary : Summaries(lists)) {
        VerifyValues(records, 4000U, summary);
    }
    HardwareBoardTest::Require(std::count_if(records.begin(), records.end(),
                                   [](const std::string &record) {
                                       return record.find("event=values") != std::string::npos;
                                   }) == 751,
        "4000-point diagnostics must contain 250 blocks per axis and one ports block");
    HardwareBoardTest::Require(std::all_of(records.begin(), records.end(),
                                   [](const std::string &record) {
                                       return record.size() < 4096U;
                                   }),
        "A large diagnostic list exceeded the bounded record size");
}
}  // 匿名命名空间

void VerifyPureExtremes(const std::string &prefix)
{
    const auto lists = ExtremeLists();
    {
        Capture capture(prefix + "-extremes.log", VnaLog::Level::DEBUG);
        VnaCore::Detail::LogBoardConfiguration(41U, lists);
        VerifyExactValues(capture.Read(), lists);
    }
    {
        Capture capture(prefix + "-pure-off.log", VnaLog::Level::OFF);
        VnaCore::Detail::LogBoardConfiguration(42U, lists);
        VnaCore::Detail::LogBoardConfigurationFailure(42U, lists);
        HardwareBoardTest::Require(capture.Read().empty(), "Disabled pure diagnostics emitted records");
    }
    VerifyLargeLists(prefix);
}
}  // 命名空间 BoardDiagnosticsTest
