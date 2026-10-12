/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/display_workspace.h"

#include <exception>
#include <iostream>
#include <limits>

#include "display_test_support.h"
#include "vna_display/trace_projection.h"
#include "vna_measurement/measurement_processor.h"

namespace {

using namespace VnaDisplay;
using DisplayTest::Change;
using DisplayTest::Frame;
using DisplayTest::Measurement;
using DisplayTest::Near;
using DisplayTest::Require;

void TestSharedStateAndNoOp()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    const auto initial = workspace.Snapshot();
    Require(initial.selectedMeasurement == "Trc1" && initial.displayedNames == std::vector<std::string>{"Trc1"},
        "初始选择与显示槽不一致");
    auto format = Change(DisplayChangeKind::FORMAT);
    format.format = DisplayFormat::PHASE;
    Require(workspace.Apply(format, catalog, {}).changed, "格式变化未提交");
    const auto first = workspace.Snapshot();
    const auto result = workspace.Apply(format, catalog, {});
    Require(result.ok && !result.changed && workspace.Snapshot().revision == first.revision, "无变化操作增加版本");
    Require(initial.traces.front().format == DisplayFormat::DB, "显示快照被后续操作修改");
    Require(first.traces.front().format == DisplayFormat::PHASE, "状态不能独立供观察窗口读取");
    auto disabledMarker = Change(DisplayChangeKind::MARKER);
    disabledMarker.marker.hasEnabled = true;
    disabledMarker.marker.enabled = false;
    Require(!workspace.Apply(disabledMarker, catalog, Frame()).changed, "关闭已关闭标记仍修改了隐藏位置");
}

void TestScalePatchRetainsReference()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto scale = Change(DisplayChangeKind::SCALE);
    scale.scale.hasPerDivision = true;
    scale.scale.perDivision = 5.0;
    Require(workspace.Apply(scale, catalog, {}).ok, "单项每格尺度修改失败");
    auto applied = workspace.Snapshot().traces.front().scale;
    Near(applied.min, -45.0);
    Near(applied.max, 5.0);
    scale.scale = {};
    scale.scale.hasReferencePosition = true;
    scale.scale.referencePosition = 5.0;
    Require(workspace.Apply(scale, catalog, {}).ok, "参考位置修改失败");
    applied = workspace.Snapshot().traces.front().scale;
    Near(applied.min, -25.0);
    Near(applied.max, 25.0);
    const auto revision = workspace.Snapshot().revision;
    scale.scale.perDivision = -1.0;
    scale.scale.hasPerDivision = true;
    Require(!workspace.Apply(scale, catalog, {}).ok && workspace.Snapshot().revision == revision,
        "失败尺度修改不是原子操作");
}

void TestDisplaySlotsAndHiddenSelection()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog;
    for (unsigned index = 1U; index <= 5U; ++index) {
        catalog.push_back(Measurement("Trc" + std::to_string(index)));
    }
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto select = Change(DisplayChangeKind::SELECT, "Trc5");
    Require(workspace.Apply(select, catalog, {}).ok, "选择非显示测量失败");
    Require(workspace.Snapshot().displayedNames.size() == 5U && workspace.Snapshot().displayedNames.front() == "Trc1" &&
                workspace.Snapshot().displayedNames.back() == "Trc5",
        "第五条迹线不能替换已有显示槽");
    auto hide = Change(DisplayChangeKind::DISPLAYED);
    hide.hasDisplayedNames = true;
    Require(workspace.Apply(hide, catalog, {}).ok, "空显示集被拒绝");
    Require(workspace.Snapshot().displayedNames.empty() && workspace.Snapshot().selectedMeasurement == "Trc5",
        "隐藏所有曲线破坏 SCPI 测量选择");
    hide.hasDisplayedNames = false;
    hide.name = "Trc3";
    hide.enabled = true;
    Require(workspace.Apply(hide, catalog, {}).ok && workspace.Snapshot().displayedNames.front() == "Trc3",
        "后端未从名称插入显示槽");
    const auto revision = workspace.Snapshot().revision;
    hide.hasDisplayedNames = true;
    hide.displayedNames = {"Trc1", "Trc1"};
    Require(!workspace.Apply(hide, catalog, {}).ok && workspace.Snapshot().revision == revision,
        "重复显示名称没有原子拒绝");
}

void TestCatalogReconcilePreservesAndRemoves()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto smith = Change(DisplayChangeKind::FORMAT);
    smith.format = DisplayFormat::SMITH;
    Require(workspace.Apply(smith, catalog, {}).ok, "反射 Smith 失败");
    catalog[0] = Measurement("Trc1", 2U);
    Require(workspace.Reconcile(catalog).ok, "测量改变未重新协调显示状态");
    Require(workspace.Snapshot().traces.front().format == DisplayFormat::SMITH, "目录协调不能按反射量限制 Smith");
    auto visible = Change(DisplayChangeKind::VISIBLE, "Trc2");
    visible.visible = false;
    Require(workspace.Apply(visible, catalog, {}).ok, "隐藏曲线失败");
    catalog.erase(catalog.begin());
    Require(workspace.Reconcile(catalog).ok, "删除目录项失败");
    const auto state = workspace.Snapshot();
    Require(state.traces.size() == 1U && state.selectedMeasurement == "Trc2" && !state.traces.front().visible,
        "删除没有保留剩余迹线状态及选择");
}

void TestAutoscaleAndPlanGuard()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto autoscale = Change(DisplayChangeKind::AUTOSCALE);
    autoscale.checkAcquisitionRevision = true;
    autoscale.expectedAcquisitionRevision = 6U;
    const auto frame = Frame();
    const auto revision = workspace.Snapshot().revision;
    Require(!workspace.Apply(autoscale, catalog, frame).ok && workspace.Snapshot().revision == revision,
        "旧物理版本 Autoscale 被应用");
    autoscale.expectedAcquisitionRevision = 7U;
    Require(workspace.Apply(autoscale, catalog, frame).ok, "完整帧 Autoscale 失败");
    const auto fixed = workspace.Snapshot().traces.front().scale;
    const auto another = Frame(100U);
    const auto projected = TraceProjection::Calculate(workspace.State().traces.front(),
        VnaMeasurement::MeasurementProcessor::Calculate(*another.frame, catalog.front()),
        another.frame->plan.frequencyHz);
    Require(projected.available, "后续帧投影失败");
    Near(workspace.Snapshot().traces.front().scale.min, fixed.min);
    Near(workspace.Snapshot().traces.front().scale.max, fixed.max);
    Require(!workspace.Apply(autoscale, catalog, {}).ok, "无当前帧 Autoscale 被接受");
}

void TestMarkerSampleStepAndStalePlan()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    const auto frame = Frame(10001U, true);
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 7777U;
    Require(workspace.Apply(marker, catalog, frame).ok, "CW 原始采样标记失败");
    const auto point = workspace.Snapshot().traces.front().markers.Active();
    Require(point.hasSampleIndex && point.sampleIndex == 7777U && !point.interpolate, "CW 采样身份丢失");
    marker.marker = {};
    marker.marker.hasStep = true;
    marker.marker.step = 1;
    Require(workspace.Apply(marker, catalog, frame).ok &&
                workspace.State().traces.front().markers.Active().sampleIndex == 7778U,
        "标记没有移动一个真实原始点");
    marker.marker.step = std::numeric_limits<int>::min();
    Require(workspace.Apply(marker, catalog, frame).ok &&
                workspace.State().traces.front().markers.Active().sampleIndex == 0U,
        "标记负步长没有安全钳制");
    const auto revision = workspace.State().revision;
    marker.marker.checkAcquisitionRevision = true;
    marker.marker.expectedAcquisitionRevision = 6U;
    Require(!workspace.Apply(marker, catalog, frame).ok && workspace.State().revision == revision,
        "旧采集计划的点击写入被接受");
}

void TestRevisionExhaustionAndSnapshotIndependence()
{
    DisplayWorkspace initial;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(initial.Reconcile(catalog).ok, "初始目录失败");
    auto snapshot = initial.Snapshot();
    snapshot.revision = std::numeric_limits<std::uint64_t>::max();
    DisplayWorkspace exhausted(snapshot);
    auto visible = Change(DisplayChangeKind::VISIBLE);
    visible.visible = false;
    Require(!exhausted.Apply(visible, catalog, {}).ok && exhausted.State().traces.front().visible,
        "显示版本溢出或失败后状态改变");
    visible.visible = true;
    const auto noOp = exhausted.Apply(visible, catalog, {});
    Require(noOp.ok && !noOp.changed, "版本耗尽后无变化操作被错误拒绝");
    snapshot.traces.front().visible = false;
    Require(exhausted.State().traces.front().visible, "外部快照修改污染仪器状态");
}

void TestMarkerFrequencyBeforeFirstFrame()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    const std::vector<double> axis{10e6, 11e6, 20e6};
    FrameContext context;
    context.frequencyHz = &axis;
    context.acquisitionRevision = 1U;
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasFrequency = true;
    marker.marker.frequencyHz = 9e6;
    const auto revision = workspace.State().revision;
    Require(!workspace.Apply(marker, catalog, context).ok && workspace.State().revision == revision &&
                !workspace.State().traces.front().markers.Active().enabled,
        "首帧前超扫描范围频率没有原子拒绝");
    marker.marker.frequencyHz = 15e6;
    Require(workspace.Apply(marker, catalog, context).ok, "首帧前合法频率不能保存");
    Near(workspace.State().traces.front().markers.Active().frequencyHz, 15e6);
    marker.marker.frequencyHz = 21e6;
    Require(!workspace.Apply(marker, catalog, context).ok, "首帧前大于扫描终点的频率被钳制接受");
    Near(workspace.State().traces.front().markers.Active().frequencyHz, 15e6);
    marker.marker.frequencyHz = 10e6;
    Require(workspace.Apply(marker, catalog, context).ok, "扫描起点频率被拒绝");
}

void TestAxisReconcileKeepsSavedAndActualCoordinatesConsistent()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    const std::vector<double> oldAxis{1e6, 50e6, 100e6};
    FrameContext context;
    context.frequencyHz = &oldAxis;
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasFrequency = true;
    marker.marker.frequencyHz = 90e6;
    Require(workspace.Apply(marker, catalog, context).ok, "保存旧范围标记失败");
    auto exhaustedState = workspace.Snapshot();
    exhaustedState.revision = std::numeric_limits<std::uint64_t>::max();
    DisplayWorkspace exhausted(exhaustedState);
    const std::vector<double> newAxis{1e6, 5e6, 10e6};
    Require(!exhausted.ReconcileAxis(newAxis, false).ok, "版本耗尽后协调刺激轴仍成功");
    Near(exhausted.State().traces.front().markers.Active().frequencyHz, 90e6);
    Require(workspace.ReconcileAxis(newAxis, false).ok, "新刺激轴协调失败");
    const auto projection =
        TraceProjection::Calculate(workspace.State().traces.front(), DisplayTest::Trace({0.1, 0.2, 0.3}), newAxis, 8U);
    Near(workspace.State().traces.front().markers.Active().frequencyHz, 10e6);
    Near(projection.marker.frequencyHz, workspace.State().traces.front().markers.Active().frequencyHz);
    const auto revision = workspace.State().revision;
    Require(workspace.ReconcileAxis(newAxis, false).ok && workspace.State().revision == revision,
        "重复协调同一位置增加显示版本");
    Require(!workspace.ReconcileAxis({}, false).ok && workspace.State().revision == revision, "无效刺激轴不是原子拒绝");
}

void TestSameAxisHardwareRevisionPreservesCwSample()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement()};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    const auto frame = Frame(10001U, true);
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 7777U;
    Require(workspace.Apply(marker, catalog, frame).ok, "CW 标记设置失败");
    const auto revision = workspace.State().revision;
    Require(workspace.ReconcileAxis(frame.frame->plan.frequencyHz, true).ok, "相同轴协调失败");
    workspace.BindAcquisitionRevision(8U);
    const auto trace = VnaMeasurement::MeasurementProcessor::Calculate(*frame.frame, catalog.front());
    const auto projection =
        TraceProjection::Calculate(workspace.State().traces.front(), trace, frame.frame->plan.frequencyHz, 8U);
    Require(workspace.State().revision == revision &&
                workspace.State().traces.front().markers.Active().sampleIndex == 7777U &&
                projection.marker.sampleIndex == 7777U,
        "IFBW 等同轴配置变更使 CW 标记跳回中点或修改显示版本");
    workspace.BindAcquisitionRevision(9U);
    Require(workspace.State().revision == revision, "隐藏采集版本绑定修改可见显示版本");
}

void TestEmptyAndMergedDiagramsArePureProjections()
{
    DisplayState empty;
    for (const auto layout : {LayoutMode::SINGLE, LayoutMode::GRID, LayoutMode::MAXIMIZED}) {
        empty.layout = layout;
        const auto diagrams = DisplayWorkspace::ArrangeDiagrams(empty);
        Require(diagrams.size() == 1U && diagrams.front().number == 1U && diagrams.front().measurementNames.empty() &&
                    diagrams.front().primaryMeasurement.empty(),
            "空显示集没有保留空图窗 1");
    }
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc2"), catalog, {}).ok, "选择主迹线失败");
    const auto before = workspace.Snapshot();
    const auto diagrams = DisplayWorkspace::ArrangeDiagrams(before);
    Require(diagrams.size() == 1U && diagrams.front().measurementNames == std::vector<std::string>{"Trc1", "Trc2"} &&
                diagrams.front().primaryMeasurement == "Trc2",
        "单图没有按有序槽合并并采用组内选中项");
    Require(workspace.State().revision == before.revision && workspace.State().selectedMeasurement == "Trc2",
        "图窗派生修改了权威状态或显示版本");
}

void TestMixedFormatsUseBackendDiagramGroups()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{
        Measurement(), Measurement("Trc2"), Measurement("Trc3"), Measurement("Trc4")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    auto smith = Change(DisplayChangeKind::FORMAT);
    smith.format = DisplayFormat::SMITH;
    Require(workspace.Apply(smith, catalog, {}).ok, "Smith 格式设置失败");
    smith.name = "Trc3";
    Require(workspace.Apply(smith, catalog, {}).ok, "第二条 Smith 格式设置失败");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc3"), catalog, {}).ok, "选择 Smith 主迹线失败");
    const auto diagrams = DisplayWorkspace::ArrangeDiagrams(workspace.State());
    Require(diagrams.size() == 1U && diagrams.front().measurementNames == workspace.State().displayedNames &&
                diagrams.front().primaryMeasurement == "Trc3",
        "混合格式必须保留原图窗和有序目录");
    const auto revision = workspace.State().revision;
    Require(DisplayWorkspace::ArrangeDiagrams(workspace.State()).size() == 1U && workspace.State().revision == revision,
        "读取图窗不能重写显示状态");
}

void TestGridAndMaximizedRetainOriginalDiagramNumber()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{
        Measurement(), Measurement("Trc2"), Measurement("Trc3"), Measurement("Trc4")};
    Require(workspace.Reconcile(catalog).ok, "初始目录失败");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc3"), catalog, {}).ok, "选择第三条迹线失败");
    auto layout = Change(DisplayChangeKind::LAYOUT);
    layout.layout = LayoutMode::GRID;
    Require(workspace.Apply(layout, catalog, {}).ok && workspace.State().diagrams.size() == 1U,
        "All Diagrams 不能偷偷拆分已有图窗");
    Require(workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok, "Split All 失败");
    const auto grid = DisplayWorkspace::ArrangeDiagrams(workspace.State());
    Require(grid.size() == 4U, "Split All 没有按有序目录生成图窗");
    for (std::size_t index = 0U; index < grid.size(); ++index) {
        Require(grid[index].number == index + 1U && grid[index].measurementNames.size() == 1U &&
                    grid[index].primaryMeasurement == catalog[index].name,
            "图号或主迹线不对应目录");
    }
    layout.layout = LayoutMode::MAXIMIZED;
    Require(workspace.Apply(layout, catalog, {}).ok && workspace.State().selectedDiagram == 3U, "最大化未保留当前图号");
    Require(DisplayWorkspace::ArrangeDiagrams(workspace.State()).size() == 4U &&
                workspace.State().diagrams[2U].primaryMeasurement == "Trc3",
        "最大化不能删除其他图窗");
}

void TestManyTracesUseBoundedFormatSafeDiagrams()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog;
    for (std::size_t number = 1U; number <= MAX_DISPLAY_TRACES; ++number) {
        catalog.push_back(Measurement("Trc" + std::to_string(number)));
    }
    Require(workspace.Reconcile(catalog).ok && workspace.State().displayedNames.size() == MAX_DISPLAY_TRACES,
        "目录预算内所有迹线均应进入显示集");
    Require(workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok &&
                workspace.State().diagrams.size() == MAX_DISPLAY_TRACES,
        "Split All 不能静默限制为四个图窗");
    const auto lastName = "Trc" + std::to_string(MAX_DISPLAY_TRACES);
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, lastName), catalog, {}).ok &&
                workspace.State().selectedDiagram == MAX_DISPLAY_TRACES,
        "选择末尾迹线应选择所属图窗");
    Require(workspace.Apply(Change(DisplayChangeKind::OVERLAY), catalog, {}).ok &&
                workspace.State().diagrams.size() == 1U &&
                workspace.State().displayedNames.size() == MAX_DISPLAY_TRACES,
        "Overlay All 必须完整保留全部迹线");
    const auto revision = workspace.State().revision;
    catalog.push_back(Measurement("Overflow"));
    Require(!workspace.Reconcile(catalog).ok && workspace.State().revision == revision, "超出仪器预算必须原子拒绝");
}

void TestPresentationCloneIsAtomicAndOwnsMarkers()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3", 2U)};
    Require(workspace.Reconcile(catalog).ok, "准备显示复制目录");
    auto format = Change(DisplayChangeKind::FORMAT);
    format.format = DisplayFormat::SMITH;
    Require(workspace.Apply(format, catalog, {}).ok, "设置源 Smith 格式");
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.id = 2U;
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 1U;
    Require(workspace.Apply(marker, catalog, Frame()).ok, "准备源 M2");
    const auto before = workspace.Snapshot();
    Require(workspace.ClonePresentation("Trc1", "Trc2", catalog).changed, "复制显示并选择目标");
    Require(workspace.State().revision == before.revision + 1U && workspace.State().selectedMeasurement == "Trc2" &&
                workspace.State().traces[1U].format == DisplayFormat::SMITH &&
                workspace.State().traces[1U].markers.SameSettings(before.traces[0U].markers),
        "显示设置与选择必须在同一次显示域提交中完成");
    const auto revision = workspace.State().revision;
    Require(!workspace.ClonePresentation("Missing", "Trc2", catalog).ok &&
                !workspace.ClonePresentation("Trc1", "Missing", catalog).ok &&
                !workspace.ClonePresentation("Trc1", "Trc1", catalog).ok && workspace.State().revision == revision &&
                workspace.State().selectedMeasurement == "Trc2" &&
                workspace.State().traces[2U].format == DisplayFormat::DB,
        "名称不存在或同名复制必须原子拒绝");
    marker.name = "Trc2";
    marker.marker.sampleIndex = 2U;
    Require(workspace.Apply(marker, catalog, Frame()).ok &&
                workspace.State().traces[0U].markers.SameSettings(before.traces[0U].markers) &&
                workspace.State().traces[1U].markers.Find(2U)->sampleIndex == 2U,
        "显示域中的源、目标标记集合必须互相独立");
}

}  // 匿名命名空间

int main()
{
    try {
        TestSharedStateAndNoOp();
        TestScalePatchRetainsReference();
        TestDisplaySlotsAndHiddenSelection();
        TestCatalogReconcilePreservesAndRemoves();
        TestAutoscaleAndPlanGuard();
        TestMarkerSampleStepAndStalePlan();
        TestRevisionExhaustionAndSnapshotIndependence();
        TestMarkerFrequencyBeforeFirstFrame();
        TestAxisReconcileKeepsSavedAndActualCoordinatesConsistent();
        TestSameAxisHardwareRevisionPreservesCwSample();
        TestEmptyAndMergedDiagramsArePureProjections();
        TestMixedFormatsUseBackendDiagramGroups();
        TestGridAndMaximizedRetainOriginalDiagramNumber();
        TestManyTracesUseBoundedFormatSafeDiagrams();
        TestPresentationCloneIsAtomicAndOwnsMarkers();
        std::cout << "15 个共享显示状态验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
