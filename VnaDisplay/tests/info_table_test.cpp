/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "display_test_support.h"

namespace {
using namespace VnaDisplay;
using DisplayTest::Change;
using DisplayTest::Measurement;
using DisplayTest::Require;

DisplayChange Table(bool visible, InfoTablePosition position = InfoTablePosition::BOTTOM)
{
    auto change = Change(DisplayChangeKind::INFO_TABLE, "");
    change.infoTable.hasVisible = true;
    change.infoTable.hasPosition = true;
    change.infoTable.settings.visible = visible;
    change.infoTable.settings.position = position;
    return change;
}

void IndependentContainerAndFieldAssignment()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    Require(workspace.Reconcile(catalog).ok, "初始化目录失败");
    const auto original = workspace.Snapshot();
    Require(!original.infoTable.visible && original.infoTable.position == InfoTablePosition::BOTTOM &&
                original.diagrams.front().markerInfoVisible && !original.annotations.activeTraceOnly &&
                original.annotations.stimulusInfoVisible && original.annotations.fontScale == 100.0,
        "出厂显示设置不符合独立容器、图窗字段和全局注释的默认值");
    auto move = Change(DisplayChangeKind::MARKER_INFO_LOCATION, "Trc2");
    move.markerInfoLocation = MarkerInfoLocation::TABLE;
    Require(workspace.Apply(move, catalog, {}).changed, "移入 Info Table 失败");
    Require(!workspace.State().infoTable.visible &&
                workspace.State().selectedMeasurement == original.selectedMeasurement &&
                workspace.State().traces.front().markerInfoLocation == MarkerInfoLocation::DIAGRAM &&
                workspace.State().traces.back().markerInfoLocation == MarkerInfoLocation::TABLE,
        "迁移字段不能自动打开表格、选择别的迹线或迁移其他字段");
    const auto revision = workspace.State().revision;
    Require(workspace.Apply(move, catalog, {}).ok && workspace.State().revision == revision,
        "重复迁移相同字段不能增加显示版本");
    Require(workspace.Apply(Table(true, InfoTablePosition::LEFT), catalog, {}).changed, "显示左侧表格失败");
    Require(workspace.Apply(Table(false, InfoTablePosition::LEFT), catalog, {}).changed, "隐藏表格失败");
    Require(
        workspace.State().traces.back().markerInfoLocation == MarkerInfoLocation::TABLE, "隐藏表格不能将字段搬回图窗");
    auto copy = workspace.Snapshot();
    copy.infoTable.visible = true;
    copy.traces.back().markerInfoLocation = MarkerInfoLocation::DIAGRAM;
    Require(!workspace.State().infoTable.visible &&
                workspace.State().traces.back().markerInfoLocation == MarkerInfoLocation::TABLE,
        "外部快照不得改变权威显示设置");
}

void WindowScopeAndRearrangement()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok && workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok,
        "拆分三个图窗失败");
    auto hide = Change(DisplayChangeKind::MARKER_INFO, "");
    hide.diagram = 2U;
    hide.enabled = false;
    const auto selected = workspace.State().selectedDiagram;
    Require(workspace.Apply(hide, catalog, {}).changed && workspace.State().diagrams[0U].markerInfoVisible &&
                !workspace.State().diagrams[1U].markerInfoVisible && workspace.State().diagrams[2U].markerInfoVisible &&
                workspace.State().selectedDiagram == selected,
        "显式图窗字段开关不得改变其他图窗或选择项");
    hide.diagram = 4U;
    const auto revision = workspace.State().revision;
    Require(
        !workspace.Apply(hide, catalog, {}).ok && workspace.State().revision == revision, "不存在的图窗必须原子拒绝");
    Require(workspace.Apply(Change(DisplayChangeKind::SELECT, "Trc2"), catalog, {}).ok &&
                workspace.Apply(Change(DisplayChangeKind::OVERLAY), catalog, {}).ok &&
                !workspace.State().diagrams.front().markerInfoVisible,
        "Overlay 必须保留原活动图窗的字段开关");
    Require(workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok, "再次拆分失败");
    for (const auto &diagram : workspace.State().diagrams) {
        Require(!diagram.markerInfoVisible, "Split 必须将原所属图窗的字段开关传到新图窗");
    }
}

void PlacementSurvivesCloneCatalogAndMarkerChanges()
{
    DisplayWorkspace workspace;
    std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2")};
    Require(workspace.Reconcile(catalog).ok, "初始化复制目录失败");
    auto move = Change(DisplayChangeKind::MARKER_INFO_LOCATION);
    move.markerInfoLocation = MarkerInfoLocation::TABLE;
    Require(workspace.Apply(move, catalog, {}).ok && workspace.ClonePresentation("Trc1", "Trc2", catalog).ok,
        "复制字段归属失败");
    Require(workspace.State().traces.back().markerInfoLocation == MarkerInfoLocation::TABLE,
        "Trace Config 复制必须复制字段归属");
    move.name = "Trc2";
    move.markerInfoLocation = MarkerInfoLocation::DIAGRAM;
    Require(workspace.Apply(move, catalog, {}).ok &&
                workspace.State().traces.front().markerInfoLocation == MarkerInfoLocation::TABLE,
        "复制后字段归属必须独立");
    auto marker = Change(DisplayChangeKind::MARKER);
    marker.marker.action = MarkerAction::ADD;
    Require(workspace.Apply(marker, catalog, DisplayTest::Frame()).ok, "新增标记失败");
    marker.marker.action = MarkerAction::ALL_OFF;
    Require(workspace.Apply(marker, catalog, {}).ok &&
                workspace.State().traces.front().markerInfoLocation == MarkerInfoLocation::TABLE,
        "标记全部关闭后必须保留信息字段归属");
    catalog.push_back(Measurement("Trc3"));
    Require(workspace.Reconcile(catalog).ok &&
                workspace.State().traces.front().markerInfoLocation == MarkerInfoLocation::TABLE &&
                workspace.State().traces.back().markerInfoLocation == MarkerInfoLocation::DIAGRAM,
        "协调目录必须保留旧字段归属并为新迹线提供独立默认值");
}

void AnnotationPatchAndAtomicRejection()
{
    DisplayWorkspace workspace;
    auto change = Change(DisplayChangeKind::DISPLAY_ANNOTATIONS, "");
    change.annotations.hasActiveTraceOnly = true;
    change.annotations.hasStimulusInfoVisible = true;
    change.annotations.hasFontScale = true;
    change.annotations.settings = {true, false, 137.5};
    Require(workspace.Apply(change, {}, {}).changed && workspace.State().annotations.fontScale == 137.5,
        "无迹线时也能保存全局注释，字体比例不得无依据限制为整数");
    const auto revision = workspace.State().revision;
    Require(
        workspace.Apply(change, {}, {}).ok && workspace.State().revision == revision, "重复设置全局注释不得增加版本");
    for (const double invalid : {79.9, 170.1, std::numeric_limits<double>::quiet_NaN()}) {
        change.annotations.settings = {false, true, invalid};
        Require(!workspace.Apply(change, {}, {}).ok && workspace.State().revision == revision &&
                    workspace.State().annotations.activeTraceOnly && !workspace.State().annotations.stimulusInfoVisible,
            "非法字体不能部分提交其他注释开关");
    }
    auto table = Table(true, static_cast<InfoTablePosition>(99));
    Require(
        !workspace.Apply(table, {}, {}).ok && !workspace.State().infoTable.visible, "非法表格位置不能部分提交显隐开关");
    auto empty = Change(DisplayChangeKind::INFO_TABLE, "");
    Require(!workspace.Apply(empty, {}, {}).ok, "空表格修改必须拒绝");
    auto exhaustedState = workspace.Snapshot();
    exhaustedState.revision = std::numeric_limits<std::uint64_t>::max();
    DisplayWorkspace exhausted(exhaustedState);
    Require(!exhausted.Apply(Table(true), {}, {}).ok && !exhausted.State().infoTable.visible,
        "显示版本耗尽不得提交信息表格设置");
}

void MarkerSelectionActivatesOwnerAtomically()
{
    DisplayWorkspace workspace;
    const std::vector<VnaMeasurement::Measurement> catalog{Measurement(), Measurement("Trc2"), Measurement("Trc3")};
    Require(workspace.Reconcile(catalog).ok, "初始化标记导航目录失败");
    auto select = Change(DisplayChangeKind::MARKER, "Trc2");
    select.marker.action = MarkerAction::SELECT;
    select.marker.id = 5U;
    const auto revision = workspace.State().revision;
    Require(workspace.Apply(select, catalog, DisplayTest::Frame()).changed &&
                workspace.State().selectedMeasurement == "Trc2" &&
                workspace.State().diagrams.front().primaryMeasurement == "Trc2" &&
                workspace.State().traces[1U].markers.Selected() == 5U && workspace.State().revision == revision + 1U,
        "选择别的迹线标记必须在一次显示提交中激活所属迹线与标记");
    const auto selectedRevision = workspace.State().revision;
    Require(workspace.Apply(select, catalog, DisplayTest::Frame()).ok && workspace.State().revision == selectedRevision,
        "重复选择同一活动标记不得增加显示版本");
    Require(workspace.Apply(Change(DisplayChangeKind::SPLIT), catalog, {}).ok, "拆分标记导航图窗失败");
    select.name = "Trc3";
    select.marker.id = 2U;
    Require(workspace.Apply(select, catalog, DisplayTest::Frame()).changed &&
                workspace.State().selectedMeasurement == "Trc3" && workspace.State().selectedDiagram == 3U &&
                workspace.State().diagrams[2U].primaryMeasurement == "Trc3",
        "选择另一图窗的标记必须激活所属图窗");
    const auto beforeInvalid = workspace.Snapshot();
    select.name = "Trc2";
    select.marker.id = MAX_MARKERS + 1U;
    Require(!workspace.Apply(select, catalog, DisplayTest::Frame()).ok &&
                workspace.State().revision == beforeInvalid.revision &&
                workspace.State().selectedMeasurement == "Trc3" && workspace.State().selectedDiagram == 3U &&
                workspace.State().traces[1U].markers.SameSettings(beforeInvalid.traces[1U].markers),
        "非法标记编号不得部分改变标记选择或显示版本");
    select.marker.id = 6U;
    select.diagram = MAX_DISPLAY_DIAGRAMS + 1U;
    Require(!workspace.Apply(select, catalog, DisplayTest::Frame()).ok &&
                workspace.State().revision == beforeInvalid.revision &&
                workspace.State().selectedMeasurement == "Trc3" && workspace.State().selectedDiagram == 3U &&
                workspace.State().traces[1U].markers.SameSettings(beforeInvalid.traces[1U].markers),
        "标记已在候选中选中但所属图窗无效时必须丢弃整个候选");
    for (const auto action : {MarkerAction::UPDATE, MarkerAction::ADD, MarkerAction::ALL_OFF}) {
        auto change = Change(DisplayChangeKind::MARKER, "Trc1");
        change.marker.action = action;
        if (action == MarkerAction::UPDATE) {
            change.marker.hasEnabled = true;
            change.marker.enabled = true;
        }
        Require(workspace.Apply(change, catalog, DisplayTest::Frame()).ok &&
                    workspace.State().selectedMeasurement == "Trc3" && workspace.State().selectedDiagram == 3U,
            "标记更新、新增与全部关闭不得隐式导航到其所属迹线");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        IndependentContainerAndFieldAssignment();
        WindowScopeAndRearrangement();
        PlacementSurvivesCloneCatalogAndMarkerChanges();
        AnnotationPatchAndAtomicRejection();
        MarkerSelectionActivatesOwnerAtomically();
        std::cout << "5 个信息表格与注释领域验收用例通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
