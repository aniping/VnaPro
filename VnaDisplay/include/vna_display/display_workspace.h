/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_DISPLAY_WORKSPACE_H
#define VNAPRO_VNA_DISPLAY_DISPLAY_WORKSPACE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vna_core/measurement_frame.h"
#include "vna_display/marker_collection.h"
#include "vna_measurement/measurement.h"

namespace VnaDisplay {

// 迹线容量是仪器目录的资源预算，与物理端口数量无关。
constexpr std::size_t MAX_DISPLAY_TRACES = 128U;
// 图窗独立于坐标格式；允许 Split All 为每条迹线创建一个图窗。
constexpr std::size_t MAX_DISPLAY_DIAGRAMS = MAX_DISPLAY_TRACES;

enum class DisplayFormat {
    DB,
    MAGNITUDE,
    PHASE,
    SWR,
    UNWRAPPED_PHASE,
    LOG_MAGNITUDE,
    REAL,
    IMAG,
    REALIMAG,
    DELAY,
    DELAY_DERIVATION,
    POLAR,
    SMITH,
    INV_SMITH
};

enum class LayoutMode {
    SINGLE,
    GRID,
    MAXIMIZED
};

struct DisplayScale final {
    double min = -90.0;
    double max = 10.0;
    double referencePosition = 9.0;
};

struct TracePresentation final {
    std::string name;
    DisplayFormat format = DisplayFormat::DB;
    DisplayFormat cartesianFormat = DisplayFormat::DB;
    DisplayScale scale;
    double smithRadius = 1.0;
    bool visible = true;
    MarkerCollection markers;
};

struct DisplayDiagram final {
    unsigned number = 1U;
    std::vector<std::string> measurementNames;
    // 每个图窗保留自己的活动迹线，控制网格并在绘制时置顶。
    std::string primaryMeasurement;
    // 显示槽身份与 Trc 名称独立；删除成员不得压缩其他槽号。
    std::vector<unsigned> traceSlots = {};
};

struct DisplayState final {
    std::uint64_t revision = 1U;
    std::string selectedMeasurement;
    // 兼容协议的只读扁平目录，由 diagrams 派生，禁止作为第二份归属状态写入。
    std::vector<std::string> displayedNames;
    LayoutMode layout = LayoutMode::SINGLE;
    std::vector<TracePresentation> traces;
    std::vector<DisplayDiagram> diagrams;
    unsigned selectedDiagram = 1U;
};

struct FrameContext final {
    VnaCore::FramePtr frame;
    // 仅在本次同步 Apply 期间借用已提交刺激轴，不会保存在工作区或异步任务中。
    const std::vector<double> *frequencyHz = nullptr;
    std::uint64_t acquisitionRevision = 0U;
    std::uint64_t runId = 0U;
    bool current = false;
};

enum class DisplayChangeKind {
    SELECT,
    DISPLAYED,
    LAYOUT,
    FORMAT,
    SCALE,
    AUTOSCALE,
    SMITH_RADIUS,
    VISIBLE,
    MARKER
};

struct ScaleChange final {
    bool hasMin = false;
    bool hasMax = false;
    bool hasPerDivision = false;
    bool hasReference = false;
    bool hasReferencePosition = false;
    double min = 0.0;
    double max = 0.0;
    double perDivision = 0.0;
    double reference = 0.0;
    double referencePosition = 0.0;
};

struct DisplayChange final {
    DisplayChangeKind kind = DisplayChangeKind::SELECT;
    // 名称为空时使用执行时选中项；槽号是从 1 开始的全局显示槽。
    std::string name;
    bool hasSlot = false;
    unsigned slot = 0U;
    bool hasDisplayedNames = false;
    std::vector<std::string> displayedNames;
    bool enabled = true;
    LayoutMode layout = LayoutMode::SINGLE;
    DisplayFormat format = DisplayFormat::DB;
    ScaleChange scale;
    MarkerChange marker;
    bool checkAcquisitionRevision = false;
    std::uint64_t expectedAcquisitionRevision = 0U;
    double smithRadius = 1.0;
    bool visible = true;
};

struct DisplayResult final {
    bool ok = false;
    bool changed = false;
    std::string error;
};

// 格式名称属于显示合同，各协议仅负责映射自己的标准命令词。
const char *DisplayFormatName(DisplayFormat format) noexcept;
bool ParseDisplayFormat(const std::string &value, DisplayFormat &format) noexcept;
const char *LayoutModeName(LayoutMode layout) noexcept;
bool ParseLayoutMode(const std::string &value, LayoutMode &layout) noexcept;

// 单线程显示域：由服务执行器持有，不创建线程、控制租约或采集计划。
class DisplayWorkspace final {
public:
    DisplayWorkspace() = default;
    // 测试可提供初始合法状态，检查版本耗尽等正常运行难以到达的条件。
    explicit DisplayWorkspace(DisplayState state);
    // 引用仅用于服务执行线程内即时读取，跨请求留存必须使用 Snapshot。
    const DisplayState &State() const noexcept;
    // 返回独立快照；客户端断开或浏览器刷新不改变已应用的状态。
    DisplayState Snapshot() const;
    // 只读派生图窗分组、原图号和主迹线；输出不改变状态或扩展 SCPI 窗口合同。
    static std::vector<DisplayDiagram> ArrangeDiagrams(const DisplayState &state);
    // 目录提交后保留同名设置，并原子移除无效项和不再适用的显示格式。
    DisplayResult Reconcile(const std::vector<VnaMeasurement::Measurement> &catalog);
    // 复制已协调源迹线的全部显示设置并选择目标；失败不改变任何状态，标记集合独立持有。
    DisplayResult ClonePresentation(const std::string &sourceName, const std::string &targetName,
        const std::vector<VnaMeasurement::Measurement> &catalog);
    // 按候选真实刺激轴协调标记；轴未改变时保留合法离散下标，否则按保存的频率重新吸附。
    DisplayResult ReconcileAxis(const std::vector<double> &frequencyHz, bool preserveSampleIndexes);
    // 核心成功提交后，仅绑定已验证下标的隐藏版本，不改变显示版本或可见设置。
    void BindAcquisitionRevision(std::uint64_t acquisitionRevision) noexcept;
    // 校验和计算均在候选副本中完成，失败不改变任何已应用设置。
    DisplayResult Apply(const DisplayChange &change, const std::vector<VnaMeasurement::Measurement> &catalog,
        const FrameContext &frame);

private:
    DisplayResult Commit(DisplayState candidate);
    static DisplayResult ApplyCandidate(DisplayState &candidate, const DisplayChange &change,
        const std::vector<VnaMeasurement::Measurement> &catalog, const FrameContext &frame);

    DisplayState state_;
};

}  // 命名空间 VnaDisplay

#endif
