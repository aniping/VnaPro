/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "signal_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>

namespace VnaNative {
namespace {

constexpr unsigned FREQUENCY_MIX_SHIFT = 33U;
constexpr unsigned DOUBLE_PRECISION_SHIFT = 11U;
constexpr std::uint64_t FREQUENCY_MIX_MULTIPLIER_A = 0xff51afd7ed558ccdULL;
constexpr std::uint64_t FREQUENCY_MIX_MULTIPLIER_B = 0xc4ceb9fe1a85ec53ULL;
constexpr double DOUBLE_INTEGER_SCALE = 9007199254740992.0;
constexpr unsigned NOISE_MIX_SHIFT_A = 30U;
constexpr unsigned NOISE_MIX_SHIFT_B = 27U;
constexpr unsigned NOISE_MIX_SHIFT_C = 31U;
constexpr std::uint64_t NOISE_MIX_MULTIPLIER_A = 0xbf58476d1ce4e5b9ULL;
constexpr std::uint64_t NOISE_MIX_MULTIPLIER_B = 0x94d049bb133111ebULL;
constexpr std::uint64_t SWEEP_NOISE_SALT = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t CHANNEL_NOISE_SALT = 0xd1b54a32d192ed03ULL;
constexpr std::uint64_t IMAGINARY_NOISE_SALT = 0xa0761d6478bd642fULL;
constexpr std::size_t COMPLEX_COMPONENT_COUNT = 2U;
constexpr std::size_t WAVES_PER_RECEIVER = 2U;
constexpr std::size_t IQ_VALUES_PER_RECEIVER = COMPLEX_COMPONENT_COUNT * WAVES_PER_RECEIVER;
constexpr std::size_t NOISE_CHANNELS_PER_SOURCE = NATIVE_PORT_COUNT * WAVES_PER_RECEIVER;
constexpr double INCIDENT_RECEIVER_GAIN = 0.1;
constexpr double INCIDENT_SOURCE_GAIN = 0.01;
constexpr double FREQUENCY_RESPONSE_GAIN = 0.001;
constexpr double REFLECTION_GAIN = 0.20;
constexpr double TRANSMISSION_GAIN = 0.02;
constexpr double PORT_PHASE_GAIN = 0.005;
// 每个轴的扰动不超过 0.2% / sqrt(2)，因此复数扰动幅值不超过 0.2%。
constexpr double NOISE_AXIS_RATIO = 0.001414213562373095;

double FrequencyFactor(std::uint64_t frequencyRaw) noexcept
{
    // 无符号整数回绕是预期行为；频率的全部 64 位均参与此测试因子的计算。
    std::uint64_t mixed = frequencyRaw;
    mixed = (mixed ^ (mixed >> FREQUENCY_MIX_SHIFT)) * FREQUENCY_MIX_MULTIPLIER_A;
    mixed = (mixed ^ (mixed >> FREQUENCY_MIX_SHIFT)) * FREQUENCY_MIX_MULTIPLIER_B;
    mixed ^= mixed >> FREQUENCY_MIX_SHIFT;
    return static_cast<double>(mixed >> DOUBLE_PRECISION_SHIFT) / DOUBLE_INTEGER_SCALE;
}

bool CheckedOutputValueCount(std::size_t pointCount, std::size_t &valueCount) noexcept
{
    if (pointCount == 0U) {
        return false;
    }
    const std::size_t maxSize = (std::numeric_limits<std::size_t>::max)();
    if (pointCount > (maxSize / IQ_VALUES_PER_POINT)) {
        return false;
    }
    const std::size_t count = pointCount * IQ_VALUES_PER_POINT;
    if (count > (maxSize / sizeof(double))) {
        return false;
    }
    valueCount = count;
    return true;
}

std::uint64_t MixNoiseBits(std::uint64_t value) noexcept
{
    value = (value ^ (value >> NOISE_MIX_SHIFT_A)) * NOISE_MIX_MULTIPLIER_A;
    value = (value ^ (value >> NOISE_MIX_SHIFT_B)) * NOISE_MIX_MULTIPLIER_B;
    return value ^ (value >> NOISE_MIX_SHIFT_C);
}

double SignedNoiseUnit(std::uint64_t key) noexcept
{
    // 将可精确表示的高 53 位对应值从 [0,1) 映射到 [-1,1)。
    return (2.0 * static_cast<double>(MixNoiseBits(key) >> DOUBLE_PRECISION_SHIFT) / DOUBLE_INTEGER_SCALE) - 1.0;
}

bool IsValidFrame(const NormalFrameSnapshot &frame) noexcept
{
    if (frame.points.empty() || frame.sourcePort >= NATIVE_PORT_COUNT) {
        return false;
    }
    return std::all_of(frame.points.begin(), frame.points.end(), [&frame](const NormalFramePoint &point) {
        return point.sourcePort == frame.sourcePort;
    });
}

std::array<double, IQ_VALUES_PER_RECEIVER> ReceiverIq(
    std::uint32_t sourcePort, std::uint32_t receiverIndex, double frequencyFactor) noexcept
{
    const double sourceLabel = static_cast<double>(sourcePort + 1U);
    const double receiverLabel = static_cast<double>(receiverIndex + 1U);
    const double sourceAReal = 1.0 + (INCIDENT_RECEIVER_GAIN * sourceLabel) + (INCIDENT_SOURCE_GAIN * sourceLabel);
    const double sourceAImag = FREQUENCY_RESPONSE_GAIN * frequencyFactor;
    const double aReal = 1.0 + (INCIDENT_RECEIVER_GAIN * receiverLabel) + (INCIDENT_SOURCE_GAIN * sourceLabel);
    const double aImag = FREQUENCY_RESPONSE_GAIN * frequencyFactor;
    const double hReal =
        ((receiverIndex == sourcePort) ? REFLECTION_GAIN : (TRANSMISSION_GAIN * receiverLabel * sourceLabel)) +
        (FREQUENCY_RESPONSE_GAIN * frequencyFactor);
    const double hImag = PORT_PHASE_GAIN * (receiverLabel - sourceLabel);
    // 每个 B 均由源端口的 A_source 激励，而非接收端口的入射波。
    const double bReal = (hReal * sourceAReal) - (hImag * sourceAImag);
    const double bImag = (hReal * sourceAImag) + (hImag * sourceAReal);
    return {{aReal, aImag, bReal, bImag}};
}

void WriteGoldenPoint(const NormalFrameSnapshot &frame, std::size_t pointIndex, std::vector<double> &output) noexcept
{
    const double factor = FrequencyFactor(frame.points[pointIndex].frequencyRaw);
    const std::size_t pointBase = pointIndex * IQ_VALUES_PER_POINT;
    for (std::uint32_t receiverIndex = 0U; receiverIndex < NATIVE_PORT_COUNT; ++receiverIndex) {
        const auto receiverIq = ReceiverIq(frame.sourcePort, receiverIndex, factor);
        const std::size_t receiverBase = pointBase + (receiverIndex * IQ_VALUES_PER_RECEIVER);
        std::copy(receiverIq.begin(), receiverIq.end(), output.begin() + receiverBase);
    }
}

void AddPointNoise(const NormalFrameSnapshot &frame, std::size_t pointIndex, std::uint64_t sequenceKey,
    std::vector<double> &output) noexcept
{
    const std::uint64_t frequencyKey = MixNoiseBits(frame.points[pointIndex].frequencyRaw);
    for (std::uint32_t receiver = 0U; receiver < NATIVE_PORT_COUNT; ++receiver) {
        for (std::uint32_t wave = 0U; wave < WAVES_PER_RECEIVER; ++wave) {
            const std::uint64_t channel =
                (frame.sourcePort * NOISE_CHANNELS_PER_SOURCE) + (receiver * WAVES_PER_RECEIVER) + wave;
            const std::uint64_t key =
                MixNoiseBits(sequenceKey ^ frequencyKey ^ MixNoiseBits(channel ^ CHANNEL_NOISE_SALT));
            const std::size_t base = (pointIndex * IQ_VALUES_PER_POINT) + (receiver * IQ_VALUES_PER_RECEIVER) +
                                     (wave * COMPLEX_COMPONENT_COUNT);
            const double scale = std::hypot(output[base], output[base + 1U]) * NOISE_AXIS_RATIO;
            output[base] += scale * SignedNoiseUnit(key);
            output[base + 1U] += scale * SignedNoiseUnit(key ^ IMAGINARY_NOISE_SALT);
        }
    }
}

}  // 匿名命名空间

namespace SignalModel {

std::uint32_t Generate(const NormalFrameSnapshot &frame, std::vector<double> &output) noexcept
{
    if (!IsValidFrame(frame)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::size_t outputValueCount = 0U;
    if (!CheckedOutputValueCount(frame.points.size(), outputValueCount)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }
    try {
        std::vector<double> candidate(outputValueCount, 0.0);
        for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
            WriteGoldenPoint(frame, pointIndex, candidate);
        }
        output.swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        // 报告生成或分配失败，不发布不完整的向量。
        return DRV_SIM_INTERNAL_ERROR;
    }
}

std::uint32_t GenerateNoisy(const NormalFrameSnapshot &frame, std::uint64_t seed, std::uint64_t sweepIndex,
    std::vector<double> &output) noexcept
{
    std::vector<double> candidate;
    const std::uint32_t status = Generate(frame, candidate);
    if (status != DRV_SIM_OK) {
        return status;
    }
    const std::uint64_t sequenceKey = seed ^ MixNoiseBits(sweepIndex ^ SWEEP_NOISE_SALT);
    for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
        AddPointNoise(frame, pointIndex, sequenceKey, candidate);
    }
    output.swap(candidate);
    return DRV_SIM_OK;
}

}  // 命名空间 SignalModel
}  // 命名空间 VnaNative
