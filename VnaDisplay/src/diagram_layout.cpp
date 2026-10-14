/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/diagram_layout.h"

#include <algorithm>

namespace VnaDisplay {
const char *TraceDropPositionName(TraceDropPosition position) noexcept
{
    switch (position) {
        case TraceDropPosition::CENTER:
            return "center";
        case TraceDropPosition::ABOVE:
            return "above";
        case TraceDropPosition::BELOW:
            return "below";
        case TraceDropPosition::LEFT:
            return "left";
        case TraceDropPosition::RIGHT:
            return "right";
    }
    return nullptr;
}

bool ParseTraceDropPosition(const std::string &value, TraceDropPosition &position) noexcept
{
    for (auto candidate : {TraceDropPosition::CENTER, TraceDropPosition::ABOVE, TraceDropPosition::BELOW,
             TraceDropPosition::LEFT, TraceDropPosition::RIGHT}) {
        if (value == TraceDropPositionName(candidate)) {
            position = candidate;
            return true;
        }
    }
    return false;
}

bool DiagramLayout::Empty() const noexcept
{
    return nodes_.empty();
}

void DiagramLayout::Clear() noexcept
{
    nodes_.clear();
}

void DiagramLayout::Initialize(const std::vector<unsigned> &numbers, unsigned columns, unsigned rows)
{
    Clear();
    if (!numbers.empty() && columns != 0U && rows != 0U) {
        AppendGrid(numbers, columns, 0U, rows, 0U, columns);
        std::vector<Node> compact;
        CopyReachable(0U, compact);
        nodes_ = std::move(compact);
    }
}

unsigned DiagramLayout::AppendGrid(const std::vector<unsigned> &numbers, unsigned columns, unsigned row, unsigned rows,
    unsigned column, unsigned count)
{
    const unsigned index = static_cast<unsigned>(nodes_.size());
    nodes_.push_back({});
    if (count == 1U && rows == 1U) {
        const auto offset = row * columns + column;
        nodes_[index].diagram = offset < numbers.size() ? numbers[offset] : 0U;
        return index;
    }
    const bool vertical = count > 1U;
    const auto size = vertical ? count : rows;
    const auto half = size / 2U;
    const auto first = AppendGrid(numbers, columns, row, vertical ? rows : half, column, vertical ? half : count);
    const auto second = AppendGrid(numbers, columns, vertical ? row : row + half, vertical ? rows : rows - half,
        vertical ? column + half : column, vertical ? count - half : count);
    nodes_[index] = {0U, first, second, vertical, static_cast<double>(half) / size};
    // 最后一行的未使用网格槽交给兄弟图窗，正式分割区域始终覆盖全部工作区。
    if (nodes_[first].first == 0U && nodes_[first].diagram == 0U) {
        nodes_[index] = nodes_[second];
    } else if (nodes_[second].first == 0U && nodes_[second].diagram == 0U) {
        nodes_[index] = nodes_[first];
    }
    return index;
}

bool DiagramLayout::Split(unsigned target, unsigned added, TraceDropPosition position)
{
    if (target == 0U || added == 0U || position == TraceDropPosition::CENTER ||
        TraceDropPositionName(position) == nullptr ||
        std::any_of(nodes_.begin(), nodes_.end(), [added](const Node &node) {
            return node.diagram == added;
        })) {
        return false;
    }
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [target](const Node &node) {
        return node.diagram == target && node.first == 0U;
    });
    if (found == nodes_.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - nodes_.begin());
    const bool before = position == TraceDropPosition::LEFT || position == TraceDropPosition::ABOVE;
    const auto first = static_cast<unsigned>(nodes_.size());
    nodes_.push_back({before ? added : target});
    const auto second = static_cast<unsigned>(nodes_.size());
    nodes_.push_back({before ? target : added});
    nodes_[index] = {
        0U, first, second, position == TraceDropPosition::LEFT || position == TraceDropPosition::RIGHT, 0.5};
    return true;
}

bool DiagramLayout::RemoveFrom(unsigned index, unsigned number)
{
    const auto node = nodes_[index];
    if (node.first == 0U) {
        if (node.diagram != number) {
            return false;
        }
        nodes_[index] = {};
        return true;
    }
    const bool removed = RemoveFrom(node.first, number) || RemoveFrom(node.second, number);
    if (removed) {
        const auto empty = [this](unsigned child) {
            return nodes_[child].first == 0U && nodes_[child].diagram == 0U;
        };
        if (empty(node.first)) {
            nodes_[index] = nodes_[node.second];
        } else if (empty(node.second)) {
            nodes_[index] = nodes_[node.first];
        }
    }
    return removed;
}

unsigned DiagramLayout::CopyReachable(unsigned index, std::vector<Node> &nodes) const
{
    const auto node = nodes_[index];
    const auto copied = static_cast<unsigned>(nodes.size());
    nodes.push_back(node);
    if (node.first != 0U) {
        const auto first = CopyReachable(node.first, nodes);
        const auto second = CopyReachable(node.second, nodes);
        nodes[copied].first = first;
        nodes[copied].second = second;
    }
    return copied;
}

void DiagramLayout::Remove(unsigned number)
{
    if (number != 0U && !Empty() && RemoveFrom(0U, number)) {
        std::vector<Node> compact;
        CopyReachable(0U, compact);
        nodes_ = std::move(compact);
    }
}

void DiagramLayout::Renumber(const std::vector<std::pair<unsigned, unsigned>> &mapping)
{
    for (auto &node : nodes_) {
        const auto found =
            std::find_if(mapping.begin(), mapping.end(), [&node](const std::pair<unsigned, unsigned> &item) {
                return item.first == node.diagram;
            });
        if (node.diagram != 0U && found != mapping.end()) {
            node.diagram = found->second;
        }
    }
}

void DiagramLayout::AppendRegions(
    unsigned index, const DiagramRegion &region, std::vector<DiagramRegion> &regions) const
{
    const auto &node = nodes_[index];
    if (node.first == 0U) {
        if (node.diagram != 0U) {
            auto leaf = region;
            leaf.number = node.diagram;
            regions.push_back(leaf);
        }
        return;
    }
    auto first = region;
    auto second = region;
    if (node.vertical) {
        first.width *= node.ratio;
        second.left += first.width;
        second.width -= first.width;
    } else {
        first.height *= node.ratio;
        second.top += first.height;
        second.height -= first.height;
    }
    AppendRegions(node.first, first, regions);
    AppendRegions(node.second, second, regions);
}

std::vector<DiagramRegion> DiagramLayout::Regions() const
{
    std::vector<DiagramRegion> regions;
    if (!Empty()) {
        AppendRegions(0U, {}, regions);
    }
    return regions;
}

bool DiagramLayout::SameSettings(const DiagramLayout &other) const noexcept
{
    return nodes_.size() == other.nodes_.size() &&
           std::equal(nodes_.begin(), nodes_.end(), other.nodes_.begin(), [](const Node &left, const Node &right) {
               return left.diagram == right.diagram && left.first == right.first && left.second == right.second &&
                      left.vertical == right.vertical && left.ratio == right.ratio;
           });
}
}  // 命名空间 VnaDisplay
