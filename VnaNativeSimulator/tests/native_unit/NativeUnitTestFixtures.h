#ifndef VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H
#define VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H

#include "../TestSupport.h"
#include "NormalFrameDecoder.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vna_test {

struct EncodedFrame {
    std::vector<std::uint32_t> parameters;
    std::vector<double> powerTable;
};

inline EncodedFrame MakeEncodedFrame(std::uint32_t pointCount,
                                     std::uint32_t sourcePort,
                                     std::uint64_t firstFrequency) {
    EncodedFrame frame;
    frame.parameters.resize(1U + 6U * pointCount, 0U);
    frame.powerTable.resize(9U * pointCount, 0.0);
    for (std::uint32_t point = 0U; point < pointCount; ++point) {
        const std::uint64_t frequency =
            firstFrequency + static_cast<std::uint64_t>(point) * 0x100000003ULL;
        const std::size_t base = 1U + 6U * point;
        frame.parameters[base] = static_cast<std::uint32_t>(frequency >> 32U);
        frame.parameters[base + 1U] = static_cast<std::uint32_t>(frequency);
        frame.parameters[base + 2U] = sourcePort;
        frame.parameters[base + 3U] = 0U;
        frame.parameters[base + 4U] = 100U + point;
        frame.parameters[base + 5U] = 0U;
        for (std::size_t item = 0U; item < 9U; ++item) {
            frame.powerTable[9U * point + item] =
                static_cast<double>(sourcePort * 100U + point * 10U + item) +
                0.5;
        }
    }
    return frame;
}

inline vna_native::NormalFrameSnapshot MakeSnapshot(
    std::uint32_t sourcePort,
    const std::vector<std::uint64_t>& frequencies) {
    vna_native::NormalFrameSnapshot frame;
    frame.sourcePort = sourcePort;
    frame.points.resize(frequencies.size());
    frame.tableData.resize(frequencies.size() * 9U, 0.0);
    for (std::size_t index = 0U; index < frequencies.size(); ++index) {
        frame.points[index].frequencyRaw = frequencies[index];
        frame.points[index].sourcePort = sourcePort;
        frame.points[index].ifBandwidthRaw =
            static_cast<std::uint32_t>(1000U + index);
    }
    return frame;
}

inline void ExpectVectorNear(const std::vector<double>& expected,
                             const std::vector<double>& actual) {
    VNA_EXPECT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) ||
            std::fabs(expected[index] - actual[index]) > 1.0e-12) {
            VNA_FAIL(std::string("向量偏移 ") + ToString(index) +
                     " 不符合预期，实际为 " + ToString(actual[index]) +
                     "，期望为 " + ToString(expected[index]));
        }
    }
}

}  // namespace vna_test

#endif  // VNAPRO_TESTS_NATIVE_UNIT_NATIVE_UNIT_TEST_FIXTURES_H
