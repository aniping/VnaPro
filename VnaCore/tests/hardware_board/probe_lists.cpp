/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <cmath>

#include "probe_state.h"

namespace HardwareBoardProbe {
namespace {

double ExpectedPower(const std::string &pattern, std::size_t point)
{
    if (pattern == "ramp") {
        return 17.0 + static_cast<double>(point % 7U);
    }
    if (pattern == "fixed-negative") {
        return -17.5;
    }
    if (pattern == "fractional") {
        return point % 2U == 0U ? -17.5 : 0.25;
    }
    if (pattern == "boundaries") {
        return point % 2U == 0U ? -120.0 : 100.0;
    }
    return 0.0;
}

bool ValidPorts(std::vector<std::uint32_t> ports)
{
    // 只排序验证副本，设备接收和 IQ 源块均保留调用方顺序。
    std::sort(ports.begin(), ports.end());
    return ports.front() >= 1U && ports.back() <= 4U && std::adjacent_find(ports.begin(), ports.end()) == ports.end();
}

std::string PortValues(const std::vector<std::uint32_t> &ports)
{
    std::string result;
    for (const auto port : ports) {
        result += result.empty() ? std::to_string(port) : "," + std::to_string(port);
    }
    return result;
}

}  // 匿名命名空间

void ProbeState::SetFrequencies(std::uint64_t *values, std::uint32_t count)
{
    if (!Enter("FREQ", std::to_string(count)) || !initialized_ || values == nullptr || count == 0U || scanning_) {
        valid_ = false;
        return;
    }
    frequencies_.assign(values, values + count);
    frequencyPointer_ = values;
}

void ProbeState::SetBandwidths(std::uint32_t *values, std::uint32_t count)
{
    if (!Enter("IFBW", std::to_string(count)) || !initialized_ || values == nullptr || count == 0U || scanning_) {
        valid_ = false;
        return;
    }
    bandwidths_.assign(values, values + count);
    bandwidthPointer_ = values;
}

void ProbeState::SetPowers(double *values, std::uint32_t count)
{
    if (!Enter("POWER", std::to_string(count)) || !initialized_ || values == nullptr || count == 0U || scanning_) {
        valid_ = false;
        return;
    }
    powers_.assign(values, values + count);
    powerPointer_ = values;
    valid_ = valid_ && std::all_of(powers_.begin(), powers_.end(), [](double value) {
        return std::isfinite(value) && value >= -120.0 && value <= 100.0;
    });
}

void ProbeState::SetPorts(std::uint32_t *ports, std::uint32_t count)
{
    if (!Enter("PORTS", std::to_string(count)) || !initialized_ || ports == nullptr || count == 0U || count > 4U ||
        scanning_) {
        valid_ = false;
        return;
    }
    ports_.assign(ports, ports + count);
    portPointer_ = ports;
    valid_ = ValidPorts(ports_);
    if (!valid_) {
        Record("PORTS_INVALID", PortValues(ports_));
    }
}

bool ProbeState::ValidateLists() const
{
    // 期望模式由调用方独立设置，不能从实际功率推断模式而放过错误的全零列表。
    for (std::size_t point = 0U; point < frequencies_.size(); ++point) {
        if (frequencies_[point] != 10000000U + 1000000U * point || bandwidths_[point] != 1000U ||
            powers_[point] != ExpectedPower(powerPattern_, point)) {
            return false;
        }
    }
    return true;
}

bool ProbeState::ListsRetained() const
{
    // 供应接口没有承诺复制列表，测试采用借用当前列表指针的合法实现。
    return frequencyPointer_ != nullptr && powerPointer_ != nullptr && bandwidthPointer_ != nullptr &&
           portPointer_ != nullptr && std::equal(frequencies_.begin(), frequencies_.end(), frequencyPointer_) &&
           std::equal(powers_.begin(), powers_.end(), powerPointer_) &&
           std::equal(bandwidths_.begin(), bandwidths_.end(), bandwidthPointer_) &&
           std::equal(ports_.begin(), ports_.end(), portPointer_);
}

}  // 命名空间 HardwareBoardProbe
