/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_unit_test_fixtures.h"

namespace VnaTest {
namespace {

constexpr std::size_t PARAMETER_HEADER_VALUES = 1U;
constexpr std::size_t PARAMETERS_PER_POINT = 6U;
constexpr std::size_t POWER_VALUES_PER_POINT = 9U;
constexpr unsigned FREQUENCY_WORD_BITS = 32U;
constexpr std::uint64_t FREQUENCY_STEP = 0x100000003ULL;
constexpr std::uint32_t ENCODED_IF_BANDWIDTH_BASE = 100U;
constexpr std::uint32_t SNAPSHOT_IF_BANDWIDTH_BASE = 1000U;
constexpr std::uint32_t SOURCE_POWER_STRIDE = 100U;
constexpr std::uint32_t POINT_POWER_STRIDE = 10U;
constexpr double POWER_VALUE_FRACTION = 0.5;
constexpr double IQ_TOLERANCE = 1.0e-12;

// 测试夹具使用独立的布局值，不读取生产解码器常量。
enum ParameterOffset {
    FREQUENCY_HIGH_WORD,
    FREQUENCY_LOW_WORD,
    SOURCE_PORT,
    DYNAMIC_LINK,
    IF_BANDWIDTH,
    DYNAMIC_ENABLED
};

}  // 匿名命名空间

EncodedFrame MakeEncodedFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency)
{
    EncodedFrame frame;
    frame.parameters.resize(PARAMETER_HEADER_VALUES + (PARAMETERS_PER_POINT * pointCount), 0U);
    frame.powerTable.resize(POWER_VALUES_PER_POINT * pointCount, 0.0);
    for (std::uint32_t point = 0U; point < pointCount; ++point) {
        const std::uint64_t frequency = firstFrequency + (std::uint64_t{point} * FREQUENCY_STEP);
        const std::size_t base = PARAMETER_HEADER_VALUES + (PARAMETERS_PER_POINT * point);
        frame.parameters[base + FREQUENCY_HIGH_WORD] = static_cast<std::uint32_t>(frequency >> FREQUENCY_WORD_BITS);
        frame.parameters[base + FREQUENCY_LOW_WORD] = static_cast<std::uint32_t>(frequency);
        frame.parameters[base + SOURCE_PORT] = sourcePort;
        frame.parameters[base + DYNAMIC_LINK] = 0U;
        frame.parameters[base + IF_BANDWIDTH] = ENCODED_IF_BANDWIDTH_BASE + point;
        frame.parameters[base + DYNAMIC_ENABLED] = 0U;
        for (std::size_t item = 0U; item < POWER_VALUES_PER_POINT; ++item) {
            frame.powerTable[(POWER_VALUES_PER_POINT * point) + item] =
                static_cast<double>((sourcePort * SOURCE_POWER_STRIDE) + (point * POINT_POWER_STRIDE) + item) +
                POWER_VALUE_FRACTION;
        }
    }
    return frame;
}

VnaNative::NormalFrameSnapshot MakeSnapshot(std::uint32_t sourcePort, const std::vector<std::uint64_t> &frequencies)
{
    VnaNative::NormalFrameSnapshot frame;
    frame.sourcePort = sourcePort;
    frame.points.resize(frequencies.size());
    frame.tableData.resize(frequencies.size() * POWER_VALUES_PER_POINT, 0.0);
    for (std::size_t index = 0U; index < frequencies.size(); ++index) {
        frame.points[index].frequencyRaw = frequencies[index];
        frame.points[index].sourcePort = sourcePort;
        frame.points[index].ifBandwidthRaw = static_cast<std::uint32_t>(SNAPSHOT_IF_BANDWIDTH_BASE + index);
    }
    return frame;
}

void ExpectVectorNear(const std::vector<double> &expected, const std::vector<double> &actual)
{
    VnaTest::Expect(actual.size() == expected.size(), VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) || std::fabs(expected[index] - actual[index]) > IQ_TOLERANCE) {
            VnaTest::Fail(std::string("向量偏移 ") + ToString(index) + " 不符合预期，实际为 " +
                              ToString(actual[index]) + "，期望为 " + ToString(expected[index]),
                VnaTest::TestLocation{__FILE__, __LINE__});
        }
    }
}

}  // 命名空间 VnaTest
