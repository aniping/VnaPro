/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_DISPLAY_JSON_H
#define VNAPRO_WEB_DISPLAY_JSON_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vna_display/trace_projection.h"
#include "web/json.h"

namespace VnaWeb {

// 只转换协议表示；权威状态与所有显示计算都在 VnaDisplay 中执行。
class DisplayJson final {
public:
    DisplayJson() = delete;
    // 序列化同一次快照的目录、显示状态与只读图窗编排，不保存协议私有副本。
    static Json State(const VnaDisplay::DisplayState &state, const std::vector<VnaMeasurement::Measurement> &catalog);
    // 裁剪完整投影，保留独立 Marker 读数；特殊数值输出 null 和显式状态。
    static Json Projection(const VnaDisplay::ProjectedTrace &trace, VnaDisplay::DisplayFormat format,
        const std::vector<std::size_t> &indexes);
    // 严格校验 action 对应字段；领域范围、联动及计算由显示域执行。
    static VnaDisplay::DisplayChange Change(const Json &body);
    // 完整解析十进制 uint64，拒绝空值、非法字符和溢出。
    static std::uint64_t Revision(const Json &body, const std::string &field);

private:
    static void SettingsChange(VnaDisplay::DisplayChange &change, const Json &body, const std::string &action);
    static void WorkspaceChange(VnaDisplay::DisplayChange &change, const Json &body, const std::string &action);
    static void TraceChange(VnaDisplay::DisplayChange &change, const Json &body, const std::string &action);
    static Json Presentation(
        const VnaDisplay::TracePresentation &trace, const VnaMeasurement::Measurement &measurement);
    static Json Marker(const VnaDisplay::MarkerReadout &marker);
    static Json MarkerConfiguration(const VnaDisplay::MarkerSettings &marker);
    static void PointImpedance(Json &projection, const VnaDisplay::ProjectedTrace &trace,
        VnaDisplay::DisplayFormat format, const std::vector<std::size_t> &indexes);
    static void PointMarkerValues(Json &projection, const VnaDisplay::ProjectedTrace &trace,
        VnaDisplay::DisplayFormat format, const std::vector<std::size_t> &indexes);
};

}  // 命名空间 VnaWeb

#endif
