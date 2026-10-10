/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNACORE_INCLUDE_VNA_CORE_PORTFRAME_H
#define VNAPRO_VNACORE_INCLUDE_VNA_CORE_PORTFRAME_H
#include <cstdint>
#include <vector>

namespace VnaCore {

struct PortFrame final {
    std::uint32_t sourcePort = 0U;
    std::vector<double> iq;  // 按采样点排列，每个接收端依次存放 A.real、A.imag、B.real、B.imag。
};

}  // 命名空间 VnaCore

#endif
