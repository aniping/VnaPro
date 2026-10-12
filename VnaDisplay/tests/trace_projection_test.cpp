/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/trace_projection.h"

#include <exception>
#include <iostream>
#include <limits>

#include "display_test_support.h"

namespace {

using namespace VnaDisplay;
using DisplayTest::Near;
using DisplayTest::Require;
using DisplayTest::Trace;

void TestFormatsAndExceptionalValues()
{
    const auto trace = Trace({3.0, 0.0, 1e308}, {4.0, 0.0, 1e308});
    Near(TraceProjection::Format(trace.points[0], DisplayFormat::MAGNITUDE).primary, 5.0);
    Near(TraceProjection::Format(trace.points[0], DisplayFormat::DB).primary, 20.0 * std::log10(5.0));
    Near(TraceProjection::Format(trace.points[0], DisplayFormat::PHASE).primary, 53.13010235415598);
    const auto pair = TraceProjection::Format(trace.points[0], DisplayFormat::REALIMAG);
    Require(pair.hasSecondary, "实虚格式未返回双值");
    Near(pair.primary, 3.0);
    Near(pair.secondary, 4.0);
    Require(TraceProjection::Format(trace.points[1], DisplayFormat::DB).state == DisplayValueState::NEGATIVE_INFINITY,
        "零幅度 dB 没有保留负无穷语义");
    Require(TraceProjection::Format(trace.points[1], DisplayFormat::PHASE).state == DisplayValueState::UNDEFINED,
        "零幅度相位伪装为零度");
    Require(TraceProjection::Format(trace.points[2], DisplayFormat::DB).state == DisplayValueState::FINITE,
        "可表示 dB 被中间幅度溢出破坏");
    auto invalid = trace.points.front();
    invalid.valid = false;
    Require(TraceProjection::Format(invalid, DisplayFormat::REAL).state == DisplayValueState::INVALID,
        "无效采样点被格式处理修复");
}

void TestSmithProjectionAndImpedance()
{
    const auto zero = TraceProjection::Impedance(0.0, 0.0);
    Require(zero.state == ImpedanceState::FINITE, "匹配阻抗无效");
    Near(zero.real, 1.0);
    Near(zero.imag, 0.0);
    Near(TraceProjection::Impedance(-1.0, 0.0).real, 0.0);
    Require(TraceProjection::Impedance(1.0, 0.0).state == ImpedanceState::OPEN, "开路被伪装成有限阻抗");
    const auto reactive = TraceProjection::Impedance(0.0, 1.0);
    Near(reactive.real, 0.0);
    Near(reactive.imag, 1.0);
    const auto huge = TraceProjection::Impedance(1e308, 1e308);
    Require(huge.state == ImpedanceState::FINITE, "极大 Γ 的中间计算溢出");
    Near(huge.real, -1.0);
    const auto smith = TraceProjection::Format(Trace({2.0}, {0.5}).points.front(), DisplayFormat::SMITH);
    Near(smith.primary, 2.0);
    Near(smith.secondary, 0.5);
    Require(smith.hasSecondary && TraceProjection::AllowedFormats(DisplayTest::Measurement()).size() == 7U &&
                TraceProjection::AllowedFormats(DisplayTest::Measurement("Transmission", 2U)).size() == 6U,
        "格式能力与反射支持不一致");
}

void TestComplexInterpolationAndDiscreteMode()
{
    TracePresentation presentation;
    presentation.format = DisplayFormat::REALIMAG;
    presentation.marker.enabled = true;
    presentation.marker.hasFrequency = true;
    presentation.marker.frequencyHz = 1.5e6;
    const auto trace = Trace({0.0, 2.0}, {0.0, 4.0});
    auto result = TraceProjection::Calculate(presentation, trace, {1e6, 2e6});
    Require(result.marker.available && result.marker.valid && result.marker.interpolated, "复数插值标记不可用");
    Near(result.marker.real, 1.0);
    Near(result.marker.imag, 2.0);
    Near(result.marker.frequencyHz, 1.5e6);
    presentation.marker.interpolate = false;
    result = TraceProjection::Calculate(presentation, trace, {1e6, 2e6});
    Require(!result.marker.interpolated && result.marker.sampleIndex == 0U, "离散标记没有确定吸附真实点");
    Near(result.marker.frequencyHz, 1e6);
    auto invalid = trace;
    invalid.points[1].valid = false;
    presentation.marker.interpolate = true;
    result = TraceProjection::Calculate(presentation, invalid, {1e6, 2e6});
    Require(!result.marker.valid && result.marker.value.state == DisplayValueState::INVALID,
        "插值跨过无效点补造了有效数据");
}

void TestSmithPointImpedanceSharesTheImmutableProjection()
{
    auto trace = Trace({0.0, -1.0, 0.0, 1.0, 0.5}, {0.0, 0.0, 1.0, 0.0, 0.0});
    trace.points.back().valid = false;
    TracePresentation presentation;
    presentation.format = DisplayFormat::SMITH;
    presentation.marker.enabled = true;
    presentation.marker.hasSampleIndex = true;
    presentation.marker.sampleIndex = 2U;
    const auto result = TraceProjection::Calculate(presentation, trace, {1, 2, 3, 4, 5});
    Require(result.impedancePoints.size() == trace.points.size(), "Smith 阻抗必须覆盖完整原始点数组");
    Near(result.impedancePoints[0].real, 1.0);
    Near(result.impedancePoints[1].real, 0.0);
    Near(result.impedancePoints[2].real, 0.0);
    Near(result.impedancePoints[2].imag, 1.0);
    Require(result.impedancePoints[3].state == ImpedanceState::OPEN &&
                result.impedancePoints[4].state == ImpedanceState::INVALID,
        "逐点阻抗保留开路和无效点状态");
    Near(result.marker.impedance.real, result.impedancePoints[2].real);
    Near(result.marker.impedance.imag, result.impedancePoints[2].imag);
    Require(presentation.marker.sampleIndex == 2U && !trace.points.back().valid, "投影不得修改正式标记配置或测量数据");
    presentation.format = DisplayFormat::REALIMAG;
    Require(TraceProjection::Calculate(presentation, trace, {1, 2, 3, 4, 5}).impedancePoints.empty(),
        "非 Smith 格式不计算阻抗数组");
}

void TestOriginalIndexSurvivesCwAndTransportDecimation()
{
    TracePresentation presentation;
    presentation.format = DisplayFormat::REAL;
    presentation.marker.enabled = true;
    presentation.marker.hasSampleIndex = true;
    presentation.marker.sampleIndex = 7777U;
    presentation.marker.acquisitionRevision = 7U;
    const std::vector<double> axis(10001U, 10e6);
    std::vector<double> values;
    for (std::size_t index = 0U; index < axis.size(); ++index) {
        values.push_back(static_cast<double>(index));
    }
    const auto projection = TraceProjection::Calculate(presentation, Trace(values), axis, 7U);
    Require(projection.marker.sampleIndex == 7777U && projection.points.size() == 10001U,
        "CW 标记落到同频第一点或传输抽点下标");
    Near(projection.marker.real, 7777.0);
    const auto newPlan = TraceProjection::Calculate(presentation, Trace(values), axis, 8U);
    Require(newPlan.marker.sampleIndex == 5000U, "旧计划离散下标被新计划盲目复用");
}

void TestAutoscaleOverflowAndInvalidData()
{
    DisplayScale scale{-10.0, 10.0, 8.0};
    double radius = 1.0;
    const auto original = scale;
    Require(!TraceProjection::Autoscale(Trace({1e20, 1e20}), DisplayFormat::REAL, scale, radius),
        "不可表示的恒定范围拟合被接受");
    Near(scale.min, original.min);
    Near(scale.max, original.max);
    Require(
        !TraceProjection::Autoscale(Trace({-1e308, 1e308}), DisplayFormat::REAL, scale, radius), "极大范围溢出被接受");
    Require(!TraceProjection::Autoscale(Trace({0.0}), DisplayFormat::DB, scale, radius), "负无穷被当有限点拟合");
    Require(TraceProjection::Autoscale(Trace({1.0, 2.0}, {-5.0, 6.0}), DisplayFormat::REALIMAG, scale, radius),
        "双值共同范围不能拟合");
    Require(scale.min <= -5.0 && scale.max >= 6.0, "Autoscale 未覆盖两个分量");
    Require(TraceProjection::Autoscale(Trace({2.0}), DisplayFormat::SMITH, scale, radius), "Smith 半径不能拟合");
    Near(radius, 2.4);
    auto invalid = Trace({1.0});
    invalid.points.front().valid = false;
    Require(
        !TraceProjection::Calculate(TracePresentation{}, invalid, {2e6, 1e6}).available, "不完整或倒序真实频轴被接受");
}

}  // 匿名命名空间

int main()
{
    try {
        TestFormatsAndExceptionalValues();
        TestSmithProjectionAndImpedance();
        TestComplexInterpolationAndDiscreteMode();
        TestSmithPointImpedanceSharesTheImmutableProjection();
        TestOriginalIndexSurvivesCwAndTransportDecimation();
        TestAutoscaleOverflowAndInvalidData();
        std::cout << "6 个完整频轴显示投影验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
