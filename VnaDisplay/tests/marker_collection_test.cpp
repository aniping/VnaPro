/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "display_test_support.h"
#include "vna_display/trace_projection.h"

using namespace DisplayTest;
using namespace VnaDisplay;

namespace {

void NumberedMarkersAreIndependentAndBounded()
{
    MarkerCollection collection;
    std::string error;
    const auto context = Frame();
    MarkerChange add;
    add.action = MarkerAction::ADD;
    for (unsigned id = 1U; id <= MAX_MARKERS; ++id) {
        Require(collection.Apply(add, context, error), "Add Marker 必须依次开启十个独立标记");
        Require(collection.Selected() == id && collection.Find(id)->enabled, "活动编号必须对应新标记");
    }
    const auto full = collection;
    Require(!collection.Apply(add, context, error) && collection.SameSettings(full),
        "第十一个普通标记必须原子拒绝，不能覆盖已有位置");
    MarkerChange move;
    move.id = 2U;
    move.hasSampleIndex = true;
    move.sampleIndex = 0U;
    move.checkAcquisitionRevision = true;
    move.expectedAcquisitionRevision = context.acquisitionRevision;
    Require(collection.Apply(move, context, error) && collection.Selected() == 2U, "只移动并激活 M2");
    Require(collection.Find(2U)->sampleIndex == 0U && collection.Find(1U)->frequencyHz == 2e6 &&
                collection.Find(10U)->frequencyHz == 2e6 && !collection.Find(1U)->hasSampleIndex,
        "M2 移动不能修改 M1 或 M10");
    const auto before = collection;
    move.id = MAX_MARKERS + 1U;
    Require(!collection.Apply(move, context, error) && collection.SameSettings(before), "非法编号不改变任何状态");
    move.id = 2U;
    --move.expectedAcquisitionRevision;
    Require(!collection.Apply(move, context, error) && collection.SameSettings(before), "旧采集版本不改变标记");
}

void SwitchingOffRetainsSettingsAndAddUsesFreeNumber()
{
    MarkerCollection collection;
    const auto context = Frame();
    std::string error;
    MarkerChange select;
    select.action = MarkerAction::SELECT;
    select.id = 7U;
    Require(collection.Apply(select, context, error), "编号按钮开启并选择对应标记");
    MarkerChange move;
    move.hasFrequency = true;
    move.frequencyHz = 1e6;
    Require(collection.Apply(move, context, error), "活动标记保存独立频率");
    MarkerChange allOff;
    allOff.action = MarkerAction::ALL_OFF;
    Require(collection.Apply(allOff, context, error), "All Off 关闭所有普通标记");
    Require(collection.Selected() == 7U && !collection.Find(7U)->enabled && collection.Find(7U)->frequencyHz == 1e6,
        "关闭必须保留选择和位置属性");
    MarkerChange add;
    add.action = MarkerAction::ADD;
    Require(collection.Apply(add, context, error) && collection.Selected() == 1U,
        "新增使用最小空闲编号，不按活动编号一直累加");
    Require(collection.Apply(select, context, error) && collection.Find(7U)->frequencyHz == 1e6,
        "重新开启恢复已保存的 M7 位置");
    const auto before = collection;
    select.hasFrequency = true;
    select.frequencyHz = 3e6;
    Require(!collection.Apply(select, context, error) && collection.SameSettings(before),
        "选择与位置更新不能混合成部分生效的命令");
}

void EveryMarkerReadsTheCompleteFrameAndReconcilesAxes()
{
    TracePresentation presentation;
    auto context = Frame(10001U);
    std::string error;
    MarkerChange move;
    move.hasEnabled = true;
    move.enabled = true;
    move.hasSampleIndex = true;
    move.sampleIndex = 7777U;
    move.id = 10U;
    Require(presentation.markers.Apply(move, context, error), "M10 可以选择被传输抽点省略的原始下标");
    const auto projected = TraceProjection::Calculate(presentation, Trace(std::vector<double>(10001U, 0.5)),
        context.frame->plan.frequencyHz, context.acquisitionRevision);
    Require(projected.markers.size() == MAX_MARKERS && projected.activeMarkerId == 10U &&
                projected.markers[9U].id == 10U && projected.markers[9U].sampleIndex == 7777U &&
                projected.marker.sampleIndex == projected.markers[9U].sampleIndex,
        "活动别名和十个独立读数都基于同一完整帧");
    Require(!projected.markers.front().available, "未开启的 M1 不应借用 M10 的数据");
    presentation.markers.BindAcquisitionRevision(8U);
    Require(presentation.markers.Find(10U)->acquisitionRevision == 8U, "所有标记绑定采集版本");
    presentation.markers.ReconcileAxis({1e6, 2e6, 3e6}, false);
    Require(presentation.markers.Find(10U)->frequencyHz == 3e6 && presentation.markers.Find(10U)->hasSampleIndex &&
                presentation.markers.Find(10U)->sampleIndex == 2U,
        "频轴改变后离散标记按保存频率重协调到新轴，不复用旧原始下标");
}

}  // 匿名命名空间

int main()
{
    try {
        NumberedMarkersAreIndependentAndBounded();
        SwitchingOffRetainsSettingsAndAddUsesFreeNumber();
        EveryMarkerReadsTheCompleteFrameAndReconcilesAxes();
        std::cout << "PASS 十个独立标记、编号复用、完整帧与原子失败\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
