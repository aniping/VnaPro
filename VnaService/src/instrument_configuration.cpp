/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "instrument_configuration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>

#include "vna_core/frame_layout.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace Detail {
namespace {
OperationResult Result(bool ok, int code = 0, const std::string &message = std::string())
{
    OperationResult result;
    result.ok = ok;
    result.errorCode = code;
    result.message = message;
    return result;
}
}  // 匿名命名空间

bool ValidFrequency(double value, const InstrumentProfile &profile) noexcept
{
    return std::isfinite(value) && value >= profile.minFrequencyHz && value <= profile.maxFrequencyHz;
}

double QuantizedBandwidth(double value) noexcept
{
    const double values[] = {10.0, 100.0, 1000.0, 10000.0, 100000.0};
    for (double valid : values) {
        if (value <= valid) {
            return valid;
        }
    }
    return 0.0;
}

VnaCore::SweepPlan MakePlan(const InstrumentSettings &settings, const InstrumentProfile &profile, unsigned channelId)
{
    (void)profile;
    VnaCore::SweepPlan plan;
    plan.ifBandwidthRaw = static_cast<std::uint32_t>(settings.ifBandwidthHz);
    plan.sweepTimeAuto = settings.sweepTimeAuto;
    plan.sweepTimeSeconds = settings.sweepTimeSeconds;
    plan.powerDbm.assign(settings.points, settings.powerDbm);
    std::set<unsigned> sources;
    for (const Measurement &measurement : settings.measurements) {
        if (measurement.channelId != channelId) {
            continue;
        }
        sources.insert(measurement.sourcePort);
        plan.receiverMask |= 1U << (measurement.receiverPort - 1U);
        if (measurement.kind == MeasurementKind::S_PARAMETER) {
            // 每个 S 参数使用自身激励端口的参考波，必须与响应波来自同一源帧。
            plan.receiverMask |= 1U << (measurement.sourcePort - 1U);
        }
    }
    if (sources.empty()) {
        sources.insert(1U);
    }
    if (plan.receiverMask == 0U) {
        plan.receiverMask = 1U;
    }
    plan.sourcePorts.assign(sources.begin(), sources.end());
    for (unsigned point = 0; point < settings.points; ++point) {
        const double hz = settings.startHz + (settings.stopHz - settings.startHz) * static_cast<double>(point) /
                                                 static_cast<double>(settings.points - 1U);
        // 已确认 VN10 的频率和中频列表使用 Hz；旧接口的原始表由对应设备适配器处理。
        const std::uint64_t raw = static_cast<std::uint64_t>(std::llround(hz));
        plan.frequencyRaw.push_back(raw);
        plan.frequencyHz.push_back(static_cast<double>(raw));
    }
    // 保留旧 VN08 接口必需的 9N 表；VN10 下发不读取此字段。
    plan.tableData.assign(static_cast<std::size_t>(settings.points) * VnaCore::TABLE_VALUES_PER_POINT, 0.0);
    return plan;
}

bool CoversPlan(const VnaCore::SweepPlan &active, const VnaCore::SweepPlan &requested)
{
    // 保留已下发的源和接收机；目录收缩不重启扫描，也不使已有完整 IQ 失效。
    return active.frequencyRaw == requested.frequencyRaw && active.frequencyHz == requested.frequencyHz &&
           active.ifBandwidthRaw == requested.ifBandwidthRaw && active.powerDbm == requested.powerDbm &&
           active.sweepTimeAuto == requested.sweepTimeAuto && active.sweepTimeSeconds == requested.sweepTimeSeconds &&
           active.tableData == requested.tableData &&
           (active.receiverMask & requested.receiverMask) == requested.receiverMask &&
           std::includes(active.sourcePorts.begin(), active.sourcePorts.end(), requested.sourcePorts.begin(),
               requested.sourcePorts.end());
}

OperationResult ApplyCenterSpanPatch(
    const ConfigPatch &patch, InstrumentSettings &settings, const InstrumentProfile &profile)
{
    if (patch.hasStartHz || patch.hasStopHz) {
        return Result(false, Detail::INVALID_PARAMETER_ERROR, "Center/span cannot be mixed with start/stop");
    }
    const double center = patch.hasCenterHz ? patch.centerHz : (settings.startHz + settings.stopHz) / 2.0;
    const double span = patch.hasSpanHz ? patch.spanHz : settings.stopHz - settings.startHz;
    if (!std::isfinite(center) || !std::isfinite(span) || span < 0.0) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Invalid center or span frequency");
    }
    const double start = std::round(center - span / 2.0);
    const double stop = std::round(center + span / 2.0);
    if (!ValidFrequency(start, profile) || !ValidFrequency(stop, profile)) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Center/span exceeds instrument frequency range");
    }
    settings.startHz = start;
    settings.stopHz = stop;
    return Result(true);
}

namespace {
OperationResult FrequencyEndpoint(
    double requested, bool present, const InstrumentProfile &profile, const char *label, double &output)
{
    if (present) {
        if (!ValidFrequency(requested, profile)) {
            return Result(false, DATA_OUT_OF_RANGE_ERROR, std::string(label) + " frequency out of instrument range");
        }
        output = std::round(requested);
    }
    return Result(true);
}

OperationResult CoupleFrequencyEndpoints(
    const ConfigPatch &patch, double span, const InstrumentProfile &profile, InstrumentSettings &settings)
{
    if (patch.hasStartHz && patch.hasStopHz) {
        return settings.startHz > settings.stopHz
                   ? Result(false, DATA_OUT_OF_RANGE_ERROR, "Start frequency exceeds stop frequency")
                   : Result(true);
    }
    if (patch.hasStartHz && settings.startHz > settings.stopHz) {
        settings.stopHz = settings.startHz + span;
    }
    if (patch.hasStopHz && settings.stopHz < settings.startHz) {
        settings.startHz = settings.stopHz - span;
    }
    return ValidFrequency(settings.startHz, profile) && ValidFrequency(settings.stopHz, profile)
               ? Result(true)
               : Result(false, DATA_OUT_OF_RANGE_ERROR, "Coupled frequency endpoint out of instrument range");
}
}  // 匿名命名空间

OperationResult ApplyFrequencyPatch(
    const ConfigPatch &patch, InstrumentSettings &settings, const InstrumentProfile &profile)
{
    if (patch.hasCenterHz || patch.hasSpanHz) {
        return ApplyCenterSpanPatch(patch, settings, profile);
    }
    const double span = settings.stopHz - settings.startHz;
    const auto start = FrequencyEndpoint(patch.startHz, patch.hasStartHz, profile, "Start", settings.startHz);
    if (!start.ok) {
        return start;
    }
    const auto stop = FrequencyEndpoint(patch.stopHz, patch.hasStopHz, profile, "Stop", settings.stopHz);
    if (!stop.ok) {
        return stop;
    }
    return CoupleFrequencyEndpoints(patch, span, profile, settings);
}

namespace {
bool HasConfigurationPatch(const ConfigPatch &patch)
{
    return patch.hasStartHz || patch.hasStopHz || patch.hasCenterHz || patch.hasSpanHz || patch.hasPoints ||
           patch.hasIfBandwidthHz || patch.hasPowerDbm || patch.hasSweepTimeAuto || patch.hasSweepTimeSeconds;
}

OperationResult ApplyPowerPatch(const ConfigPatch &patch, InstrumentSettings &settings)
{
    if (patch.hasPowerDbm) {
        if (!std::isfinite(patch.powerDbm) || patch.powerDbm < -120.0 || patch.powerDbm > 100.0) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Power must be finite and within -120..100 dBm");
        }
        settings.powerDbm = patch.powerDbm;
    }
    return Result(true);
}

OperationResult ApplyScalarPatch(const ConfigPatch &patch, InstrumentSettings &settings)
{
    if (patch.hasPoints && (patch.points < MIN_SWEEP_POINTS || patch.points > MAX_SWEEP_POINTS)) {
        return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Point count outside 2..10001");
    }
    if (patch.hasIfBandwidthHz) {
        if (!std::isfinite(patch.ifBandwidthHz) || patch.ifBandwidthHz <= 0 ||
            QuantizedBandwidth(patch.ifBandwidthHz) == 0) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "IF bandwidth out of instrument range");
        }
        settings.ifBandwidthHz = QuantizedBandwidth(patch.ifBandwidthHz);
    }
    if (patch.hasPoints) {
        settings.points = patch.points;
    }
    return ApplyPowerPatch(patch, settings);
}

OperationResult ApplySweepTimingPatch(const ConfigPatch &patch, InstrumentSettings &settings, bool manualSupported)
{
    if (patch.hasSweepTimeSeconds) {
        if (!std::isfinite(patch.sweepTimeSeconds) || patch.sweepTimeSeconds <= 0.0 ||
            patch.sweepTimeSeconds > MAX_SWEEP_TIME_SECONDS) {
            return Result(false, Detail::DATA_OUT_OF_RANGE_ERROR, "Sweep time must be finite and within (0,100000] s");
        }
        settings.sweepTimeSeconds = patch.sweepTimeSeconds;
        settings.sweepTimeAuto = false;
    }
    if (patch.hasSweepTimeAuto) {
        settings.sweepTimeAuto = patch.sweepTimeAuto;
    }
    if (!settings.sweepTimeAuto && !manualSupported) {
        return Result(false, Detail::SETTINGS_CONFLICT_ERROR, "Software sweep timing is not supported by this driver");
    }
    return Result(true);
}
}  // 匿名命名空间

OperationResult ApplySettingsPatch(const ConfigPatch &patch, InstrumentSettings &settings,
    const InstrumentProfile &profile, bool manualSweepTimeSupported)
{
    if (!HasConfigurationPatch(patch)) {
        return Result(false, Detail::MISSING_PARAMETER_ERROR, "Configuration patch is empty");
    }
    const auto frequency = ApplyFrequencyPatch(patch, settings, profile);
    if (!frequency.ok) {
        return frequency;
    }
    const auto scalar = ApplyScalarPatch(patch, settings);
    if (!scalar.ok) {
        return scalar;
    }
    return ApplySweepTimingPatch(patch, settings, manualSweepTimeSupported);
}

}  // 命名空间 Detail
}  // 命名空间 VnaService
