/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/diagram_collection.h"

#include <algorithm>
#include <utility>

namespace VnaDisplay {
namespace {
DisplayResult Result(const std::string &error = {})
{
    DisplayResult result;
    result.ok = error.empty();
    result.error = error;
    return result;
}

bool Contains(const std::vector<std::string> &names, const std::string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

void RemoveMember(DisplayDiagram &diagram, std::size_t index)
{
    diagram.measurementNames.erase(diagram.measurementNames.begin() + static_cast<std::ptrdiff_t>(index));
    diagram.traceSlots.erase(diagram.traceSlots.begin() + static_cast<std::ptrdiff_t>(index));
}

void InitializeSlots(DisplayDiagram &diagram)
{
    if (diagram.traceSlots.size() != diagram.measurementNames.size()) {
        diagram.traceSlots.clear();
        for (std::size_t index = 0U; index < diagram.measurementNames.size(); ++index) {
            diagram.traceSlots.push_back(static_cast<unsigned>(index + 1U));
        }
    }
}

}  // 匿名命名空间

DisplayDiagram *DiagramCollection::Find(DisplayState &state, unsigned number) noexcept
{
    const auto item = std::find_if(state.diagrams.begin(), state.diagrams.end(), [number](const DisplayDiagram &value) {
        return value.number == number;
    });
    return item == state.diagrams.end() ? nullptr : &*item;
}

const DisplayDiagram *DiagramCollection::Find(const DisplayState &state, unsigned number) noexcept
{
    const auto item = std::find_if(state.diagrams.begin(), state.diagrams.end(), [number](const DisplayDiagram &value) {
        return value.number == number;
    });
    return item == state.diagrams.end() ? nullptr : &*item;
}

const std::string *DiagramCollection::NameAtSlot(const DisplayDiagram &diagram, unsigned slot) noexcept
{
    const auto found = std::find(diagram.traceSlots.begin(), diagram.traceSlots.end(), slot);
    const auto index = static_cast<std::size_t>(found - diagram.traceSlots.begin());
    return found == diagram.traceSlots.end() || index >= diagram.measurementNames.size()
               ? nullptr
               : &diagram.measurementNames[index];
}

void DiagramCollection::Refresh(DisplayState &state)
{
    if (state.diagrams.empty()) {
        state.diagrams.push_back({1U, {}, {}});
    }
    state.displayedNames.clear();
    for (auto &diagram : state.diagrams) {
        InitializeSlots(diagram);
        for (const auto &name : diagram.measurementNames) {
            if (!Contains(state.displayedNames, name)) {
                state.displayedNames.push_back(name);
            }
        }
        if (!Contains(diagram.measurementNames, diagram.primaryMeasurement)) {
            diagram.primaryMeasurement = diagram.measurementNames.empty() ? "" : diagram.measurementNames.front();
        }
    }
    if (Find(state, state.selectedDiagram) == nullptr) {
        state.selectedDiagram = state.diagrams.front().number;
    }
}

void DiagramCollection::Reconcile(DisplayState &state, const std::vector<std::string> &catalog, bool initial)
{
    if (state.diagrams.empty()) {
        state.diagrams.push_back({1U, initial ? catalog : state.displayedNames, state.selectedMeasurement});
    }
    for (auto &diagram : state.diagrams) {
        auto &names = diagram.measurementNames;
        const bool occupied = !names.empty();
        InitializeSlots(diagram);
        for (std::size_t index = names.size(); index-- > 0U;) {
            if (!Contains(catalog, names[index])) {
                RemoveMember(diagram, index);
            }
        }
        // 删除最后一条所属迹线会删除图窗；主动创建的空窗口则继续保留。
        if (occupied && names.empty() && state.diagrams.size() > 1U) {
            diagram.number = 0U;
        }
    }
    const bool removed = std::any_of(state.diagrams.begin(), state.diagrams.end(), [](const DisplayDiagram &diagram) {
        return diagram.number == 0U;
    });
    state.diagrams.erase(std::remove_if(state.diagrams.begin(), state.diagrams.end(),
                             [](const DisplayDiagram &diagram) {
                                 return diagram.number == 0U;
                             }),
        state.diagrams.end());
    if (removed) {
        const unsigned previousSelection = state.selectedDiagram;
        unsigned nextSelection = 0U;
        unsigned next = 1U;
        for (auto &diagram : state.diagrams) {
            if (previousSelection == diagram.number) {
                nextSelection = next;
            }
            diagram.number = next++;
        }
        state.selectedDiagram = nextSelection == 0U ? 1U : nextSelection;
    }
    Refresh(state);
}

DisplayResult DiagramCollection::Add(DisplayState &state, unsigned number)
{
    Refresh(state);
    if (state.diagrams.size() >= MAX_DISPLAY_DIAGRAMS) {
        return Result("Diagram capacity exhausted");
    }
    if (number == 0U) {
        number = 1U;
        for (const auto &diagram : state.diagrams) {
            number = std::max(number, diagram.number + 1U);
        }
    }
    if (number > MAX_DISPLAY_DIAGRAMS || Find(state, number) != nullptr) {
        return Result("Diagram number is invalid or already exists");
    }
    state.diagrams.push_back({number, {}, {}});
    state.selectedDiagram = number;
    state.layout = LayoutMode::GRID;
    return Result();
}

DisplayResult DiagramCollection::Remove(DisplayState &state, unsigned number, bool renumber)
{
    const auto *removed = Find(state, number);
    if (removed == nullptr || state.diagrams.size() <= 1U) {
        return Result("Cannot delete a missing or the last diagram");
    }
    const bool removedSelectedTrace = Contains(removed->measurementNames, state.selectedMeasurement);
    const unsigned previousSelection = state.selectedDiagram;
    state.diagrams.erase(std::remove_if(state.diagrams.begin(), state.diagrams.end(),
                             [number](const DisplayDiagram &diagram) {
                                 return diagram.number == number;
                             }),
        state.diagrams.end());
    if (renumber) {
        unsigned nextSelection = 0U;
        unsigned next = 1U;
        for (auto &diagram : state.diagrams) {
            if (previousSelection == diagram.number) {
                nextSelection = next;
            }
            diagram.number = next++;
        }
        state.selectedDiagram = nextSelection == 0U ? 1U : nextSelection;
    }
    Refresh(state);
    const auto *active = Find(state, state.selectedDiagram);
    if (removedSelectedTrace && !active->primaryMeasurement.empty()) {
        state.selectedMeasurement = active->primaryMeasurement;
    }
    return Result();
}

DisplayResult DiagramCollection::Select(DisplayState &state, unsigned number)
{
    const auto *diagram = Find(state, number);
    if (diagram == nullptr) {
        return Result("Diagram does not exist");
    }
    state.selectedDiagram = number;
    if (!diagram->primaryMeasurement.empty()) {
        state.selectedMeasurement = diagram->primaryMeasurement;
    }
    return Result();
}

DisplayResult DiagramCollection::Feed(DisplayState &state, const std::string &name, unsigned number, unsigned slot)
{
    number = number == 0U ? state.selectedDiagram : number;
    auto *diagram = Find(state, number);
    if (diagram == nullptr || slot > MAX_DISPLAY_TRACES) {
        return Result("Target diagram or display trace slot does not exist");
    }
    InitializeSlots(*diagram);
    auto &names = diagram->measurementNames;
    // 一条测量只归属一个图窗；移动不会复制测量或增加采集需求。
    for (auto &other : state.diagrams) {
        if (other.number != number) {
            InitializeSlots(other);
            for (std::size_t index = other.measurementNames.size(); index-- > 0U;) {
                if (other.measurementNames[index] == name) {
                    RemoveMember(other, index);
                }
            }
        }
    }
    if (slot == 0U) {
        const auto existing = std::find(names.begin(), names.end(), name);
        if (existing != names.end()) {
            slot = diagram->traceSlots[static_cast<std::size_t>(existing - names.begin())];
        } else {
            slot = 1U;
            while (
                std::find(diagram->traceSlots.begin(), diagram->traceSlots.end(), slot) != diagram->traceSlots.end()) {
                ++slot;
            }
        }
    }
    if (slot > MAX_DISPLAY_TRACES) {
        return Result("Displayed trace capacity exhausted");
    }
    for (std::size_t index = names.size(); index-- > 0U;) {
        if (names[index] == name && diagram->traceSlots[index] != slot) {
            RemoveMember(*diagram, index);
        }
    }
    const auto target = std::find(diagram->traceSlots.begin(), diagram->traceSlots.end(), slot);
    if (target == diagram->traceSlots.end()) {
        names.push_back(name);
        diagram->traceSlots.push_back(slot);
    } else {
        names[static_cast<std::size_t>(target - diagram->traceSlots.begin())] = name;
    }
    diagram->primaryMeasurement = name;
    state.selectedMeasurement = name;
    state.selectedDiagram = number;
    Refresh(state);
    return Result();
}

void DiagramCollection::Unfeed(DisplayState &state, const std::string &name, unsigned number)
{
    bool removed = false;
    for (auto &diagram : state.diagrams) {
        if (number == 0U || number == diagram.number) {
            InitializeSlots(diagram);
            for (std::size_t index = diagram.measurementNames.size(); index-- > 0U;) {
                if (diagram.measurementNames[index] == name) {
                    RemoveMember(diagram, index);
                    removed = true;
                }
            }
        }
    }
    Refresh(state);
    if (removed && state.selectedMeasurement == name && !Contains(state.displayedNames, name) &&
        !state.displayedNames.empty()) {
        for (auto &diagram : state.diagrams) {
            if (!diagram.primaryMeasurement.empty()) {
                Select(state, diagram.number);
                break;
            }
        }
    }
}

void DiagramCollection::Overlay(DisplayState &state)
{
    Refresh(state);
    const bool markerInfoVisible = Find(state, state.selectedDiagram)->markerInfoVisible;
    state.diagrams = {{1U, state.displayedNames, state.selectedMeasurement}};
    // 合并保留原活动图窗的字段开关，不因为重新编排而打开隐藏字段。
    state.diagrams.front().markerInfoVisible = markerInfoVisible;
    state.selectedDiagram = 1U;
    state.layout = LayoutMode::SINGLE;
    Refresh(state);
}

void DiagramCollection::Split(DisplayState &state)
{
    Refresh(state);
    const auto names = state.displayedNames;
    const auto previous = state.diagrams;
    state.diagrams.clear();
    unsigned number = 1U;
    for (const auto &name : names) {
        state.diagrams.push_back({number, {name}, name});
        // Split 将每条迹线原所属图窗的字段开关复制到新图窗。
        const auto source = std::find_if(previous.begin(), previous.end(), [&name](const DisplayDiagram &diagram) {
            return Contains(diagram.measurementNames, name);
        });
        if (source != previous.end()) {
            state.diagrams.back().markerInfoVisible = source->markerInfoVisible;
        }
        if (name == state.selectedMeasurement) {
            state.selectedDiagram = number;
        }
        ++number;
    }
    state.layout = LayoutMode::GRID;
    Refresh(state);
}

}  // 命名空间 VnaDisplay
