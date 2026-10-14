/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_DIAGRAM_COLLECTION_H
#define VNAPRO_DIAGRAM_COLLECTION_H

#include <string>
#include <vector>

#include "vna_display/display_workspace.h"

namespace VnaDisplay {

// 图窗归属模块：格式不参与分组，协议只提交意图，不自行重排迹线。
class DiagramCollection final {
public:
    DiagramCollection() = delete;
    static DisplayDiagram *Find(DisplayState &state, unsigned number) noexcept;
    static const DisplayDiagram *Find(const DisplayState &state, unsigned number) noexcept;
    static const std::string *NameAtSlot(const DisplayDiagram &diagram, unsigned slot) noexcept;
    static void Refresh(DisplayState &state);
    static void Reconcile(DisplayState &state, const std::vector<std::string> &catalog, bool initial);
    static DisplayResult Add(DisplayState &state, unsigned number = 0U);
    static DisplayResult Remove(DisplayState &state, unsigned number, bool renumber);
    static DisplayResult Select(DisplayState &state, unsigned number);
    static DisplayResult Feed(DisplayState &state, const std::string &name, unsigned number, unsigned slot = 0U);
    // 相对分割、归属移动和空源图窗合并均在调用者的同一候选中完成。
    static DisplayResult Place(DisplayState &state, const std::string &name, unsigned target,
        TraceDropPosition position = TraceDropPosition::CENTER, bool newDiagram = false);
    static void Unfeed(DisplayState &state, const std::string &name, unsigned number = 0U);
    static void Overlay(DisplayState &state);
    static void Split(DisplayState &state);
};

}  // 命名空间 VnaDisplay
#endif
