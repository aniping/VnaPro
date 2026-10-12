/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_TRACE_PROJECTION_H
#define VNAPRO_VNA_DISPLAY_TRACE_PROJECTION_H

#include <cstddef>
#include <string>
#include <vector>

#include "vna_display/display_workspace.h"
#include "vna_measurement/trace_result.h"

namespace VnaDisplay {

enum class DisplayValueState {
    FINITE,
    INVALID,
    NEGATIVE_INFINITY,
    UNDEFINED,
    OVERFLOW
};

struct FormattedPoint final {
    double primary = 0.0;
    double secondary = 0.0;
    bool hasSecondary = false;
    DisplayValueState state = DisplayValueState::INVALID;
};

enum class ImpedanceState {
    FINITE,
    OPEN,
    OVERFLOW,
    INVALID
};

struct NormalizedImpedance final {
    double real = 0.0;
    double imag = 0.0;
    ImpedanceState state = ImpedanceState::INVALID;
};

struct MarkerReadout final {
    unsigned id = 1U;
    bool available = false;
    bool valid = false;
    bool interpolated = false;
    std::size_t sampleIndex = 0U;
    double frequencyHz = 0.0;
    double real = 0.0;
    double imag = 0.0;
    FormattedPoint value;
    NormalizedImpedance impedance;
};

struct ProjectedTrace final {
    bool available = false;
    std::string error;
    std::vector<FormattedPoint> points;
    // 仅 Smith 格式按完整原始点序计算，协议抽点时与投影值使用相同下标。
    std::vector<NormalizedImpedance> impedancePoints;
    unsigned activeMarkerId = 1U;
    std::vector<MarkerReadout> markers;
    MarkerReadout marker;
};

class TraceProjection final {
public:
    TraceProjection() = delete;
    // 格式默认尺度与支持条件由 C++ 唯一维护，前端不再复制控制规则。
    static DisplayScale DefaultScale(DisplayFormat format) noexcept;
    static bool SupportsSmith(const VnaMeasurement::Measurement &measurement) noexcept;
    static std::vector<DisplayFormat> AllowedFormats(const VnaMeasurement::Measurement &measurement);
    static FormattedPoint Format(const VnaMeasurement::ComplexPoint &point, DisplayFormat format) noexcept;
    static NormalizedImpedance Impedance(double real, double imag) noexcept;
    // 传入完整测量数组及真实频轴；抽点仅由传输适配器在此计算之后完成。
    static ProjectedTrace Calculate(const TracePresentation &presentation, const VnaMeasurement::TraceResult &trace,
        const std::vector<double> &frequencyHz, std::uint64_t acquisitionRevision = 0U);
    // 只适配一次，失败时不修改调用者已有的矩形尺度或 Smith 半径。
    static bool Autoscale(
        const VnaMeasurement::TraceResult &trace, DisplayFormat format, DisplayScale &scale, double &smithRadius);
};

}  // 命名空间 VnaDisplay

#endif
