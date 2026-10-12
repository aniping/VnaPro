/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_SCALE_CALCULATOR_H
#define VNAPRO_VNA_DISPLAY_SCALE_CALCULATOR_H

#include <string>

#include "vna_display/display_workspace.h"
#include "vna_measurement/trace_result.h"

namespace VnaDisplay {
namespace Detail {

class ScaleCalculator final {
public:
    ScaleCalculator() = delete;
    static bool Apply(
        DisplayScale &scale, const ScaleChange &change, std::string &error, DisplayFormat format = DisplayFormat::DB);
    static bool Autoscale(const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale,
        double &smithRadius, const std::vector<double> &frequencyHz = {});
};

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay

#endif
