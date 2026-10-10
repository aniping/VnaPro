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

// Builds an owned normal frame with independent protocol constants and a high-word frequency step.
EncodedFrame MakeEncodedFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency);

// Copies the supplied raw frequencies into a snapshot; input storage may be discarded after return.
VnaNative::NormalFrameSnapshot MakeSnapshot(std::uint32_t sourcePort, const std::vector<std::uint64_t> &frequencies);

// Requires matching lengths and finite actual values within the fixed IQ tolerance.
void ExpectVectorNear(const std::vector<double> &expected, const std::vector<double> &actual);

}  // namespace VnaTest

#endif  // VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H
