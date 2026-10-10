/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H
#define VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../test_support.h"
#include "native_library_probe.h"

namespace VnaTest {

// 运行时加载的 C ABI 契约所使用的独立状态预期值。
enum SimulatorStatus {
    OK = 0U,
    INVALID_ARGUMENT = 1U,
    NOT_INITIALIZED = 2U,
    RF_DISABLED = 3U,
    INVALID_STATE = 4U,
    NOT_SUPPORTED = 5U,
    SIZE_OVERFLOW = 6U,
    BUFFER_TOO_SMALL = 7U,
    INTERNAL_ERROR = 8U
};

struct FrameInput final {
    std::vector<std::uint32_t> parameters;
    std::vector<double> powerTable;
    std::vector<std::uint64_t> frequencies;
};

// 为运行时 ABI 测试使用独立给定的布局值，创建独立持有的输入缓冲区。
FrameInput MakeNormalFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency);

// 使用在整个调用期间有效的自有帧缓冲区，调用已固定的非 const ABI。
std::uint32_t DownloadFrame(const NativeFunctions &functions, FrameInput &frame);

// 独立测试判定器：混合完整原始频率，不调用生产模型。
double FrequencyFactor(std::uint64_t frequency) noexcept;

// 计算独立公式预期值；固定离线基准向量另外检测双方共有的公式错误。
std::vector<double> ExpectedIq(const std::vector<std::uint64_t> &frequencies, std::uint32_t sourcePort);

// 实际缓冲区必须包含 expected.size() 个值；非有限值或超出容差时判定失败。
void ExpectIqNear(const std::vector<double> &expected, const double *actual);

void RunNativeNormalSweepTests(TestRunner &runner, NativeLibraryProbe &library);
void RunNativeNoiseModeTests(TestRunner &runner, NativeLibraryProbe &library);

}  // 命名空间 VnaTest

#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H
