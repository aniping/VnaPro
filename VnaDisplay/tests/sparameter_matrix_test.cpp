/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <iostream>
#include <limits>

#include "display_test_support.h"

namespace {
using DisplayTest::Require;

std::vector<VnaMeasurement::Measurement> Matrix(unsigned ports)
{
    std::vector<VnaMeasurement::Measurement> catalog;
    for (unsigned receiver = 1U; receiver <= ports; ++receiver) {
        for (unsigned source = 1U; source <= ports; ++source) {
            auto trace = DisplayTest::Measurement("Trc" + std::to_string(catalog.size() + 1U), receiver);
            trace.sourcePort = source;
            catalog.push_back(std::move(trace));
        }
    }
    return catalog;
}

void ReplacesOldTraceSettings(unsigned ports)
{
    const auto old = std::vector<VnaMeasurement::Measurement>{DisplayTest::Measurement()};
    VnaDisplay::DisplayWorkspace display;
    Require(display.Reconcile(old).ok, "建立旧迹线");
    auto scale = DisplayTest::Change(VnaDisplay::DisplayChangeKind::SCALE);
    scale.scale.hasMin = scale.scale.hasMax = true;
    scale.scale.min = -10.0;
    scale.scale.max = 10.0;
    Require(display.Apply(scale, old, {}).ok, "保存旧刻度");
    auto marker = DisplayTest::Change(VnaDisplay::DisplayChangeKind::MARKER);
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 1U;
    Require(display.Apply(marker, old, DisplayTest::Frame()).ok, "保存旧标记");
    auto initial = display.Snapshot();
    initial.traces.front().smithRadius = 3.0;
    initial.annotations.fontScale = 110.0;
    initial.infoTable.visible = true;
    display = VnaDisplay::DisplayWorkspace(initial);
    const auto catalog = Matrix(ports);
    const auto result = display.ReplaceSParameterMatrix(catalog, ports);
    const auto &state = display.State();
    Require(result.ok && result.changed && state.revision == initial.revision + 1U, "整组显示只提交一次版本");
    Require(state.diagrams.size() == ports * ports && state.traces.size() == catalog.size(), "完整矩阵替换旧显示目录");
    Require(state.selectedMeasurement == "Trc1" && state.selectedDiagram == 1U, "矩阵选中 S11");
    Require(state.layout == VnaDisplay::LayoutMode::GRID, "矩阵进入全部图窗布局");
    const auto grid = VnaDisplay::DisplayWorkspace::GridGeometry(state);
    Require(grid.columns == ports && grid.rows == ports, "布局由后端保存为端口数平方");
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        const auto &diagram = state.diagrams[index];
        const auto &trace = state.traces[index];
        Require(
            diagram.number == index + 1U && diagram.measurementNames == std::vector<std::string>{catalog[index].name},
            "每个参数独占连续编号图窗");
        Require(diagram.traceSlots == std::vector<unsigned>{1U} && diagram.primaryMeasurement == catalog[index].name,
            "局部活动迹线和显示槽独立");
        const auto expectedFormat = catalog[index].receiverPort == catalog[index].sourcePort
                                        ? VnaDisplay::DisplayFormat::SMITH
                                        : VnaDisplay::DisplayFormat::DB;
        Require(trace.format == expectedFormat, "整组矩阵对角反射使用 Smith，非对角传输使用 dB Mag");
        Require(trace.cartesianFormat == VnaDisplay::DisplayFormat::DB && trace.scale.min == -90.0 &&
                    trace.scale.max == 10.0 && trace.scale.referencePosition == 9.0 && trace.smithRadius == 1.0,
            "同名旧迹线的矩形刻度与圆图半径不继承");
        Require(!trace.markers.Find(1U)->enabled, "同名旧迹线的 Marker 不继承");
    }
    Require(state.annotations.fontScale == 110.0 && state.infoTable.visible, "保留全局显示偏好");
    Require(display.ReplaceSParameterMatrix(catalog, ports).ok && display.State().diagrams.size() == catalog.size(),
        "重复整组创建不累加窗口");
    auto maximized = DisplayTest::Change(VnaDisplay::DisplayChangeKind::LAYOUT);
    maximized.layout = VnaDisplay::LayoutMode::MAXIMIZED;
    Require(display.Apply(maximized, catalog, {}).ok, "最大化一个矩阵图窗");
    Require(VnaDisplay::DisplayWorkspace::GridGeometry(display.State()).columns == ports, "最大化保留原矩阵布局");
    maximized.layout = VnaDisplay::LayoutMode::GRID;
    Require(display.Apply(maximized, catalog, {}).ok &&
                VnaDisplay::DisplayWorkspace::GridGeometry(display.State()).rows == ports,
        "返回全部图窗恢复矩阵");
}

void InvalidMatrixIsAtomic()
{
    auto catalog = Matrix(2U);
    VnaDisplay::DisplayWorkspace display;
    Require(display.Reconcile(catalog).ok, "建立已有显示");
    const auto before = display.Snapshot();
    std::swap(catalog[1U], catalog[2U]);
    Require(!display.ReplaceSParameterMatrix(catalog, 2U).ok, "拒绝错误参数排列");
    catalog = Matrix(2U);
    catalog.back().name = catalog.front().name;
    Require(!display.ReplaceSParameterMatrix(catalog, 2U).ok, "拒绝重复名称");
    catalog = Matrix(2U);
    Require(!display.ReplaceSParameterMatrix(catalog, 0U).ok && !display.ReplaceSParameterMatrix(catalog, 5U).ok,
        "拒绝非法端口数量");
    catalog.pop_back();
    Require(!display.ReplaceSParameterMatrix(catalog, 2U).ok, "拒绝缺少参数");
    Require(display.State().revision == before.revision && display.State().displayedNames == before.displayedNames,
        "失败保持旧版本和归属");
    auto exhausted = before;
    exhausted.revision = std::numeric_limits<std::uint64_t>::max();
    display = VnaDisplay::DisplayWorkspace(exhausted);
    Require(!display.ReplaceSParameterMatrix(Matrix(4U), 4U).ok && display.State().revision == exhausted.revision,
        "版本耗尽不替换显示");
}
}  // 匿名命名空间

int main()
{
    try {
        ReplacesOldTraceSettings(2U);
        ReplacesOldTraceSettings(4U);
        InvalidMatrixIsAtomic();
        std::cout << "PASS 二端口/四端口整组显示替换、固定矩阵与失败原子性\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
