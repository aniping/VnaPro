/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

#include "display_test_support.h"
#include "scale_calculator.h"
#include "vna_display/trace_projection.h"

namespace {

using namespace VnaDisplay;
using DisplayTest::Near;
using DisplayTest::Require;
using DisplayTest::Trace;

constexpr double PI = 3.14159265358979323846;

void Close(double actual, double expected, double tolerance)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, "频率导数与独立解析结果不一致");
}

VnaMeasurement::TraceResult PhaseTrace(const std::vector<double> &phaseRadians)
{
    std::vector<double> real;
    std::vector<double> imaginary;
    for (const double phase : phaseRadians) {
        real.push_back(std::cos(phase));
        imaginary.push_back(std::sin(phase));
    }
    return Trace(real, imaginary);
}

TracePresentation Presentation(DisplayFormat format, double markerFrequency)
{
    TracePresentation presentation;
    presentation.format = format;
    presentation.markers.Active().enabled = true;
    presentation.markers.Active().hasFrequency = true;
    presentation.markers.Active().frequencyHz = markerFrequency;
    return presentation;
}

void TestAllNamesAndCapabilities()
{
    const auto formats = TraceProjection::AllowedFormats(DisplayTest::Measurement());
    Require(formats.size() == 13U, "常规 Format 菜单未提供十三项");
    for (const auto format : formats) {
        DisplayFormat parsed = DisplayFormat::DB;
        Require(DisplayFormatName(format) != nullptr && ParseDisplayFormat(DisplayFormatName(format), parsed) &&
                    parsed == format,
            "显示格式名称不能往返解析");
    }
    DisplayFormat legacy = DisplayFormat::DB;
    Require(ParseDisplayFormat("realimag", legacy) && legacy == DisplayFormat::REALIMAG &&
                std::find(formats.begin(), formats.end(), legacy) == formats.end(),
        "旧实虚 API 不应混入 ZNB 常规菜单");
    Require(!ParseDisplayFormat("MLOG", legacy), "显示合同混入 SCPI 格式语法");
    const auto transmission = DisplayTest::Measurement("S21", 2U);
    Require(TraceProjection::SupportsSmith(transmission) && TraceProjection::AllowedFormats(transmission).size() == 13U,
        "传输参数的复数显示格式被禁止");
    VnaMeasurement::Measurement wave;
    Require(TraceProjection::SupportsSmith(wave), "原始复数波量不能显示圆图");
}

void TestSwrFormulaAndExceptionalValues()
{
    Near(TraceProjection::Format(Trace({0.0}).points.front(), DisplayFormat::SWR).primary, 1.0);
    Near(TraceProjection::Format(Trace({0.5}).points.front(), DisplayFormat::SWR).primary, 3.0);
    Near(TraceProjection::Format(Trace({2.0}).points.front(), DisplayFormat::SWR).primary, -3.0);
    const auto singular = TraceProjection::Format(Trace({1.0}).points.front(), DisplayFormat::SWR);
    Require(singular.state == DisplayValueState::OVERFLOW, "完全反射的 SWR 被伪装成有限数");
    const auto huge = TraceProjection::Format(Trace({1e308}, {1e308}).points.front(), DisplayFormat::SWR);
    Require(huge.state == DisplayValueState::FINITE, "极大幅值破坏可表示的 SWR");
    Near(huge.primary, -1.0);
}

void TestLogMagnitudeAndScale()
{
    Near(TraceProjection::Format(Trace({1000.0}).points.front(), DisplayFormat::LOG_MAGNITUDE).primary, 1000.0);
    Near(TraceProjection::Format(Trace({0.0}).points.front(), DisplayFormat::LOG_MAGNITUDE).primary, 0.0);
    DisplayScale scale = TraceProjection::DefaultScale(DisplayFormat::LOG_MAGNITUDE);
    ScaleChange change;
    change.hasReference = true;
    change.reference = 1.0;
    change.hasPerDivision = true;
    change.perDivision = 0.5;
    std::string error;
    Require(Detail::ScaleCalculator::Apply(scale, change, error, DisplayFormat::LOG_MAGNITUDE),
        "对数尺度不能按正幅值参考和 decade 联动");
    Near(scale.min, 1e-4);
    Near(scale.max, 10.0);
    const auto saved = scale;
    change.reference = 0.0;
    Require(!Detail::ScaleCalculator::Apply(scale, change, error, DisplayFormat::LOG_MAGNITUDE),
        "零幅值被接受为对数尺度参考");
    Near(scale.min, saved.min);
    Near(scale.max, saved.max);
    double radius = 1.0;
    Require(TraceProjection::Autoscale(Trace({0.0, 0.001, 1000.0}), DisplayFormat::LOG_MAGNITUDE, scale, radius),
        "对数 Autoscale 没有使用正幅值");
    Require(scale.min > 0.0 && scale.min <= 0.001 && scale.max >= 1000.0, "对数尺度未覆盖有效数据");
    Require(!TraceProjection::Autoscale(Trace({0.0}), DisplayFormat::LOG_MAGNITUDE, scale, radius),
        "无正幅值的对数图被伪造为有限范围");
}

void TestCircularCoordinatesAndPhysicalInterpretation()
{
    const auto point = Trace({0.25}, {0.4}).points.front();
    for (const auto format : {DisplayFormat::POLAR, DisplayFormat::SMITH, DisplayFormat::INV_SMITH}) {
        const auto value = TraceProjection::Format(point, format);
        Require(value.hasSecondary && TraceProjection::IsCircular(format), "复平面丢失了第二分量");
        Near(value.primary, 0.25);
        Near(value.secondary, 0.4);
    }
    Near(TraceProjection::Admittance(0.0, 0.0).real, 1.0);
    Near(TraceProjection::Admittance(1.0, 0.0).real, 0.0);
    Require(TraceProjection::Admittance(-1.0, 0.0).state == AdmittanceState::SHORT, "短路的无限导纳被标为有限值或开路");
    Near(TraceProjection::Admittance(0.0, 1.0).imag, -1.0);
    auto presentation = Presentation(DisplayFormat::INV_SMITH, 2.0);
    const auto trace = Trace({0.0, 1.0, -1.0});
    const auto reflection = DisplayTest::Measurement();
    const auto reflected = TraceProjection::Calculate(presentation, trace, {1, 2, 3}, reflection);
    Require(reflected.admittancePoints.size() == 3U && reflected.impedancePoints.size() == 3U,
        "反射圆图未保留完整原始导纳和阻抗数组");
    Near(reflected.marker.admittance.real, 0.0);
    const auto transmission = DisplayTest::Measurement("S21", 2U);
    const auto transmitted = TraceProjection::Calculate(presentation, trace, {1, 2, 3}, transmission);
    Require(transmitted.available && transmitted.admittancePoints.empty() && transmitted.impedancePoints.empty() &&
                transmitted.marker.impedance.state == ImpedanceState::INVALID &&
                transmitted.marker.admittance.state == AdmittanceState::INVALID,
        "一般复数被冒充成反射负载阻抗");
    Require(TraceProjection::Calculate(presentation, trace, {1, 2, 3}).impedancePoints.empty(),
        "缺少测量元数据仍提供物理解释");
}

void TestUnwrappedPhaseAndMarker()
{
    const auto trace = PhaseTrace({175.0 * PI / 180.0, -175.0 * PI / 180.0, -160.0 * PI / 180.0});
    auto presentation = Presentation(DisplayFormat::UNWRAPPED_PHASE, 1.5);
    const auto result = TraceProjection::Calculate(presentation, trace, {1, 2, 4});
    Near(result.points[0].primary, 175.0);
    Near(result.points[1].primary, 185.0);
    Near(result.points[2].primary, 200.0);
    Near(result.marker.value.primary, 180.0);
    Require(result.marker.interpolated, "展开相位标记没有沿同份序列投影插值");
    presentation.markers.Active().interpolate = false;
    presentation.markers.Active().frequencyHz = 2.0;
    const auto discrete = TraceProjection::Calculate(presentation, trace, {1, 2, 4});
    Near(discrete.marker.value.primary, discrete.points[1].primary);
    Require(
        TraceProjection::Format(trace.points[1], DisplayFormat::UNWRAPPED_PHASE).state == DisplayValueState::UNDEFINED,
        "孤立复数伪造了展开相位");
}

void TestPolarDefaultMarkerUsesMagnitudeAndPhase()
{
    const auto trace = Trace({0.0, 0.0, 0.0}, {1.0, 2.0, 0.0});
    auto presentation = Presentation(DisplayFormat::POLAR, 1.5);
    const auto result = TraceProjection::Calculate(presentation, trace, {1, 2, 3});
    Near(result.points[0].primary, 0.0);
    Near(result.points[0].secondary, 1.0);
    Require(result.markerPoints.size() == 3U && result.marker.value.hasSecondary,
        "Polar 默认标记的幅相表示未与绘图坐标分开");
    Near(result.markerPoints[0].primary, 1.0);
    Near(result.markerPoints[0].secondary, 90.0);
    Near(result.marker.real, 0.0);
    Near(result.marker.imag, 1.5);
    Near(result.marker.value.primary, 1.5);
    Near(result.marker.value.secondary, 90.0);
    Require(result.markerPoints[2].state == DisplayValueState::FINITE && result.markerPoints[2].primary == 0.0 &&
                result.markerPoints[2].secondaryState == DisplayValueState::UNDEFINED,
        "零幅值标记丢失有效幅值或伪造了可定义相位");
    presentation.markers.Active().interpolate = false;
    presentation.markers.Active().frequencyHz = 2.0;
    const auto discrete = TraceProjection::Calculate(presentation, trace, {1, 2, 3});
    Near(discrete.marker.value.primary, discrete.markerPoints[1].primary);
    Near(discrete.marker.value.secondary, discrete.markerPoints[1].secondary);
    presentation.markers.Active().frequencyHz = 3.0;
    const auto zero = TraceProjection::Calculate(presentation, trace, {1, 2, 3});
    Require(zero.marker.valid && zero.marker.value.state == DisplayValueState::FINITE &&
                zero.marker.value.primary == 0.0 && zero.marker.value.secondaryState == DisplayValueState::UNDEFINED,
        "零幅值正式标记与预览没有采用一致的独立相位状态");
}

void TestDelayAndSecondDerivativeOnNonuniformAxis()
{
    const std::vector<double> axis{1e6, 1.7e6, 2.2e6, 3.3e6, 5e6};
    std::vector<double> phase;
    constexpr double BASE_DELAY = 7e-9;
    constexpr double DERIVATIVE = 1e-18;
    for (const double frequency : axis) {
        phase.push_back(-2.0 * PI * BASE_DELAY * frequency - 2.0 * PI * PI * DERIVATIVE * frequency * frequency);
    }
    const auto trace = PhaseTrace(phase);
    auto presentation = Presentation(DisplayFormat::DELAY, 2.5e6);
    const auto delay = TraceProjection::Calculate(presentation, trace, axis);
    for (std::size_t index = 0U; index < axis.size(); ++index) {
        Close(delay.points[index].primary, BASE_DELAY + 2.0 * PI * DERIVATIVE * axis[index], 1e-18);
    }
    Close(delay.marker.value.primary, BASE_DELAY + 2.0 * PI * DERIVATIVE * 2.5e6, 1e-18);
    presentation.format = DisplayFormat::DELAY_DERIVATION;
    const auto derivative = TraceProjection::Calculate(presentation, trace, axis);
    for (const auto &value : derivative.points) {
        Close(value.primary, DERIVATIVE, 1e-23);
    }
    Close(derivative.marker.value.primary, DERIVATIVE, 1e-23);
    DisplayScale scale = TraceProjection::DefaultScale(DisplayFormat::DELAY);
    double radius = 1.0;
    Require(TraceProjection::Autoscale(trace, DisplayFormat::DELAY, scale, radius, axis),
        "延迟 Autoscale 没有消费完整频轴结果");
    Require(scale.min <= delay.points.front().primary && scale.max >= delay.points.back().primary,
        "延迟尺度未覆盖中心和边界投影");
    Require(!TraceProjection::Autoscale(trace, DisplayFormat::DELAY, scale, radius), "缺少真实频轴却拟合假延迟");
}

void TestInvalidSegmentsAndCw()
{
    auto trace = PhaseTrace({0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6});
    trace.points[2].valid = false;
    trace.points[6] = {0.0, 0.0, true, VnaMeasurement::PointError::NONE};
    const auto result =
        TraceProjection::Calculate(Presentation(DisplayFormat::DELAY, 3.0), trace, {1, 2, 3, 4, 5, 6, 7});
    Require(
        result.points[2].state == DisplayValueState::INVALID && result.points[6].state == DisplayValueState::UNDEFINED,
        "频率导数跨过无效点或零幅度接线");
    Close(result.points[0].primary, -0.1 / (2.0 * PI), 1e-12);
    Close(result.points[3].primary, -0.1 / (2.0 * PI), 1e-12);
    const auto cw =
        TraceProjection::Calculate(Presentation(DisplayFormat::DELAY, 10.0), PhaseTrace({0, 0.1}), {10, 10});
    Require(cw.available && cw.points[0].state == DisplayValueState::UNDEFINED &&
                cw.points[1].state == DisplayValueState::UNDEFINED &&
                cw.marker.value.state == DisplayValueState::UNDEFINED,
        "CW 没有频率间隔仍返回群延迟");
    const auto sparse =
        TraceProjection::Calculate(Presentation(DisplayFormat::DELAY_DERIVATION, 1.0), PhaseTrace({0, 0.1}), {1, 2});
    Require(sparse.points.front().state == DisplayValueState::UNDEFINED, "两个点伪造二阶导数");
}

void TestFullSequenceBeforeTransportSelection()
{
    std::vector<double> axis;
    std::vector<double> phase;
    for (std::size_t index = 0U; index < 10001U; ++index) {
        axis.push_back(static_cast<double>(index + 1U));
        phase.push_back(static_cast<double>(index) * 0.1 * PI / 180.0);
    }
    const auto presentation = Presentation(DisplayFormat::UNWRAPPED_PHASE, 7778.0);
    const auto result = TraceProjection::Calculate(presentation, PhaseTrace(phase), axis);
    Require(result.points.size() == 10001U && result.marker.sampleIndex == 7777U, "序列格式在 HTTP 抽点前丢失原始频轴");
    Close(result.points.back().primary, 1000.0, 1e-9);
    Close(result.marker.value.primary, 777.7, 1e-9);
}

}  // 匿名命名空间

int main()
{
    try {
        TestAllNamesAndCapabilities();
        TestSwrFormulaAndExceptionalValues();
        TestLogMagnitudeAndScale();
        TestCircularCoordinatesAndPhysicalInterpretation();
        TestPolarDefaultMarkerUsesMagnitudeAndPhase();
        TestUnwrappedPhaseAndMarker();
        TestDelayAndSecondDerivativeOnNonuniformAxis();
        TestInvalidSegmentsAndCw();
        TestFullSequenceBeforeTransportSelection();
        std::cout << "9 个常规格式与完整频轴数学验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
