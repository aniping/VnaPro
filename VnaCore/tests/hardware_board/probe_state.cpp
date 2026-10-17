/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "probe_state.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace HardwareBoardProbe {
namespace {

constexpr std::uint32_t INVALID_CONTRACT = 7901U;
constexpr std::size_t VALUES_PER_POINT = 16U;

std::string Environment(const char *name)
{
#if defined(_WIN32)
    char value[4096] = {};
    const auto size = GetEnvironmentVariableA(name, value, sizeof(value));
    return size > 0U && size < sizeof(value) ? std::string(value, size) : std::string();
#else
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

std::uint32_t Number(const char *name, std::uint32_t fallback)
{
    const auto text = Environment(name);
    return text.empty() ? fallback : static_cast<std::uint32_t>(std::stoul(text));
}

std::string ThreadIdentity()
{
    std::ostringstream value;
    value << std::this_thread::get_id();
    return value.str();
}

}  // 匿名命名空间

ProbeState::~ProbeState()
{
    // 正式调用期间必须保持驻留；进程退出时的析构事件不属于会话关闭的资源释放。
    Record("UNLOAD");
}

void ProbeState::Record(const char *name, const std::string &arguments) const
{
    std::ofstream stream(trace_, std::ios::app);
    stream << ThreadIdentity() << ' ' << name;
    if (!arguments.empty()) {
        stream << ' ' << arguments;
    }
    stream << '\n';
}

bool ProbeState::Enter(const char *name, const std::string &arguments)
{
    Record(name, arguments);
    const auto current = ThreadIdentity();
    if (thread_.empty()) {
        thread_ = current;
    }
    // 故意使用不合法的模拟参数，确认正常采集全过程不会读取或覆盖模拟器配置。
    const bool environmentUnchanged = Environment("VNAPRO_SIM_MODE") == "hardware-probe-mode-unchanged" &&
                                      Environment("VNAPRO_SIM_SEED") == "hardware-probe-seed-unchanged" &&
                                      Environment("VNAPRO_SIM_NOISE_AMPLITUDE") == "hardware-probe-amplitude-unchanged";
    valid_ = valid_ && current == thread_ && environmentUnchanged;
    return valid_;
}

std::uint32_t ProbeState::Failure(const char *name, std::uint32_t occurrence) const
{
    return failure_ == name && occurrence == failAt_ ? failureCode_ : 0U;
}

std::uint32_t ProbeState::Initialize()
{
    // 正式 VN10 库不在关闭时卸载；独立用例借助 INIT 更新测试配置及仪器状态。
    trace_ = Environment("VNAPRO_TEST_BOARD_TRACE");
    failure_ = Environment("VNAPRO_TEST_BOARD_FAIL");
    powerPattern_ = Environment("VNAPRO_TEST_BOARD_POWER_PATTERN");
    failureCode_ = Number("VNAPRO_TEST_BOARD_FAIL_CODE", 731U);
    failAt_ = Number("VNAPRO_TEST_BOARD_FAIL_AT", 1U);
    gateAt_ = Number("VNAPRO_TEST_BOARD_GATE_AT", 0U);
    readDelayMs_ = Number("VNAPRO_TEST_BOARD_READ_DELAY_MS", 0U);
    thread_.clear();
    frequencies_.clear();
    powers_.clear();
    bandwidths_.clear();
    ports_.clear();
    frequencyPointer_ = nullptr;
    powerPointer_ = nullptr;
    bandwidthPointer_ = nullptr;
    portPointer_ = nullptr;
    valid_ = true;
    rf_ = false;
    scanning_ = false;
    sweep_ = 0U;
    next_ = 0U;
    getCalls_ = 0U;
    downloadCalls_ = 0U;
    offCalls_ = 0U;
    if (!Enter("INIT")) {
        return INVALID_CONTRACT;
    }
    const auto failure = Failure("INIT", 1U);
    initialized_ = failure == 0U;
    return failure;
}

std::uint32_t ProbeState::Download()
{
    if (!Enter("DOWNLOAD") || !initialized_ || frequencies_.empty() || powers_.size() != frequencies_.size() ||
        bandwidths_.size() != frequencies_.size() || ports_.empty() || scanning_) {
        return INVALID_CONTRACT;
    }
    valid_ = valid_ && ValidateLists();
    return valid_ ? Failure("DOWNLOAD", ++downloadCalls_) : INVALID_CONTRACT;
}

std::uint32_t ProbeState::Rf(std::uint32_t enabled)
{
    if (!Enter(enabled == 0U ? "RF_OFF" : "RF_ON") || enabled > 1U) {
        return INVALID_CONTRACT;
    }
    const auto failure = Failure(enabled == 0U ? "RF_OFF" : "RF_ON", enabled == 0U ? ++offCalls_ : 1U);
    if (failure == 0U) {
        rf_ = enabled != 0U;
        // RF OFF 只关闭射频端口，不能终止未完成扫描或回收驱动资源。
    }
    return failure;
}

std::uint32_t ProbeState::Start()
{
    if (!Enter("START") || !initialized_ || !rf_ || scanning_ || !ListsRetained()) {
        return INVALID_CONTRACT;
    }
    const auto failure = Failure("START", sweep_ + 1U);
    if (failure == 0U) {
        ++sweep_;
        next_ = 0U;
        scanning_ = true;
    }
    return failure;
}

bool ProbeState::WaitForGate(std::uint32_t occurrence) const
{
    if (gateAt_ != occurrence) {
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::ifstream(trace_ + ".gate").good()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

bool ProbeState::ValidRead(std::uint32_t first, std::uint32_t count, double *data, int capacity) const
{
    if (!initialized_ || !scanning_ || first != next_ || count == 0U || first >= frequencies_.size()) {
        return false;
    }
    return count <= frequencies_.size() - first && data != nullptr &&
           capacity == static_cast<int>(count * ports_.size() * VALUES_PER_POINT);
}

void ProbeState::WriteData(std::uint32_t first, std::uint32_t count, double *data) const
{
    for (std::size_t source = 0U; source < ports_.size(); ++source) {
        for (std::uint32_t point = 0U; point < count; ++point) {
            for (std::size_t component = 0U; component < VALUES_PER_POINT; ++component) {
                const std::size_t offset = (source * count + point) * VALUES_PER_POINT + component;
                // 分配互不重叠的扫次、源端口、频点和分量数值区间，避免组合错位相互抵消。
                data[offset] = static_cast<double>(static_cast<std::uint64_t>(sweep_) * UINT64_C(1000000) +
                                                   ports_[source] * UINT64_C(100000) + (first + point) * UINT64_C(100) +
                                                   component);
            }
        }
    }
}

std::uint32_t ProbeState::Get(std::uint32_t first, std::uint32_t count, double *data, int capacity)
{
    const auto occurrence = ++getCalls_;
    const auto arguments = std::to_string(first) + " " + std::to_string(count) + " " + std::to_string(capacity);
    if (!Enter("GET_BEGIN", arguments) || !ValidRead(first, count, data, capacity) || !ListsRetained() ||
        !WaitForGate(occurrence)) {
        return INVALID_CONTRACT;
    }
    const auto failure = Failure("GET", occurrence);
    std::this_thread::sleep_for(std::chrono::milliseconds(readDelayMs_));
    if (failure != 0U) {
        data[0] = -1234567.0;  // 原生失败可能已改写输出缓冲区，核心仍必须保留上一完整帧。
        Record("GET_FAIL", arguments);
        return failure;
    }
    WriteData(first, count, data);
    next_ += count;
    scanning_ = next_ != frequencies_.size();
    Record("GET_END", arguments);
    return 0U;
}

ProbeState &State()
{
    static ProbeState state;
    return state;
}

}  // 命名空间 HardwareBoardProbe
