/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_SWEEP_TYPES_H
#define VNAPRO_BOARD_SWEEP_TYPES_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VnaBoard {

constexpr std::size_t MAX_SWEEP_BYTES = 64U * 1024U * 1024U;
constexpr std::size_t MAX_POINT_COUNT = MAX_SWEEP_BYTES / (16U * sizeof(double));

struct SweepPlan {
    std::vector<std::uint64_t> frequencies;
    // 逐频点功率，单位 dBm；功率暂不参与模拟 IQ 的物理响应。
    std::vector<double> powers;
    std::vector<std::uint32_t> bandwidths;
    // 暂存为原生端口 1～4，ValidatePlan 成功后为已提交信号轴下标 0～3。
    std::vector<std::uint32_t> workPorts;
    std::size_t valuesPerPoint = 0U;
    std::size_t totalValues = 0U;
};

struct StagedPlan {
    SweepPlan plan;
    bool frequenciesValid = false;
    bool powersValid = false;
    bool bandwidthsValid = false;
    bool portsValid = false;
};

struct SweepResult {
    std::shared_ptr<const SweepPlan> plan;
    std::vector<double> values;
    std::size_t availablePoints = 0U;
    bool cancelled = false;
    std::uint32_t error = 0U;
};

enum class SignalMode {
    GOLDEN,
    NOISE
};

struct SignalSettings {
    SignalMode mode = SignalMode::GOLDEN;
    std::uint64_t seed = 42U;
    // 相对于模拟复数波幅值的扰动系数；0.002 保持原有 0.2% 噪声。
    double noiseAmplitude = 0.002;
};

}  // 命名空间 VnaBoard
#endif  // VNAPRO_BOARD_SWEEP_TYPES_H
