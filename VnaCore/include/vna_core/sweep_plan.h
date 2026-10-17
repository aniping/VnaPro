/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_SWEEP_PLAN_H
#define VNAPRO_VNA_CORE_SWEEP_PLAN_H

#include <cstdint>
#include <vector>

namespace VnaCore {

constexpr double MIN_POWER_DBM = -120.0;
constexpr double MAX_POWER_DBM = 100.0;
constexpr double MAX_SWEEP_TIME_SECONDS = 100000.0;

struct SweepPlan final {
    std::vector<std::uint64_t> frequencyRaw;
    std::vector<double> frequencyHz;
    // 逐点功率 dBm；VN10 使用 double 数组，空数组兼容为 0 dBm，产品启动显式填写。
    std::vector<double> powerDbm;
    std::uint32_t ifBandwidthRaw = 0U;
    // 手动值约束整轮主机回读的最小节拍，不代表板卡 ADC 的采样时刻。
    bool sweepTimeAuto = true;
    double sweepTimeSeconds = 1.0;
    std::uint32_t receiverMask = 0U;
    std::vector<std::uint32_t> sourcePorts;  // 物理端口编号为 1～4。
    std::vector<double> tableData;  // 旧 VN08 接口需要的 9N 个不透明表值；新 VN10 接口可为空。
};

}  // 命名空间 VnaCore

#endif
