#ifndef VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H
#define VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../TestSupport.h"
#include "NativeLibraryProbe.h"

namespace vna_test {

enum SimulatorStatus {
    kOk = 0U,
    kInvalidArgument = 1U,
    kNotInitialized = 2U,
    kRfDisabled = 3U,
    kInvalidState = 4U,
    kNotSupported = 5U,
    kSizeOverflow = 6U,
    kBufferTooSmall = 7U,
    kInternalError = 8U
};

struct FrameInput {
    std::vector<std::uint32_t> parameters;
    std::vector<double> powerTable;
    std::vector<std::uint64_t> frequencies;
};

inline FrameInput MakeNormalFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency)
{
    FrameInput frame;
    frame.parameters.resize(1U + 6U * pointCount, 0U);
    frame.powerTable.resize(9U * pointCount, 0.0);
    frame.frequencies.reserve(pointCount);
    frame.parameters[0] = 0U;

    for (std::uint32_t point = 0U; point < pointCount; ++point) {
        const std::uint64_t frequency = firstFrequency + static_cast<std::uint64_t>(point) * 1234567ULL;
        const std::size_t base = 1U + 6U * point;
        frame.frequencies.push_back(frequency);
        frame.parameters[base] = static_cast<std::uint32_t>(frequency >> 32U);
        frame.parameters[base + 1U] = static_cast<std::uint32_t>(frequency);
        frame.parameters[base + 2U] = sourcePort;
        frame.parameters[base + 3U] = 0U;
        frame.parameters[base + 4U] = 1000U + point;
        frame.parameters[base + 5U] = 0U;
    }
    return frame;
}

inline std::uint32_t DownloadFrame(const NativeFunctions& functions, FrameInput* frame)
{
    const std::uint32_t pointCount = static_cast<std::uint32_t>(frame->frequencies.size());
    return functions.dataFrameDownload(pointCount, frame->parameters.data(),
        static_cast<std::uint32_t>(frame->parameters.size()), frame->powerTable.data(),
        static_cast<std::uint32_t>(frame->powerTable.size()));
}

inline double FrequencyFactor(std::uint64_t frequency)
{
    std::uint64_t mixed = frequency;
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xff51afd7ed558ccd);
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xc4ceb9fe1a85ec53);
    mixed ^= mixed >> 33U;
    return static_cast<double>(mixed >> 11U) / 9007199254740992.0;
}

inline std::vector<double> ExpectedIq(const std::vector<std::uint64_t>& frequencies, std::uint32_t sourcePort)
{
    std::vector<double> output(frequencies.size() * 16U, 0.0);
    const double sourceLabel = static_cast<double>(sourcePort + 1U);

    for (std::size_t point = 0U; point < frequencies.size(); ++point) {
        const double factor = FrequencyFactor(frequencies[point]);
        const double sourceAReal = 1.0 + 0.1 * sourceLabel + 0.01 * sourceLabel;
        const double sourceAImag = 0.001 * factor;

        for (std::uint32_t receiver = 1U; receiver <= 4U; ++receiver) {
            const double receiverLabel = static_cast<double>(receiver);
            const double aReal = 1.0 + 0.1 * receiverLabel + 0.01 * sourceLabel;
            const double aImag = 0.001 * factor;
            const double hReal =
                (receiver == sourcePort + 1U ? 0.20 : 0.02 * receiverLabel * sourceLabel) + 0.001 * factor;
            const double hImag = 0.005 * (receiverLabel - sourceLabel);
            const double bReal = hReal * sourceAReal - hImag * sourceAImag;
            const double bImag = hReal * sourceAImag + hImag * sourceAReal;
            const std::size_t base = point * 16U + (receiver - 1U) * 4U;
            output[base] = aReal;
            output[base + 1U] = aImag;
            output[base + 2U] = bReal;
            output[base + 3U] = bImag;
        }
    }
    return output;
}

inline void ExpectIqNear(const std::vector<double>& expected, const double* actual)
{
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) || std::fabs(expected[index] - actual[index]) > 1.0e-12) {
            VNA_FAIL(std::string("IQ 偏移 ") + ToString(index) + " 不符合黄金值，实际为 " + ToString(actual[index]) +
                     "，期望为 " + ToString(expected[index]));
        }
    }
}

void RunNativeNormalSweepTests(TestRunner* runner, NativeLibraryProbe* library);
void RunNativeNoiseModeTests(TestRunner* runner, NativeLibraryProbe* library);

}  // namespace vna_test

#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_CONTRACT_TEST_SUPPORT_H
