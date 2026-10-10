/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_FRAME_LAYOUT_H
#define VNAPRO_FRAME_LAYOUT_H
#include <cstddef>
#include <cstdint>

namespace VnaCore {
constexpr std::uint32_t PHYSICAL_PORT_COUNT = 4U;
constexpr std::uint32_t ALL_RECEIVERS_MASK = 0xFU;
constexpr std::size_t IQ_VALUES_PER_RECEIVER = 4U;
constexpr std::size_t IQ_VALUES_PER_POINT = IQ_VALUES_PER_RECEIVER * PHYSICAL_PORT_COUNT;
constexpr std::size_t COMPLEX_COMPONENT_COUNT = 2U;
constexpr std::size_t TABLE_VALUES_PER_POINT = 9U;
}  // 命名空间 VnaCore
#endif
