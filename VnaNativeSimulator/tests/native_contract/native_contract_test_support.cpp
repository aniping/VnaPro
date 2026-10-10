/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_contract_test_support.h"

namespace vna_test {
namespace {

// Independent contract expectations must not use the production decoder/model constants.
constexpr std::size_t PARAMETER_HEADER_VALUES = 1U;
constexpr std::size_t PARAMETERS_PER_POINT = 6U;
constexpr std::size_t POWER_VALUES_PER_POINT = 9U;
constexpr std::size_t IQ_VALUES_PER_POINT = 16U;
constexpr std::size_t IQ_VALUES_PER_RECEIVER = 4U;
constexpr std::uint32_t RECEIVER_PORT_COUNT = 4U;
constexpr std::uint32_t IF_BANDWIDTH_BASE = 1000U;
constexpr std::uint64_t FREQUENCY_STEP = 1234567ULL;
constexpr unsigned FREQUENCY_WORD_BITS = 32U;
constexpr unsigned FREQUENCY_MIX_SHIFT = 33U;
constexpr unsigned DOUBLE_PRECISION_SHIFT = 11U;
constexpr std::uint64_t FREQUENCY_MIX_MULTIPLIER_A = 0xff51afd7ed558ccdULL;
constexpr std::uint64_t FREQUENCY_MIX_MULTIPLIER_B = 0xc4ceb9fe1a85ec53ULL;
constexpr double DOUBLE_INTEGER_SCALE = 9007199254740992.0;
constexpr double INCIDENT_RECEIVER_GAIN = 0.1;
constexpr double INCIDENT_SOURCE_GAIN = 0.01;
constexpr double FREQUENCY_RESPONSE_GAIN = 0.001;
constexpr double REFLECTION_GAIN = 0.20;
constexpr double TRANSMISSION_GAIN = 0.02;
constexpr double PORT_PHASE_GAIN = 0.005;
constexpr double IQ_TOLERANCE = 1.0e-12;

enum ParameterOffset {
    FREQUENCY_HIGH_WORD,
    FREQUENCY_LOW_WORD,
    SOURCE_PORT,
    DYNAMIC_LINK,
    IF_BANDWIDTH,
    DYNAMIC_ENABLED
};

}  // namespace

FrameInput MakeNormalFrame(std::uint32_t pointCount, std::uint32_t sourcePort, std::uint64_t firstFrequency)
{
    FrameInput frame;
    frame.parameters.resize(PARAMETER_HEADER_VALUES + (PARAMETERS_PER_POINT * pointCount), 0U);
    frame.powerTable.resize(POWER_VALUES_PER_POINT * pointCount, 0.0);
    frame.frequencies.reserve(pointCount);
    frame.parameters[0] = 0U;

    for (std::uint32_t point = 0U; point < pointCount; ++point) {
        const std::uint64_t frequency = firstFrequency + (std::uint64_t{point} * FREQUENCY_STEP);
        const std::size_t base = PARAMETER_HEADER_VALUES + (PARAMETERS_PER_POINT * point);
        frame.frequencies.push_back(frequency);
        frame.parameters[base + FREQUENCY_HIGH_WORD] = static_cast<std::uint32_t>(frequency >> FREQUENCY_WORD_BITS);
        frame.parameters[base + FREQUENCY_LOW_WORD] = static_cast<std::uint32_t>(frequency);
        frame.parameters[base + SOURCE_PORT] = sourcePort;
        frame.parameters[base + DYNAMIC_LINK] = 0U;
        frame.parameters[base + IF_BANDWIDTH] = IF_BANDWIDTH_BASE + point;
        frame.parameters[base + DYNAMIC_ENABLED] = 0U;
    }
    return frame;
}

std::uint32_t DownloadFrame(const NativeFunctions &functions, FrameInput &frame)
{
    const std::uint32_t pointCount = static_cast<std::uint32_t>(frame.frequencies.size());
    return functions.dataFrameDownload(pointCount, frame.parameters.data(),
        static_cast<std::uint32_t>(frame.parameters.size()), frame.powerTable.data(),
        static_cast<std::uint32_t>(frame.powerTable.size()));
}

double FrequencyFactor(std::uint64_t frequency) noexcept
{
    std::uint64_t mixed = frequency;
    mixed = (mixed ^ (mixed >> FREQUENCY_MIX_SHIFT)) * FREQUENCY_MIX_MULTIPLIER_A;
    mixed = (mixed ^ (mixed >> FREQUENCY_MIX_SHIFT)) * FREQUENCY_MIX_MULTIPLIER_B;
    mixed ^= mixed >> FREQUENCY_MIX_SHIFT;
    return static_cast<double>(mixed >> DOUBLE_PRECISION_SHIFT) / DOUBLE_INTEGER_SCALE;
}

std::vector<double> ExpectedIq(const std::vector<std::uint64_t> &frequencies, std::uint32_t sourcePort)
{
    std::vector<double> output(frequencies.size() * IQ_VALUES_PER_POINT, 0.0);
    const double sourceLabel = static_cast<double>(sourcePort + 1U);

    for (std::size_t point = 0U; point < frequencies.size(); ++point) {
        const double factor = FrequencyFactor(frequencies[point]);
        const double sourceAReal = 1.0 + INCIDENT_RECEIVER_GAIN * sourceLabel + INCIDENT_SOURCE_GAIN * sourceLabel;
        const double sourceAImag = FREQUENCY_RESPONSE_GAIN * factor;

        for (std::uint32_t receiver = 1U; receiver <= RECEIVER_PORT_COUNT; ++receiver) {
            const double receiverLabel = static_cast<double>(receiver);
            const double aReal = 1.0 + INCIDENT_RECEIVER_GAIN * receiverLabel + INCIDENT_SOURCE_GAIN * sourceLabel;
            const double aImag = FREQUENCY_RESPONSE_GAIN * factor;
            const double hReal =
                (receiver == sourcePort + 1U ? REFLECTION_GAIN : TRANSMISSION_GAIN * receiverLabel * sourceLabel) +
                FREQUENCY_RESPONSE_GAIN * factor;
            const double hImag = PORT_PHASE_GAIN * (receiverLabel - sourceLabel);
            const double bReal = hReal * sourceAReal - hImag * sourceAImag;
            const double bImag = hReal * sourceAImag + hImag * sourceAReal;
            const std::size_t base = (point * IQ_VALUES_PER_POINT) + ((receiver - 1U) * IQ_VALUES_PER_RECEIVER);
            output[base] = aReal;
            output[base + 1U] = aImag;
            output[base + 2U] = bReal;
            output[base + 3U] = bImag;
        }
    }
    return output;
}

void ExpectIqNear(const std::vector<double> &expected, const double *actual)
{
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) || std::fabs(expected[index] - actual[index]) > IQ_TOLERANCE) {
            vna_test::Fail(std::string("IQ 偏移 ") + ToString(index) + " 不符合黄金值，实际为 " +
                               ToString(actual[index]) + "，期望为 " + ToString(expected[index]),
                vna_test::TestLocation{__FILE__, __LINE__});
        }
    }
}

}  // namespace vna_test
