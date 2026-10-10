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

namespace vna_test {

// Independent status expectations for the runtime-loaded C ABI contract.
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

// Creates owned input buffers using independently fixed layout values for the runtime ABI test.
FrameInput MakeNormalFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency);

// Calls the frozen non-const ABI with owned frame buffers that outlive the call.
std::uint32_t DownloadFrame(const NativeFunctions &functions, FrameInput &frame);

// Independent test oracle: mixes the entire raw frequency without calling the production model.
double FrequencyFactor(std::uint64_t frequency) noexcept;

// Computes the independent formula oracle; fixed offline golden vectors additionally detect shared formula errors.
std::vector<double> ExpectedIq(const std::vector<std::uint64_t> &frequencies, std::uint32_t sourcePort);

// The actual buffer must contain expected.size() values; non-finite or out-of-tolerance values fail.
void ExpectIqNear(const std::vector<double> &expected, const double *actual);

void RunNativeNormalSweepTests(TestRunner &runner, NativeLibraryProbe &library);
void RunNativeNoiseModeTests(TestRunner &runner, NativeLibraryProbe &library);

}  // namespace vna_test

#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H
