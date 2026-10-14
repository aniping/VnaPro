/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/diagram_layout.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
using namespace VnaDisplay;

void Require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(double actual, double expected)
{
    Require(std::abs(actual - expected) < 1e-12, "区域几何与独立期望不一致");
}

DiagramRegion Region(const DiagramLayout &layout, unsigned number)
{
    const auto regions = layout.Regions();
    const auto found = std::find_if(regions.begin(), regions.end(), [number](const DiagramRegion &region) {
        return region.number == number;
    });
    Require(found != regions.end(), "图窗区域缺失");
    return *found;
}

void VerifyPartition(const DiagramLayout &layout, std::size_t count)
{
    const auto regions = layout.Regions();
    Require(regions.size() == count, "区域数量错误");
    double area = 0.0;
    for (std::size_t index = 0U; index < regions.size(); ++index) {
        const auto &region = regions[index];
        Require(region.left >= 0.0 && region.top >= 0.0 && region.width > 0.0 && region.height > 0.0 &&
                    region.left + region.width <= 1.0 + 1e-12 && region.top + region.height <= 1.0 + 1e-12,
            "区域越界或尺寸无效");
        area += region.width * region.height;
        for (std::size_t other = index + 1U; other < regions.size(); ++other) {
            const auto &right = regions[other];
            const auto overlapWidth =
                std::min(region.left + region.width, right.left + right.width) - std::max(region.left, right.left);
            const auto overlapHeight =
                std::min(region.top + region.height, right.top + right.height) - std::max(region.top, right.top);
            Require(region.number != right.number && (overlapWidth <= 1e-12 || overlapHeight <= 1e-12),
                "图窗区域重叠或编号重复");
        }
    }
    Near(area, 1.0);
}

void TestFourRelativeDirections()
{
    for (auto position :
        {TraceDropPosition::ABOVE, TraceDropPosition::BELOW, TraceDropPosition::LEFT, TraceDropPosition::RIGHT}) {
        DiagramLayout layout;
        layout.Initialize({1U, 2U}, 2U, 1U);
        const auto initial = layout;
        const auto unchanged = Region(layout, 2U);
        Require(layout.Split(1U, 3U, position), "目标图窗分割失败");
        VerifyPartition(layout, 3U);
        const auto old = Region(layout, 1U);
        const auto added = Region(layout, 3U);
        const bool vertical = position == TraceDropPosition::LEFT || position == TraceDropPosition::RIGHT;
        Near(added.width, vertical ? 0.25 : 0.5);
        Near(added.height, vertical ? 1.0 : 0.5);
        Near(added.left, position == TraceDropPosition::RIGHT ? 0.25 : 0.0);
        Near(added.top, position == TraceDropPosition::BELOW ? 0.5 : 0.0);
        Near(old.width * old.height, 0.25);
        Near(Region(layout, 2U).left, unchanged.left);
        Near(Region(layout, 2U).height, unchanged.height);
        layout.Remove(3U);
        Require(layout.SameSettings(initial), "删除分割图窗必须合并兄弟并恢复原结构");
    }
}

void TestGridConversionAndRenumber()
{
    for (unsigned count = 1U; count <= 128U; ++count) {
        std::vector<unsigned> numbers;
        for (unsigned number = 1U; number <= count; ++number) {
            numbers.push_back(number);
        }
        DiagramLayout layout;
        const unsigned columns = std::min(2U, count);
        layout.Initialize(numbers, columns, (count + columns - 1U) / columns);
        VerifyPartition(layout, count);
    }
    DiagramLayout matrix;
    std::vector<unsigned> numbers;
    for (unsigned number = 1U; number <= 16U; ++number) {
        numbers.push_back(number);
    }
    matrix.Initialize(numbers, 4U, 4U);
    for (unsigned number = 1U; number <= 16U; ++number) {
        const auto region = Region(matrix, number);
        Near(region.left, static_cast<double>((number - 1U) % 4U) / 4.0);
        Near(region.top, static_cast<double>((number - 1U) / 4U) / 4.0);
        Near(region.width, 0.25);
        Near(region.height, 0.25);
    }
    VerifyPartition(matrix, 16U);
    matrix.Renumber({{1U, 2U}, {2U, 1U}});
    Near(Region(matrix, 2U).left, 0.0);
    Near(Region(matrix, 1U).left, 0.25);
    const auto previous = matrix;
    Require(!matrix.Split(127U, 128U, TraceDropPosition::LEFT) && !matrix.Split(1U, 2U, TraceDropPosition::LEFT) &&
                !matrix.Split(1U, 128U, TraceDropPosition::CENTER) && matrix.SameSettings(previous),
        "非法目标、重复编号及中心意图不能改变分割树");
    matrix.Clear();
    Require(matrix.Empty() && matrix.Regions().empty(), "恢复自动布局应清空自定义结构");
}
}  // 匿名命名空间

int main()
{
    try {
        TestFourRelativeDirections();
        TestGridConversionAndRenumber();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
