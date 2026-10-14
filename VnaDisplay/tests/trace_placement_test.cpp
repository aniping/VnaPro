/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <exception>
#include <iostream>

#include "display_test_support.h"
#include "vna_display/diagram_collection.h"

namespace {
using namespace VnaDisplay;
using DisplayTest::Change;
using DisplayTest::Measurement;
using DisplayTest::Near;
using DisplayTest::Require;

DiagramRegion Region(const DisplayState &state, unsigned number)
{
    const auto regions = state.diagramLayout.Regions();
    const auto found = std::find_if(regions.begin(), regions.end(), [number](const DiagramRegion &region) {
        return region.number == number;
    });
    Require(found != regions.end(), "自定义图窗区域缺失");
    return *found;
}

void TestMoveKeepsTraceSettingsAndCollapsesEmptySource()
{
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    DisplayWorkspace workspace;
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto marker = Change(DisplayChangeKind::MARKER, "Trc1");
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    Require(workspace.Apply(marker, catalog, {}).ok, "启用标记失败");
    const auto before = workspace.Snapshot();
    auto move = Change(DisplayChangeKind::TRACE_MOVE, "Trc1");
    move.diagram = 1U;
    move.dropPosition = TraceDropPosition::RIGHT;
    Require(
        workspace.Apply(move, catalog, {}).ok && workspace.State().diagrams.size() == 2U, "右侧分割应原子移动既有迹线");
    Near(Region(workspace.State(), 2U).left, 0.5);
    Near(Region(workspace.State(), 2U).width, 0.5);
    move.name = "Trc2";
    move.diagram = 2U;
    move.dropPosition = TraceDropPosition::BELOW;
    Require(workspace.Apply(move, catalog, {}).ok && workspace.State().diagrams.size() == 3U, "嵌套水平分割失败");
    Near(Region(workspace.State(), 3U).top, 0.5);
    Near(Region(workspace.State(), 3U).width, 0.5);
    move.name = "Trc3";
    move.diagram = 3U;
    move.dropPosition = TraceDropPosition::CENTER;
    Require(workspace.Apply(move, catalog, {}).ok && workspace.State().diagrams.size() == 2U &&
                workspace.State().selectedDiagram == 2U && workspace.State().selectedMeasurement == "Trc3",
        "中心移动应删除空源并映射活动目标编号");
    Near(Region(workspace.State(), 1U).width, 1.0);
    Near(Region(workspace.State(), 2U).width, 1.0);
    Near(Region(workspace.State(), 2U).top, 0.5);
    move.diagram = 2U;
    const auto settledRevision = workspace.State().revision;
    const auto repeated = workspace.Apply(move, catalog, {});
    Require(repeated.ok && !repeated.changed && workspace.State().revision == settledRevision,
        "同一活动迹线落回原图窗中心不得生成无效版本变化");
    for (std::size_t index = 0U; index < before.traces.size(); ++index) {
        const auto &old = before.traces[index];
        const auto &current = workspace.State().traces[index];
        Require(old.name == current.name && old.format == current.format && old.visible == current.visible &&
                    old.markers.SameSettings(current.markers),
            "移动不能复制或重置测量、格式、可见性及标记集合");
    }
    Require(workspace.State().traces.size() == catalog.size(), "移动不能增加或删除测量显示定义");
}

void TestCreationAndInvalidDestinationAreAtomic()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    catalog.push_back(Measurement("Trc2"));
    Require(workspace.Reconcile(catalog).ok &&
                workspace.AddPresentation("Trc2", catalog, 1U, false, TraceDropPosition::ABOVE).ok,
        "创建应连同新图窗分割、归属和选择完成");
    Near(Region(workspace.State(), 2U).top, 0.0);
    Near(Region(workspace.State(), 1U).top, 0.5);
    const auto previous = workspace.Snapshot();
    auto move = Change(DisplayChangeKind::TRACE_MOVE, "Trc1");
    move.diagram = 127U;
    move.dropPosition = TraceDropPosition::RIGHT;
    Require(!workspace.Apply(move, catalog, {}).ok && workspace.State().revision == previous.revision &&
                workspace.State().diagramLayout.SameSettings(previous.diagramLayout),
        "不存在的目标必须原子拒绝");
    Require(!workspace.AddPresentation("Trc2", catalog, 1U, true, TraceDropPosition::LEFT).ok &&
                workspace.State().revision == previous.revision,
        "相对分割不能与旧新建窗口标志组合");
    Require(workspace.RemoveDiagram(1U, true).ok && workspace.State().diagrams.size() == 1U &&
                workspace.State().selectedDiagram == 1U,
        "删除图窗应合并空位并重新编号");
    Near(Region(workspace.State(), 1U).height, 1.0);
    auto grid = Change(DisplayChangeKind::LAYOUT);
    grid.layout = LayoutMode::GRID;
    Require(
        workspace.Apply(grid, catalog, {}).ok && workspace.State().diagramLayout.Empty(), "显式排列图窗应恢复自动网格");
    DisplayState full;
    for (unsigned number = 1U; number <= MAX_DISPLAY_DIAGRAMS; ++number) {
        full.diagrams.push_back({number, number == 1U ? std::vector<std::string>{"Trc1"} : std::vector<std::string>{},
            number == 1U ? "Trc1" : ""});
    }
    DisplayWorkspace capacity(full);
    const std::vector<VnaMeasurement::Measurement> single{Measurement()};
    Require(capacity.Reconcile(single).ok, "满容量图窗前置状态失败");
    const auto fullBefore = capacity.Snapshot();
    move.diagram = 1U;
    Require(!capacity.Apply(move, single, {}).ok && capacity.State().revision == fullBefore.revision &&
                capacity.State().diagrams.size() == MAX_DISPLAY_DIAGRAMS &&
                capacity.State().diagramLayout.SameSettings(fullBefore.diagramLayout),
        "图窗容量不足不能留下候选分割树、空图窗或选择变化");
}

void TestReconcileAndMatrixClearSplitStructure()
{
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    DisplayWorkspace workspace;
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto move = Change(DisplayChangeKind::TRACE_MOVE, "Trc2");
    move.diagram = 1U;
    move.dropPosition = TraceDropPosition::LEFT;
    Require(workspace.Apply(move, catalog, {}).ok, "分割失败");
    catalog.erase(catalog.begin());
    Require(workspace.Reconcile(catalog).ok && workspace.State().diagrams.size() == 1U &&
                workspace.State().selectedDiagram == 1U,
        "删除测量产生空图窗应同步合并布局并映射图号");
    Near(Region(workspace.State(), 1U).width, 1.0);
    catalog.clear();
    for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
        for (unsigned source = 1U; source <= 4U; ++source) {
            auto trace = Measurement("Trc" + std::to_string(catalog.size() + 1U), receiver);
            trace.sourcePort = source;
            catalog.push_back(trace);
        }
    }
    Require(workspace.ReplaceSParameterMatrix(catalog, 4U).ok && workspace.State().diagramLayout.Empty() &&
                DisplayWorkspace::GridGeometry(workspace.State()).columns == 4U &&
                workspace.State().diagrams.size() == 16U,
        "4×4矩阵必须恢复正式矩阵规则，不继承拖动分割结构");
}

void TestMaximizeAndSameSourceMoveRestoreSpace()
{
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    DisplayWorkspace workspace;
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto move = Change(DisplayChangeKind::TRACE_MOVE, "Trc2");
    move.diagram = 1U;
    move.dropPosition = TraceDropPosition::ABOVE;
    Require(workspace.Apply(move, catalog, {}).ok, "分割失败");
    const auto arranged = workspace.Snapshot();
    auto layout = Change(DisplayChangeKind::LAYOUT);
    layout.diagram = 2U;
    layout.layout = LayoutMode::MAXIMIZED;
    Require(
        workspace.Apply(layout, catalog, {}).ok && workspace.State().diagramLayout.SameSettings(arranged.diagramLayout),
        "最大化不能删除正式分割结构");
    layout.layout = LayoutMode::GRID;
    Require(
        workspace.Apply(layout, catalog, {}).ok && workspace.State().diagramLayout.SameSettings(arranged.diagramLayout),
        "从最大化恢复必须保持原图窗几何");
    Require(
        workspace.Apply(Change(DisplayChangeKind::OVERLAY), catalog, {}).ok && workspace.State().diagramLayout.Empty(),
        "Overlay All应恢复自动单图窗");
    Require(
        workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok && workspace.State().diagramLayout.Empty(),
        "Split All应使用自动网格而非旧分割结构");
    const std::vector<VnaMeasurement::Measurement> single{Measurement()};
    DisplayWorkspace sole;
    Require(sole.Reconcile(single).ok, "单迹线前置状态失败");
    move.name = "Trc1";
    move.dropPosition = TraceDropPosition::RIGHT;
    Require(sole.Apply(move, single, {}).ok && sole.State().diagrams.size() == 1U &&
                sole.State().selectedMeasurement == "Trc1",
        "同源独占迹线移到边缘不能留下无测量的源窗口");
    Near(Region(sole.State(), 1U).width, 1.0);
    Near(Region(sole.State(), 1U).height, 1.0);
}

void TestSparseWindowCapacityAndWholeCatalogReplacement()
{
    DisplayState sparse;
    sparse.diagrams = {{1U, {"Trc1"}, "Trc1"}, {128U, {"Trc2"}, "Trc2"}};
    DisplayWorkspace workspace(sparse);
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok &&
                workspace.AddPresentation("Trc3", catalog, 1U, false, TraceDropPosition::RIGHT).ok &&
                workspace.State().diagrams.size() == 3U,
        "稀疏窗口最高编号128不能被当成128个图窗容量耗尽");
    Require(DiagramCollection::Find(workspace.State(), 128U) != nullptr, "相对创建不应改变无关已有窗口编号");
    const std::vector<VnaMeasurement::Measurement> replacement{Measurement("Hidden")};
    Require(workspace.Reconcile(replacement).ok && workspace.State().diagrams.size() == 1U &&
                workspace.State().diagrams.front().measurementNames.empty() && workspace.State().diagramLayout.Empty(),
        "目录替换导致全部原图窗消失时，默认空图窗必须具有可用的自动布局");
    Require(workspace.AddPresentation("Hidden", replacement, 1U, false, TraceDropPosition::LEFT).ok,
        "默认空图窗不能残留无叶编号的自定义树而拒绝后续分割");
}

void TestInvisibleTraceMovesButUnfedDefinitionCannot()
{
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    DisplayWorkspace workspace;
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto visible = Change(DisplayChangeKind::VISIBLE, "Trc2");
    visible.visible = false;
    Require(workspace.Apply(visible, catalog, {}).ok, "隐藏曲线失败");
    auto move = Change(DisplayChangeKind::TRACE_MOVE, "Trc2");
    move.diagram = 1U;
    move.dropPosition = TraceDropPosition::RIGHT;
    Require(workspace.Apply(move, catalog, {}).ok && !workspace.State().traces.back().visible &&
                workspace.State().selectedDiagram == 2U,
        "曲线不可见仍可移动标题，移动不能自动显示曲线");
    auto unfeed = Change(DisplayChangeKind::DISPLAYED, "Trc1");
    unfeed.enabled = false;
    Require(workspace.Apply(unfeed, catalog, {}).ok, "解除显示归属失败");
    const auto before = workspace.Snapshot();
    move.name = "Trc1";
    move.diagram = 2U;
    move.dropPosition = TraceDropPosition::CENTER;
    Require(!workspace.Apply(move, catalog, {}).ok && workspace.State().revision == before.revision &&
                workspace.State().displayedNames == before.displayedNames &&
                workspace.State().diagramLayout.SameSettings(before.diagramLayout),
        "没有图窗归属的测量定义不能通过移动意图被重新显示");
}

void TestLegacyNewDiagramIgnoresUnusedTarget()
{
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    DisplayWorkspace workspace;
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto marker = Change(DisplayChangeKind::MARKER, "Trc1");
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    Require(workspace.Apply(marker, catalog, {}).ok, "源标记前置状态失败");
    Require(workspace.AddPresentation("Trc2", catalog, 127U, true).ok && workspace.State().selectedDiagram == 2U &&
                workspace.State().selectedMeasurement == "Trc2",
        "旧新窗新增合同应忽略原目标编号，直接创建独立图窗");
    Require(workspace.ClonePresentation("Trc1", "Trc3", catalog, 128U, true).ok &&
                workspace.State().selectedDiagram == 3U && workspace.State().selectedMeasurement == "Trc3" &&
                workspace.State().traces.front().markers.SameSettings(workspace.State().traces.back().markers),
        "旧新窗复制合同应忽略原目标并保留源标记，不能被不存在的目标编号拒绝");
}
}  // 匿名命名空间

int main()
{
    try {
        TestMoveKeepsTraceSettingsAndCollapsesEmptySource();
        TestCreationAndInvalidDestinationAreAtomic();
        TestReconcileAndMatrixClearSplitStructure();
        TestMaximizeAndSameSourceMoveRestoreSpace();
        TestSparseWindowCapacityAndWholeCatalogReplacement();
        TestInvisibleTraceMovesButUnfedDefinitionCannot();
        TestLegacyNewDiagramIgnoresUnusedTarget();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
