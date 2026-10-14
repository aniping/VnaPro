/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_DIAGRAM_LAYOUT_H
#define VNAPRO_DIAGRAM_LAYOUT_H

#include <string>
#include <utility>
#include <vector>

namespace VnaDisplay {

enum class TraceDropPosition {
    CENTER,
    ABOVE,
    BELOW,
    LEFT,
    RIGHT
};
const char *TraceDropPositionName(TraceDropPosition position) noexcept;
bool ParseTraceDropPosition(const std::string &value, TraceDropPosition &position) noexcept;

struct DiagramRegion final {
    unsigned number = 0U;
    double left = 0.0;
    double top = 0.0;
    double width = 1.0;
    double height = 1.0;
};

// 值对象保存仪器分割结构；客户端只读取派生区域，不复制或编辑布局树。
class DiagramLayout final {
public:
    // 空结构表示沿用自动网格，而不是没有图窗。
    bool Empty() const noexcept;
    // 恢复自动网格；不修改图窗目录和迹线归属。
    void Clear() noexcept;
    // 图号唯一且按行排列，行列尺寸由调用者校验；末尾空槽交给兄弟图窗。
    void Initialize(const std::vector<unsigned> &numbers, unsigned columns, unsigned rows);
    // 二分目标叶子并放置未使用图号；只接受四边，返回false时结构不变。
    bool Split(unsigned target, unsigned added, TraceDropPosition position);
    // 删除图号、合并兄弟并紧缩孤儿节点；不存在的图号不改变结构。
    void Remove(unsigned number);
    // 同步应用编号映射；调用者保证映射后的图号非零且唯一。
    void Renumber(const std::vector<std::pair<unsigned, unsigned>> &mapping);
    // 返回0～1归一化区域的独立副本；自动网格返回空列表。
    std::vector<DiagramRegion> Regions() const;
    // 比较图号、分割结构和比例，供候选提交判断是否发生变化。
    bool SameSettings(const DiagramLayout &other) const noexcept;

private:
    struct Node final {
        unsigned diagram = 0U;
        unsigned first = 0U;
        unsigned second = 0U;
        bool vertical = false;
        double ratio = 0.5;
    };

    unsigned AppendGrid(const std::vector<unsigned> &numbers, unsigned columns, unsigned row, unsigned rows,
        unsigned column, unsigned count);
    bool RemoveFrom(unsigned index, unsigned number);
    void AppendRegions(unsigned index, const DiagramRegion &region, std::vector<DiagramRegion> &regions) const;
    unsigned CopyReachable(unsigned index, std::vector<Node> &nodes) const;
    std::vector<Node> nodes_;
};

}  // 命名空间 VnaDisplay
#endif
