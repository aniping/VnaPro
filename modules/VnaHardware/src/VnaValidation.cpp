#include "VnaValidation.h"

#include <cmath>
#include <map>
#include <set>
#include <string>

namespace vna {

bool isValid(const ValidationReport &report)
{
    // 报告允许同时携带警告和错误；只有 Error 会阻止计划进入 Prepare。
    for (const auto &issue : report.issues) {
        if (issue.severity == ValidationSeverity::Error) {
            return false;
        }
    }
    return true;
}

namespace detail {
    namespace {

        void addError(ValidationReport &report, std::string path, std::string description)
        {
            // 不在首个错误处短路，调用方一次即可得到全部可修复问题。
            report.issues.push_back(
                ValidationIssue{ValidationSeverity::Error, std::move(path), std::move(description)});
        }

        bool inside(double value, const NumericRange &range)
        {
            // 显式排除 NaN/无穷；仅做上下界比较会让 NaN 逃逸或产生平台差异。
            return std::isfinite(value) && (value >= range.minimum) && (value <= range.maximum);
        }

        bool sameWaveKeys(const std::vector<VnaWaveKey> &left, const std::vector<VnaWaveKey> &right)
        {
            if (left.size() != right.size()) {
                return false;
            }
            // 顺序就是数据块的 waveIndex 契约，不能按集合比较。
            for (std::size_t index = 0; index < left.size(); ++index) {
                if ((left[index].kind != right[index].kind) || (left[index].port != right[index].port)) {
                    return false;
                }
            }
            return true;
        }

        void validateSource(
            const VnaSourcePlan &source, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            if ((source.port == 0) || (source.port > capabilities.portCount)) {
                addError(report, path + ".port", "source port is outside the device port range");
            }
            if (!inside(source.powerDbm, capabilities.powerDbm)) {
                addError(report, path + ".powerDbm", "source power is outside device capabilities");
            }
            if (!std::isfinite(source.phaseDegrees)) {
                addError(report, path + ".phaseDegrees", "source phase must be finite");
            }
        }

        void validateWave(
            const VnaWaveKey &wave, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            if ((wave.port == 0) || (wave.port > capabilities.portCount)) {
                addError(report, path + ".port", "wave port is outside the device port range");
            }
        }

        bool simultaneousSourcesViolated(const VnaExcitationPlan &excitation, const VnaCapabilities &capabilities)
        {
            return !capabilities.simultaneousSources && (excitation.sources.size() > 1);
        }

        void validateExcitationSources(
            const VnaExcitationPlan &excitation, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            if (excitation.sources.empty()) {
                addError(report, path + ".sources", "an excitation requires at least one source");
            }
            if (simultaneousSourcesViolated(excitation, capabilities)) {
                addError(report, path + ".sources", "the device does not support simultaneous sources");
            }
            // 端口重复意味着同一物理源被赋予两组参数，必须在到达驱动前拒绝。
            std::set<PortId> sourcePorts;
            for (std::size_t index = 0; index < excitation.sources.size(); ++index) {
                validateSource(
                    excitation.sources[index], capabilities, path + ".sources[" + std::to_string(index) + "]", report);
                if (!sourcePorts.insert(excitation.sources[index].port).second) {
                    addError(report, path + ".sources", "source ports must be unique");
                }
            }
        }

        void validateExcitationWaves(
            const VnaExcitationPlan &excitation, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            if (excitation.requiredWaves.empty()) {
                addError(report, path + ".requiredWaves", "an excitation requires at least one wave");
            }
            // 波键以类型和端口联合唯一，避免两列结果具有相同语义却不同索引。
            std::set<std::pair<WaveKind, PortId>> waves;
            for (std::size_t index = 0; index < excitation.requiredWaves.size(); ++index) {
                const auto &wave = excitation.requiredWaves[index];
                validateWave(wave, capabilities, path + ".requiredWaves[" + std::to_string(index) + "]", report);
                if (!waves.insert({wave.kind, wave.port}).second) {
                    addError(report, path + ".requiredWaves", "required waves must be unique");
                }
            }
        }

        void validateExcitation(
            const VnaExcitationPlan &excitation, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            validateExcitationSources(excitation, capabilities, path, report);
            validateExcitationWaves(excitation, capabilities, path, report);
        }

        bool validExcitationCount(const VnaPointPlan &point, const VnaCapabilities &capabilities)
        {
            return !point.excitations.empty() && (point.excitations.size() <= capabilities.maximumExcitationsPerPoint);
        }

        void validatePointValues(
            const VnaPointPlan &point, const VnaCapabilities &capabilities, const std::string &path,
            ValidationReport &report)
        {
            if (!inside(point.frequencyHz, capabilities.frequencyHz)) {
                addError(report, path + ".frequencyHz", "frequency is outside device capabilities");
            }
            if (!inside(point.ifBandwidthHz, capabilities.ifBandwidthHz)) {
                addError(report, path + ".ifBandwidthHz", "IF bandwidth is outside device capabilities");
            }
            // 零驻留合法，负值没有硬件含义且在无符号 ABI 转换时会溢出。
            if (point.dwellTime.count() < 0) {
                addError(report, path + ".dwellTime", "dwell time cannot be negative");
            }
            if (!validExcitationCount(point, capabilities)) {
                addError(report, path + ".excitations", "excitation count is outside capabilities");
            }
        }

        bool validPointCount(const VnaSweepPlan &plan, const VnaCapabilities &capabilities)
        {
            return !plan.points.empty() && (plan.points.size() <= capabilities.maximumPoints);
        }

        void validateTrigger(
            const TriggerConfiguration &trigger, const VnaCapabilities &capabilities, ValidationReport &report)
        {
            // 触发模式属于整份计划，不依赖频点，因此只需在顶层验证一次。
            if ((trigger.mode == TriggerMode::Internal) && !capabilities.supportsInternalTrigger) {
                addError(report, "trigger", "internal trigger is not supported");
            }
            if ((trigger.mode == TriggerMode::External) && !capabilities.supportsExternalTrigger) {
                addError(report, "trigger", "external trigger is not supported");
            }
        }

        void validatePoint(
            const VnaPointPlan &point, const VnaCapabilities &capabilities, std::size_t pointIndex,
            std::map<ExcitationId, std::vector<VnaWaveKey>> &layouts, ValidationReport &report)
        {
            const std::string path = "points[" + std::to_string(pointIndex) + "]";
            validatePointValues(point, capabilities, path, report);
            // 点内保证 ID 唯一，layouts 则跨点保证同一 ID 始终对应同一波布局。
            std::set<ExcitationId> identifiers;
            for (std::size_t index = 0; index < point.excitations.size(); ++index) {
                const auto &excitation = point.excitations[index];
                const std::string excitationPath = path + ".excitations[" + std::to_string(index) + "]";
                validateExcitation(excitation, capabilities, excitationPath, report);
                if (!identifiers.insert(excitation.excitationId).second) {
                    addError(report, path + ".excitations", "excitation identifiers must be unique");
                }
                const auto existing = layouts.find(excitation.excitationId);
                if (existing == layouts.end()) {
                    layouts.emplace(excitation.excitationId, excitation.requiredWaves);
                } else if (!sameWaveKeys(existing->second, excitation.requiredWaves)) {
                    addError(report, excitationPath, "one excitation identifier must keep a stable wave layout");
                }
            }
        }

    }  // namespace

    ValidationReport validatePlan(const VnaSweepPlan &plan, const VnaCapabilities &capabilities)
    {
        ValidationReport report;
        // 空计划或超大计划没有继续遍历的价值，也避免后续路径生成处理异常规模输入。
        if (!validPointCount(plan, capabilities)) {
            addError(report, "points", "point count is outside device capabilities");
            return report;
        }
        validateTrigger(plan.trigger, capabilities, report);
        // PointId 用于结果关联，ExcitationId 用于跨频点复用布局，两类身份都要稳定且唯一。
        std::set<PointId> pointIdentifiers;
        std::map<ExcitationId, std::vector<VnaWaveKey>> layouts;
        for (std::size_t index = 0; index < plan.points.size(); ++index) {
            if (!pointIdentifiers.insert(plan.points[index].pointId).second) {
                addError(report, "points", "point identifiers must be unique");
            }
            validatePoint(plan.points[index], capabilities, index, layouts, report);
        }
        return report;
    }

}  // namespace detail
}  // namespace vna
