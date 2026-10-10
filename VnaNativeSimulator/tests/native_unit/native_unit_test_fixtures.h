/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H
#define VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../test_support.h"
#include "normal_frame_decoder.h"

namespace VnaTest {

struct EncodedFrame final {
    std::vector<std::uint32_t> parameters;
    std::vector<double> powerTable;
};

// 使用独立的协议常量和频率高字步进，构造独立持有的普通帧。
EncodedFrame MakeEncodedFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency);

// 将传入的原始频率复制到快照；返回后可释放输入存储。
VnaNative::NormalFrameSnapshot MakeSnapshot(std::uint32_t sourcePort, const std::vector<std::uint64_t> &frequencies);

// 要求长度匹配、实际值有限，且误差不超过固定 IQ 容差。
void ExpectVectorNear(const std::vector<double> &expected, const std::vector<double> &actual);

}  // 命名空间 VnaTest

#endif  // VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H
