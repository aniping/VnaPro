/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_service/instrument_profile.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace VnaService {
namespace {
// 同时满足 JavaScript 精确整数和驱动有符号取整的可表示范围。
constexpr double MAX_EXACT_FREQUENCY_HZ = 9007199254740991.0;

bool ValidIntegralFrequency(double value) noexcept
{
    return std::isfinite(value) && value >= 1.0 && value <= MAX_EXACT_FREQUENCY_HZ && std::floor(value) == value;
}
}  // 匿名命名空间

void ValidateInstrumentProfile(const InstrumentProfile &profile)
{
    if (profile.portCount < 1U || profile.portCount > 4U) {
        throw std::invalid_argument("instrument.portCount must be within 1..4");
    }
    if (!ValidIntegralFrequency(profile.minFrequencyHz) || !ValidIntegralFrequency(profile.maxFrequencyHz) ||
        profile.minFrequencyHz > profile.maxFrequencyHz) {
        throw std::invalid_argument("instrument.frequency must be ordered integral Hz within 1..9007199254740991");
    }
    if (!ValidIntegralFrequency(profile.defaultStartHz) || !ValidIntegralFrequency(profile.defaultStopHz) ||
        profile.defaultStartHz < profile.minFrequencyHz || profile.defaultStopHz > profile.maxFrequencyHz ||
        profile.defaultStartHz > profile.defaultStopHz) {
        throw std::invalid_argument("instrument.sweep defaults must lie within instrument.frequency");
    }
    if (profile.defaultPoints < MIN_SWEEP_POINTS || profile.defaultPoints > MAX_SWEEP_POINTS) {
        throw std::invalid_argument("instrument.sweep.defaultPoints must be within 2..10001");
    }
    const double bandwidths[] = {10.0, 100.0, 1000.0, 10000.0, 100000.0};
    if (std::find(std::begin(bandwidths), std::end(bandwidths), profile.defaultIfBandwidthHz) == std::end(bandwidths)) {
        throw std::invalid_argument("instrument.sweep.defaultIfBandwidthHz is unsupported");
    }
}

}  // 命名空间 VnaService
