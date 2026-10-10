/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H
#define VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H

#include <cstdint>
#include <vector>

#include "vna_native/native_api_contract.h"

namespace VnaNative {

// 持有一次成功启动时冻结的 IQ；后续帧下载不能改变该数据。
struct VNA_NATIVE_HIDDEN NativePendingSweep final {
    bool valid = false;
    std::uint32_t pointCount = 0U;
    std::uint32_t sourcePort = 0U;
    std::vector<double> iqData;

    // 在状态互斥锁下替换或取走待取结果，不进行内存分配。
    void Swap(NativePendingSweep &other) noexcept;
};

}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H
