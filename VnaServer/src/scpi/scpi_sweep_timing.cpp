/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cmath>
#include <string>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"
#include "vna_service/instrument_profile.h"

namespace VnaScpi {
namespace {
bool MinimumQuery(const ParsedCommand &command)
{
    if (command.arguments.size() != 1U) {
        return false;
    }
    const auto argument = Upper(command.arguments.front());
    return argument == "MIN" || argument == "MINIMUM";
}

bool TimeSeconds(const std::string &text, double &value)
{
    const auto input = Upper(Trim(text));

    struct Unit final {
        const char *name;
        double scale;
    };

    for (const auto &unit : {Unit{"MS", 1e-3}, Unit{"US", 1e-6}, Unit{"NS", 1e-9}, Unit{"S", 1.0}}) {
        const std::string suffix = unit.name;
        if (input.size() >= suffix.size() && input.compare(input.size() - suffix.size(), suffix.size(), suffix) == 0) {
            if (!Numeric(input.substr(0U, input.size() - suffix.size()), value)) {
                return false;
            }
            value *= unit.scale;
            return std::isfinite(value);
        }
    }
    return Numeric(input, value);
}
}  // 匿名命名空间

void ScpiEngine::DispatchSweepTimeAuto(const ParsedCommand &command)
{
    if (command.query) {
        if (CheckArguments(command, 0U)) {
            AppendResponse(service_.Settings(command.channelId).sweepTimeAuto ? "1" : "0");
        }
        return;
    }
    if (!CheckArguments(command, 1U)) {
        return;
    }
    const auto argument = Upper(command.arguments.front());
    if (argument != "1" && argument != "0" && argument != "ON" && argument != "OFF") {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Sweep time Auto expects ON, OFF, 1 or 0");
        return;
    }
    VnaService::ConfigPatch patch;
    patch.hasSweepTimeAuto = true;
    patch.sweepTimeAuto = argument == "1" || argument == "ON";
    SetOperation(service_.ApplyConfiguration(patch, 0U, "SCPI", command.channelId));
}

void ScpiEngine::DispatchSweepTiming(const ParsedCommand &command)
{
    if (command.path == "SENS1:SWE:TIME:AUTO") {
        DispatchSweepTimeAuto(command);
        return;
    }
    const auto &settings = service_.Settings(command.channelId);
    if (command.query) {
        if (MinimumQuery(command)) {
            RecordCommandError(command, SETTINGS_CONFLICT, "Minimum sweep time is unavailable; use TIME:AUTO ON");
            return;
        }
        const auto snapshot = service_.Snapshot(command.channelId);
        // Auto 尚无当前配置的完整帧时返回 0；不冒充 ZNB 的硬件最短扫时估算。
        const auto seconds = settings.sweepTimeAuto ? snapshot.core.lastSweepTimeSeconds : settings.sweepTimeSeconds;
        QueryNumeric(command, seconds, 0.0, VnaService::MAX_SWEEP_TIME_SECONDS);
        return;
    }
    if (!CheckArguments(command, 1U)) {
        return;
    }
    double seconds = 0.0;
    if (!TimeSeconds(command.arguments.front(), seconds)) {
        RecordCommandError(
            command, DATA_TYPE_ERROR, "Sweep time requires finite seconds with an optional S/MS/US/NS unit");
        return;
    }
    VnaService::ConfigPatch patch;
    patch.hasSweepTimeSeconds = true;
    patch.sweepTimeSeconds = seconds;
    SetOperation(service_.ApplyConfiguration(patch, 0U, "SCPI", command.channelId));
}
}  // 命名空间 VnaScpi
