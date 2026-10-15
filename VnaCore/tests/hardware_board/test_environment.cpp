/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "test_environment.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <utility>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace HardwareBoardTest {
namespace {

const std::vector<std::string> g_names = {"VNAPRO_TEST_BOARD_TRACE", "VNAPRO_TEST_BOARD_FAIL",
    "VNAPRO_TEST_BOARD_FAIL_AT", "VNAPRO_TEST_BOARD_GATE_AT", "VNAPRO_SIM_MODE", "VNAPRO_SIM_SEED",
    "VNAPRO_SIM_NOISE_AMPLITUDE", "VNAPRO_TEST_BOARD_FAIL_CODE", "VNAPRO_TEST_BOARD_POWER_PATTERN"};
const std::vector<std::string> g_sentinels = {
    "hardware-probe-mode-unchanged", "hardware-probe-seed-unchanged", "hardware-probe-amplitude-unchanged"};

std::string ReadEnvironment(const std::string &name)
{
#if defined(_WIN32)
    char value[4096] = {};
    const auto size = GetEnvironmentVariableA(name.c_str(), value, sizeof(value));
    return size > 0U && size < sizeof(value) ? std::string(value, size) : std::string();
#else
    const char *value = std::getenv(name.c_str());
    return value != nullptr ? value : "";
#endif
}

void SetEnvironment(const std::string &name, const std::string &value)
{
#if defined(_WIN32)
    const bool ok = _putenv_s(name.c_str(), value.c_str()) == 0 &&
                    SetEnvironmentVariableA(name.c_str(), value.empty() ? nullptr : value.c_str()) != 0;
#else
    const bool ok = value.empty() ? unsetenv(name.c_str()) == 0 : setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    if (!ok) {
        throw std::runtime_error("cannot set probe environment");
    }
}

}  // 匿名命名空间

TestEnvironment::TestEnvironment(std::string trace, const std::string &failure, std::uint32_t failAt,
    std::uint32_t gateAt, std::uint32_t failureCode)
    : trace_(std::move(trace))
{
    std::remove(trace_.c_str());
    std::remove((trace_ + ".gate").c_str());
    const std::vector<std::string> values = {trace_, failure, std::to_string(failAt), std::to_string(gateAt),
        g_sentinels[0U], g_sentinels[1U], g_sentinels[2U], std::to_string(failureCode), "ramp"};
    for (std::size_t index = 0U; index < g_names.size(); ++index) {
        saved_.push_back(ReadEnvironment(g_names[index]));
        SetEnvironment(g_names[index], values[index]);
    }
}

TestEnvironment::TestEnvironment(std::string trace, ExpectedPower power) : TestEnvironment(std::move(trace))
{
    const char *name = power == ExpectedPower::ZERO         ? "zero"
                       : power == ExpectedPower::FRACTIONAL ? "fractional"
                       : power == ExpectedPower::BOUNDARIES ? "boundaries"
                                                            : "ramp";
    SetEnvironment("VNAPRO_TEST_BOARD_POWER_PATTERN", name);
}

TestEnvironment::~TestEnvironment()
{
    ReleaseRead();
    for (std::size_t index = 0U; index < saved_.size(); ++index) {
        try {
            SetEnvironment(g_names[index], saved_[index]);
        } catch (...) {
            // 测试清理失败不能隐藏最初的断言；调用时会立即报告设置错误。
        }
    }
}

std::vector<std::string> TestEnvironment::Events() const
{
    std::ifstream stream(trace_);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(stream, line)) {
        const auto separator = line.find(' ');
        if (separator != std::string::npos) {
            result.push_back(line.substr(separator + 1U));
        }
    }
    return result;
}

void TestEnvironment::AwaitEvent(const std::string &event, std::size_t count) const
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        const auto events = Events();
        const auto matching = std::count_if(events.begin(), events.end(), [&event](const std::string &candidate) {
            return candidate == event || candidate.find(event + " ") == 0U;
        });
        if (static_cast<std::size_t>(matching) >= count) {
            return;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("probe event timed out: " + event);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void TestEnvironment::ReleaseRead() const
{
    std::ofstream stream(trace_ + ".gate");
    stream << "release\n";
}

void TestEnvironment::VerifyThreadAndEnvironment() const
{
    std::ifstream stream(trace_);
    std::string line;
    std::string firstThread;
    while (std::getline(stream, line)) {
        const auto current = line.substr(0U, line.find(' '));
        if (firstThread.empty()) {
            firstThread = current;
        }
        if (current != firstThread) {
            throw std::runtime_error("hardware ABI call crossed worker threads or an unexpected unload occurred");
        }
    }
    for (std::size_t index = 0U; index < g_sentinels.size(); ++index) {
        if (ReadEnvironment(g_names[index + 4U]) != g_sentinels[index]) {
            throw std::runtime_error("hardware path changed simulator environment");
        }
    }
}

}  // 命名空间 HardwareBoardTest
