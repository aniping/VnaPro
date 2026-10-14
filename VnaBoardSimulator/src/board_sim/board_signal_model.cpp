/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "board_signal_model.h"

#include <cmath>
#include <complex>

namespace VnaBoard {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double PHASE_PERIOD_RAW = 1000000000.0;
constexpr double INCIDENT_PHASE_FACTOR = 0.12;
constexpr double RECEIVER_PHASE_STEP = 0.1;
constexpr double LEAKAGE_MAGNITUDE = 0.08;
constexpr double REFLECTION_MAGNITUDE = 0.21;
constexpr double REFLECTION_RIPPLE = 0.02;
constexpr double TRANSMISSION_MAGNITUDE = 0.045;
constexpr double TRANSMISSION_RIPPLE = 0.006;
constexpr double TRANSMISSION_PHASE_FACTOR = 0.35;
constexpr double TRANSMISSION_RECEIVER_PHASE_STEP = 0.1;
constexpr double TRANSMISSION_SOURCE_PHASE_STEP = 0.2;
constexpr double NOISE_MANTISSA_RANGE = 9007199254740992.0;

std::uint64_t Mix(std::uint64_t value)
{
    // 无符号环绕是散列定义的一部分；不用于帧序号的计数。
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}  // 匿名命名空间

BoardSignalModel::BoardSignalModel(SignalSettings settings, std::uint64_t sweepNumber)
    : settings_(settings), sweepNumber_(sweepNumber)
{
}

double BoardSignalModel::Noise(
    std::size_t point, std::uint32_t source, std::uint32_t receiver, std::uint32_t component) const
{
    if (settings_.mode == SignalMode::GOLDEN || settings_.noiseAmplitude == 0.0) {
        return 0.0;
    }
    const auto key = settings_.seed ^ Mix(sweepNumber_) ^ Mix(static_cast<std::uint64_t>(point)) ^
                     Mix(17U * source + 5U * receiver + component + 1U);
    const double unit = static_cast<double>(Mix(key) >> 11U) / NOISE_MANTISSA_RANGE;
    return (2.0 * unit - 1.0) * settings_.noiseAmplitude;
}

void BoardSignalModel::GenerateSourcePoint(
    const SweepPlan &plan, std::size_t point, std::uint32_t source, double *output) const
{
    // 仅模拟信号以原始频率数值建立平滑相位；不声明真实硬件的频率或功率编码。
    const double phase = 2.0 * PI * static_cast<double>(plan.frequencies[point]) / PHASE_PERIOD_RAW;
    for (std::uint32_t receiver = 0U; receiver < BOARD_PORT_COUNT; ++receiver) {
        const double incidentMagnitude = receiver == source ? 1.0 : LEAKAGE_MAGNITUDE;
        const auto incident =
            std::polar(incidentMagnitude, phase * INCIDENT_PHASE_FACTOR + RECEIVER_PHASE_STEP * receiver);
        const double magnitude =
            receiver == source
                ? REFLECTION_MAGNITUDE + REFLECTION_RIPPLE * std::sin(phase)
                : TRANSMISSION_MAGNITUDE + TRANSMISSION_RIPPLE * std::cos(phase + static_cast<double>(receiver) -
                                                                          static_cast<double>(source));
        const auto transfer =
            std::polar(magnitude, -phase * (TRANSMISSION_PHASE_FACTOR + TRANSMISSION_RECEIVER_PHASE_STEP * receiver) -
                                      TRANSMISSION_SOURCE_PHASE_STEP * source);
        const auto reflected = std::polar(1.0, phase * INCIDENT_PHASE_FACTOR + RECEIVER_PHASE_STEP * source) * transfer;
        const std::size_t offset = IQ_VALUES_PER_RECEIVER * receiver;
        output[offset] = incident.real() * (1.0 + Noise(point, source, receiver, 0U));
        output[offset + 1U] = incident.imag() + incidentMagnitude * Noise(point, source, receiver, 1U);
        output[offset + 2U] = reflected.real() * (1.0 + Noise(point, source, receiver, 2U));
        output[offset + 3U] = reflected.imag() + magnitude * Noise(point, source, receiver, 3U);
    }
}

}  // 命名空间 VnaBoard
