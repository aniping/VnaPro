/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/display_workspace.h"

namespace VnaDisplay {

const char *DisplayFormatName(DisplayFormat format) noexcept
{
    switch (format) {
        case DisplayFormat::DB:
            return "db";
        case DisplayFormat::MAGNITUDE:
            return "magnitude";
        case DisplayFormat::PHASE:
            return "phase";
        case DisplayFormat::SWR:
            return "swr";
        case DisplayFormat::UNWRAPPED_PHASE:
            return "unwrappedPhase";
        case DisplayFormat::LOG_MAGNITUDE:
            return "logMagnitude";
        case DisplayFormat::REAL:
            return "real";
        case DisplayFormat::IMAG:
            return "imag";
        case DisplayFormat::REALIMAG:
            return "realimag";
        case DisplayFormat::DELAY:
            return "delay";
        case DisplayFormat::DELAY_DERIVATION:
            return "delayDerivation";
        case DisplayFormat::POLAR:
            return "polar";
        case DisplayFormat::SMITH:
            return "smith";
        case DisplayFormat::INV_SMITH:
            return "invSmith";
        default:
            return nullptr;
    }
}

bool ParseDisplayFormat(const std::string &value, DisplayFormat &format) noexcept
{
    for (const auto candidate : {DisplayFormat::DB, DisplayFormat::MAGNITUDE, DisplayFormat::PHASE, DisplayFormat::SWR,
             DisplayFormat::UNWRAPPED_PHASE, DisplayFormat::LOG_MAGNITUDE, DisplayFormat::REAL, DisplayFormat::IMAG,
             DisplayFormat::REALIMAG, DisplayFormat::DELAY, DisplayFormat::DELAY_DERIVATION, DisplayFormat::POLAR,
             DisplayFormat::SMITH, DisplayFormat::INV_SMITH}) {
        if (value == DisplayFormatName(candidate)) {
            format = candidate;
            return true;
        }
    }
    return false;
}

}  // 命名空间 VnaDisplay
