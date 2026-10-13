/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/diagram_collection.h"

#include <exception>
#include <iostream>

#include "display_test_support.h"

namespace {
using namespace VnaDisplay;
using DisplayTest::Change;
using DisplayTest::Measurement;
using DisplayTest::Require;

void TestFormatDoesNotCreateDiagram()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2", 2U)};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto format = Change(DisplayChangeKind::FORMAT, "Trc2");
    format.format = DisplayFormat::SMITH;
    Require(workspace.Apply(format, catalog, {}).ok, "传输量允许 Smith");
    Require(workspace.State().diagrams.size() == 1U && workspace.State().diagrams.front().measurementNames.size() == 2U,
        "格式改变不能新增图窗或移动迹线");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc2"), catalog, {}).ok &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2",
        "活动迹线控制同图窗坐标");
}

void TestExplicitMembershipAndMaximize()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    Require(workspace.AddPresentation("Trc3", catalog, 0U, true).ok, "新迹线加新图窗失败");
    Require(workspace.State().diagrams.size() == 2U && workspace.State().selectedDiagram == 2U &&
                workspace.State().diagrams.front().measurementNames == std::vector<std::string>{"Trc1", "Trc2"},
        "新图窗不能移走已有其他迹线");
    auto maximize = Change(DisplayChangeKind::LAYOUT);
    maximize.diagram = 1U;
    maximize.layout = LayoutMode::MAXIMIZED;
    Require(workspace.Apply(maximize, catalog, {}).ok && workspace.State().selectedDiagram == 1U &&
                DisplayWorkspace::ArrangeDiagrams(workspace.State()).size() == 2U,
        "最大化仍保存并发布全部图窗");
    const auto revision = workspace.State().revision;
    maximize.diagram = 127U;
    Require(!workspace.Apply(maximize, catalog, {}).ok && workspace.State().revision == revision,
        "无效上下文图号必须原子拒绝");
    Require(workspace.Apply(Change(DisplayChangeKind::OVERLAY), catalog, {}).ok &&
                workspace.State().diagrams.size() == 1U && workspace.State().displayedNames.size() == 3U,
        "Overlay All必须保留全部迹线");
    Require(
        workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok && workspace.State().diagrams.size() == 3U,
        "Split All不是Quad Split");
}

void TestLocalSlotsAndViewDeletion()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto add = Change(DisplayChangeKind::DIAGRAM_ADD);
    add.diagram = 4U;
    Require(workspace.Apply(add, catalog, {}).ok, "创建 SCPI 窗口失败");
    auto feed = Change(DisplayChangeKind::SELECT, "Trc2");
    feed.diagram = 4U;
    feed.hasSlot = true;
    feed.slot = 1U;
    Require(workspace.Apply(feed, catalog, {}).ok, "FEED窗口内槽失败");
    auto scale = Change(DisplayChangeKind::SCALE, "");
    scale.diagram = 4U;
    scale.hasSlot = true;
    scale.slot = 1U;
    scale.scale.hasPerDivision = true;
    scale.scale.perDivision = 3.0;
    Require(workspace.Apply(scale, catalog, {}).ok && workspace.State().traces[1U].scale.max == 3.0 &&
                workspace.State().traces[0U].scale.max == 10.0,
        "窗口4的槽1不能修改窗口1的槽1");
    auto remove = Change(DisplayChangeKind::DIAGRAM_REMOVE);
    remove.diagram = 4U;
    Require(workspace.Apply(remove, catalog, {}).ok && workspace.State().traces.size() == 3U &&
                workspace.State().diagrams.size() == 1U,
        "SCPI删除显示窗口不能删除定义");
    remove.diagram = 1U;
    const auto revision = workspace.State().revision;
    Require(!workspace.Apply(remove, catalog, {}).ok && workspace.State().revision == revision, "最后图窗必须保护");
}

void TestDeleteTraceRemovesEmptyDiagramAndRenumbers()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok,
        "准备三个图窗失败");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc3"), catalog, {}).ok, "选择第三图窗失败");
    catalog.erase(catalog.begin() + 1U);
    Require(workspace.Reconcile(catalog).ok && workspace.State().diagrams.size() == 2U &&
                workspace.State().diagrams[1U].number == 2U && workspace.State().selectedDiagram == 2U &&
                workspace.State().selectedMeasurement == "Trc3",
        "删除独占迹线后图窗连续重编号并保留选择");
}

void TestNonMonotonicWindowNumbersRemapSelectionOnce()
{
    DisplayState state;
    state.diagrams = {{1U, {"Trc1"}, "Trc1"}, {3U, {"Trc3"}, "Trc3"}, {2U, {"Trc2"}, "Trc2"}, {4U, {"Trc4"}, "Trc4"}};
    state.selectedDiagram = 3U;
    state.selectedMeasurement = "Trc3";
    DisplayWorkspace workspace(state);
    Require(workspace.RemoveDiagram(4U, true).ok && workspace.State().selectedDiagram == 2U &&
                workspace.State().diagrams[1U].primaryMeasurement == "Trc3" &&
                workspace.State().selectedMeasurement == "Trc3",
        "重编号不得使用更新后的选中号再次匹配旧图号");
    state.diagrams.back().measurementNames = {"Deleted"};
    DisplayWorkspace reconcile(state);
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(reconcile.Reconcile(catalog).ok && reconcile.State().selectedDiagram == 2U &&
                reconcile.State().selectedMeasurement == "Trc3",
        "目录删除独占迹线时也必须只映射一次");
}

void TestMeasurementSelectionDoesNotFeedHiddenDefinition()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    catalog.push_back(Measurement("Hidden"));
    Require(workspace.Reconcile(catalog).ok, "新增未显示定义失败");
    const auto before = workspace.State().diagrams.front().measurementNames;
    Require(workspace.Apply(Change(DisplayChangeKind::MEASUREMENT_SELECT, "Hidden"), catalog, {}).ok &&
                workspace.State().selectedMeasurement == "Hidden" &&
                workspace.State().diagrams.front().measurementNames == before,
        "CALC测量选择不能隐式FEED隐藏定义");
}

void TestSparseSlotsSurviveDeletionAndRefeed()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto feed = Change(DisplayChangeKind::SELECT, "Trc1");
    feed.diagram = 1U;
    feed.hasSlot = true;
    feed.slot = 3U;
    Require(workspace.Apply(feed, catalog, {}).ok &&
                *DiagramCollection::NameAtSlot(workspace.State().diagrams.front(), 3U) == "Trc1" &&
                *DiagramCollection::NameAtSlot(workspace.State().diagrams.front(), 2U) == "Trc2" &&
                DiagramCollection::NameAtSlot(workspace.State().diagrams.front(), 1U) == nullptr,
        "重馈旧成员不能压缩或丢失指定显示槽");
    auto remove = Change(DisplayChangeKind::DISPLAYED, "Trc2");
    remove.diagram = 1U;
    remove.enabled = false;
    Require(workspace.Apply(remove, catalog, {}).ok &&
                *DiagramCollection::NameAtSlot(workspace.State().diagrams.front(), 3U) == "Trc1",
        "删除一个槽不能重编号其他槽");
    feed.name = "Trc3";
    feed.slot = 20U;
    Require(workspace.Apply(feed, catalog, {}).ok &&
                *DiagramCollection::NameAtSlot(workspace.State().diagrams.front(), 20U) == "Trc3",
        "FEED 必须允许合法的稀疏局部编号");
}

void TestUnfeedWrongWindowDoesNotChangeSelection()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok &&
                workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc2"), catalog, {}).ok,
        "准备双图窗失败");
    auto remove = Change(DisplayChangeKind::DISPLAYED, "Trc2");
    remove.diagram = 1U;
    remove.enabled = false;
    const auto revision = workspace.State().revision;
    Require(workspace.Apply(remove, catalog, {}).ok && workspace.State().revision == revision &&
                workspace.State().selectedMeasurement == "Trc2" && workspace.State().selectedDiagram == 2U,
        "在错误图窗关闭未显示成员应幂等且保留选择");
    remove.diagram = 2U;
    Require(workspace.Apply(remove, catalog, {}).ok && workspace.State().selectedMeasurement == "Trc1" &&
                workspace.State().selectedDiagram == 1U,
        "关闭活动成员后选择及所属图窗必须一致");
}

void TestDeleteActiveEmptyDiagramSelectsFallbackTrace()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok,
        "准备三个图窗失败");
    auto move = Change(DisplayChangeKind::SELECT, "Trc1");
    move.diagram = 3U;
    Require(workspace.Apply(move, catalog, {}).ok && workspace.State().diagrams.front().measurementNames.empty(),
        "移动迹线后应保留空源图窗");
    auto select = Change(DisplayChangeKind::DIAGRAM_SELECT);
    select.diagram = 1U;
    Require(workspace.Apply(select, catalog, {}).ok && workspace.State().selectedMeasurement == "Trc1",
        "选择空图窗应保留此前的测量选择");
    Require(workspace.RemoveDiagram(1U, true).ok && workspace.Reconcile(catalog).ok &&
                workspace.State().selectedDiagram == 1U && workspace.State().selectedMeasurement == "Trc2" &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2" &&
                workspace.State().diagrams.back().measurementNames == std::vector<std::string>{"Trc3", "Trc1"},
        "原生删除活动空图窗后，活动迹线必须跟随回退图窗");
}

void TestDeleteInactiveEmptyDiagramPreservesSelection()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok,
        "准备三个图窗失败");
    auto move = Change(DisplayChangeKind::SELECT, "Trc1");
    move.diagram = 3U;
    Require(
        workspace.Apply(move, catalog, {}).ok && workspace.State().selectedDiagram == 3U, "移动迹线后应激活目标图窗");
    Require(workspace.RemoveDiagram(1U, true).ok && workspace.Reconcile(catalog).ok &&
                workspace.State().selectedDiagram == 2U && workspace.State().selectedMeasurement == "Trc1" &&
                workspace.State().diagrams.back().primaryMeasurement == "Trc1",
        "删除非活动空图窗只重映射图号，不应切换当前迹线");
}

void TestCloneUsesSourceDiagramUnlessTargetSpecified()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok,
        "准备三个图窗失败");
    auto move = Change(DisplayChangeKind::SELECT, "Trc1");
    move.diagram = 3U;
    Require(workspace.Apply(move, catalog, {}).ok, "移动源迹线失败");
    auto select = Change(DisplayChangeKind::DIAGRAM_SELECT);
    select.diagram = 1U;
    Require(workspace.Apply(select, catalog, {}).ok && workspace.State().selectedMeasurement == "Trc1",
        "选择空图窗应保留源迹线选择");
    catalog.push_back(Measurement("Trc4"));
    Require(workspace.Reconcile(catalog).ok && workspace.ClonePresentation("Trc1", "Trc4", catalog).ok &&
                workspace.State().selectedDiagram == 3U && workspace.State().selectedMeasurement == "Trc4" &&
                workspace.State().diagrams.front().measurementNames.empty() &&
                workspace.State().diagrams.back().measurementNames == std::vector<std::string>{"Trc3", "Trc1", "Trc4"},
        "默认复制必须与源迹线同图，不能受另选空图窗影响");
    catalog.push_back(Measurement("Trc5"));
    Require(workspace.Reconcile(catalog).ok && workspace.ClonePresentation("Trc1", "Trc5", catalog, 1U).ok &&
                workspace.State().selectedDiagram == 1U && workspace.State().selectedMeasurement == "Trc5" &&
                workspace.State().diagrams.front().measurementNames == std::vector<std::string>{"Trc5"} &&
                workspace.State().diagrams.back().measurementNames == std::vector<std::string>{"Trc3", "Trc1", "Trc4"},
        "显式目标图窗应优先于源迹线归属");
}

DisplayWorkspace RememberedWorkspace(const std::vector<VnaMeasurement::Measurement> &catalog)
{
    DisplayState state;
    state.diagrams = {{1U, {"Trc1", "Trc2"}, "Trc2"}, {2U, {"Trc3", "Trc4"}, "Trc4"}};
    state.diagrams.front().markerInfoVisible = false;
    state.selectedDiagram = 2U;
    state.selectedMeasurement = "Trc4";
    state.infoTable.visible = true;
    state.infoTable.position = InfoTablePosition::LEFT;
    state.annotations.fontScale = 135.0;
    DisplayWorkspace workspace(state);
    Require(workspace.Reconcile(catalog).ok, "准备各图窗记忆的活动迹线失败");
    auto scale = Change(DisplayChangeKind::SCALE, "Trc2");
    scale.scale.hasPerDivision = true;
    scale.scale.perDivision = 3.0;
    Require(workspace.Apply(scale, catalog, {}).ok, "准备未受影响迹线的独立尺度失败");
    auto marker = Change(DisplayChangeKind::MARKER, "Trc2");
    marker.marker.action = MarkerAction::ADD;
    Require(workspace.Apply(marker, catalog, DisplayTest::Frame()).ok, "准备未受影响迹线的标记失败");
    return workspace;
}

void RequireRememberedTraceUnchanged(const DisplayState &before, const DisplayState &after)
{
    const auto find = [](const DisplayState &state) -> const TracePresentation & {
        const auto trace = std::find_if(state.traces.begin(), state.traces.end(), [](const TracePresentation &item) {
            return item.name == "Trc2";
        });
        Require(trace != state.traces.end(), "未受影响迹线丢失");
        return *trace;
    };
    const auto &oldTrace = find(before);
    const auto &newTrace = find(after);
    Require(newTrace.format == oldTrace.format && newTrace.cartesianFormat == oldTrace.cartesianFormat &&
                newTrace.scale.min == oldTrace.scale.min && newTrace.scale.max == oldTrace.scale.max &&
                newTrace.scale.referencePosition == oldTrace.scale.referencePosition &&
                newTrace.smithRadius == oldTrace.smithRadius && newTrace.visible == oldTrace.visible &&
                newTrace.markerInfoLocation == oldTrace.markerInfoLocation &&
                newTrace.markers.SameSettings(oldTrace.markers) &&
                after.infoTable.visible == before.infoTable.visible &&
                after.infoTable.position == before.infoTable.position &&
                after.annotations.fontScale == before.annotations.fontScale &&
                after.diagrams.front().markerInfoVisible == before.diagrams.front().markerInfoVisible,
        "删除成员不应修改未受影响迹线或图窗的保存设置");
}

void TestDeletingActiveTraceUsesCurrentDiagramMemory()
{
    std::vector<VnaMeasurement::Measurement> catalog{
        Measurement(), Measurement("Trc2"), Measurement("Trc3"), Measurement("Trc4")};
    auto workspace = RememberedWorkspace(catalog);
    const auto before = workspace.Snapshot();
    catalog.pop_back();
    Require(workspace.Reconcile(catalog).ok && workspace.State().selectedDiagram == 2U &&
                workspace.State().selectedMeasurement == "Trc3" &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2" &&
                workspace.State().diagrams.back().primaryMeasurement == "Trc3",
        "删除活动迹线应使用同图继任，不应覆盖其他图记忆的活动迹线");
    RequireRememberedTraceUnchanged(before, workspace.State());
}

void TestDeletingInactiveTracePreservesOtherState()
{
    std::vector<VnaMeasurement::Measurement> catalog{
        Measurement(), Measurement("Trc2"), Measurement("Trc3"), Measurement("Trc4")};
    auto workspace = RememberedWorkspace(catalog);
    const auto before = workspace.Snapshot();
    catalog.erase(catalog.begin());
    Require(workspace.Reconcile(catalog).ok && workspace.State().selectedDiagram == before.selectedDiagram &&
                workspace.State().selectedMeasurement == before.selectedMeasurement &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2" &&
                workspace.State().diagrams.back().primaryMeasurement == before.diagrams.back().primaryMeasurement &&
                workspace.State().diagrams.back().measurementNames == before.diagrams.back().measurementNames &&
                workspace.State().diagrams.back().traceSlots == before.diagrams.back().traceSlots,
        "删除非活动成员不应切换活动图、当前迹线或其他图的主迹线");
    RequireRememberedTraceUnchanged(before, workspace.State());
}

void TestDeletingOnlyActiveTraceUsesFallbackDiagramMemory()
{
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc4")};
    auto workspace = RememberedWorkspace(catalog);
    const auto before = workspace.Snapshot();
    catalog.pop_back();
    Require(workspace.Reconcile(catalog).ok && workspace.State().diagrams.size() == 1U &&
                workspace.State().selectedDiagram == 1U && workspace.State().selectedMeasurement == "Trc2" &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2",
        "删除独占活动图后应恢复回退图保存的主迹线，而非目录首条");
    RequireRememberedTraceUnchanged(before, workspace.State());
}

void TestUnfeedActiveTraceUsesCurrentDiagramMemory()
{
    const std::vector<VnaMeasurement::Measurement> catalog{
        Measurement(), Measurement("Trc2"), Measurement("Trc3"), Measurement("Trc4")};
    auto workspace = RememberedWorkspace(catalog);
    const auto before = workspace.Snapshot();
    auto unfeed = Change(DisplayChangeKind::DISPLAYED, "Trc4");
    unfeed.diagram = 2U;
    unfeed.enabled = false;
    Require(workspace.Apply(unfeed, catalog, {}).ok && workspace.State().selectedDiagram == 2U &&
                workspace.State().selectedMeasurement == "Trc3" &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2" &&
                workspace.State().diagrams.back().primaryMeasurement == "Trc3" && workspace.State().traces.size() == 4U,
        "解除活动成员显示应优先同图剩余主迹线，并保留全部测量定义");
    RequireRememberedTraceUnchanged(before, workspace.State());
}
}  // 匿名命名空间

int main()
{
    try {
        TestFormatDoesNotCreateDiagram();
        TestExplicitMembershipAndMaximize();
        TestLocalSlotsAndViewDeletion();
        TestDeleteTraceRemovesEmptyDiagramAndRenumbers();
        TestNonMonotonicWindowNumbersRemapSelectionOnce();
        TestMeasurementSelectionDoesNotFeedHiddenDefinition();
        TestSparseSlotsSurviveDeletionAndRefeed();
        TestUnfeedWrongWindowDoesNotChangeSelection();
        TestDeleteActiveEmptyDiagramSelectsFallbackTrace();
        TestDeleteInactiveEmptyDiagramPreservesSelection();
        TestCloneUsesSourceDiagramUnlessTargetSpecified();
        TestDeletingActiveTraceUsesCurrentDiagramMemory();
        TestDeletingInactiveTracePreservesOtherState();
        TestDeletingOnlyActiveTraceUsesFallbackDiagramMemory();
        TestUnfeedActiveTraceUsesCurrentDiagramMemory();
        std::cout << "15 个显式图窗验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
