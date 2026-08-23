#ifndef VNA_HARDWARE_VNA_SWEEP_PLAN_H
#define VNA_HARDWARE_VNA_SWEEP_PLAN_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "VnaError.h"

namespace vna {

using PointId = std::uint32_t;
using ExcitationId = std::uint16_t;
using PortId = std::uint16_t;

// WaveKind 使用仪器端口参考方向，不表示显示层中的 S 参数。
enum class WaveKind : std::uint8_t { Incident, Outgoing };
// 外部触发的电气细节由具体驱动能力和设备配置决定。
enum class TriggerMode : std::uint8_t { Internal, External };
enum class ValidationSeverity : std::uint8_t { Error, Warning };

// 波键决定波块的 waveIndex；顺序在同一 ExcitationId 上必须稳定。
struct VnaWaveKey {
    WaveKind kind{WaveKind::Incident};
    PortId port{0};
};

// 相位单位为度，功率单位为 dBm，端口编号从 1 开始。
struct VnaSourcePlan {
    PortId port{0};
    double powerDbm{0.0};
    double phaseDegrees{0.0};
};

struct VnaExcitationPlan {
    // 同一 excitationId 在不同点上必须保持 requiredWaves 完全一致。
    ExcitationId excitationId{0};
    std::vector<VnaSourcePlan> sources;
    std::vector<VnaWaveKey> requiredWaves;
};

struct VnaPointPlan {
    // pointId 是调用方稳定身份，不要求等于 points 中的数组下标。
    PointId pointId{0};
    double frequencyHz{0.0};
    double ifBandwidthHz{0.0};
    std::chrono::nanoseconds dwellTime{0};
    std::vector<VnaExcitationPlan> excitations;
};

struct TriggerConfiguration {
    TriggerMode mode{TriggerMode::Internal};
};

struct SweepPolicy {
    // V1 ABI 不能返回量化后的实际点表，因此即使允许量化，报告量化也会失败关闭。
    bool allowQuantization{false};
    bool allowMultiplePhysicalSweeps{false};
    bool requirePhaseCoherence{true};
    bool requireAtomicConfiguration{true};
};

struct VnaSweepPlan {
    // points 的数组顺序就是采集顺序，驱动不得静默重排。
    std::vector<VnaPointPlan> points;
    TriggerConfiguration trigger;
    SweepPolicy policy;
};

struct ValidationIssue {
    // path 使用 points[i].excitations[j] 形式，供界面精确定位输入字段。
    ValidationSeverity severity{ValidationSeverity::Error};
    std::string path;
    std::string description;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
};

struct VnaWaveLayout {
    // layoutId 由驱动准备阶段分配，用于解释后续波块的波索引。
    LayoutId layoutId{0};
    ExcitationId excitationId{0};
    std::vector<VnaWaveKey> waveKeys;
};

enum class ExecutionMode : std::uint8_t { Native, Compiled };

struct ExecutionReport {
    // Native 表示设备直接接受计划，Compiled 表示驱动进行了等价的内部编排。
    ExecutionMode executionMode{ExecutionMode::Native};
    std::uint32_t physicalSweepCount{0};
    std::size_t waveBlockPointCapacity{0};
    std::size_t estimatedBytesPerPoint{0};
    bool quantized{false};
    bool atomic{false};
    bool phaseCoherent{false};
    // warnings 不会使 PreparedSweep 失效，但调用方应明确展示或记录。
    std::vector<ValidationIssue> warnings;
};

struct PreparedSweep {
    // configurationId 是一次准备结果的代次，旧代次不能再次启动。
    ConfigurationId configurationId{0};
    VnaSweepPlan effectivePlan;
    std::vector<VnaWaveLayout> layouts;
    ExecutionReport report;
};

bool isValid(const ValidationReport &report);

}  // namespace vna

#endif
