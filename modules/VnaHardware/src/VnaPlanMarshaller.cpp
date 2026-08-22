#include "VnaPlanMarshaller.h"

#include <cstdint>

namespace vna::detail {

VnaPlanMarshaller::VnaPlanMarshaller(const VnaSweepPlan &plan)
{
    // 先建立所有叶子存储，再建立借用其地址的视图，避免 vector 扩容使指针失效。
    buildPointStorage(plan);
    buildViews(plan);
    view_.structSize = static_cast<std::uint32_t>(sizeof(view_));
    view_.triggerMode = plan.trigger.mode == TriggerMode::Internal ? VNA_TRIGGER_INTERNAL : VNA_TRIGGER_EXTERNAL;
    view_.policyFlags = policyFlags(plan.policy);
    view_.pointCount = pointViews_.size();
    view_.points = pointViews_.data();
}

const VnaSweepPlanViewV1 &VnaPlanMarshaller::view() const
{
    // 返回视图仅在 marshaller 存活且未移动时有效，当前仅用于同步 Prepare 调用。
    return view_;
}

void VnaPlanMarshaller::buildPointStorage(const VnaSweepPlan &plan)
{
    // storage_ 镜像“频点 -> 激励 -> 源/波”层级，拥有 ABI 叶子数组的实际内存。
    storage_.resize(plan.points.size());
    for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
        const auto &point = plan.points[pointIndex];
        auto &pointStorage = storage_[pointIndex];
        pointStorage.resize(point.excitations.size());
        for (std::size_t excitationIndex = 0; excitationIndex < point.excitations.size(); ++excitationIndex) {
            buildExcitationStorage(point.excitations[excitationIndex], pointStorage[excitationIndex]);
        }
    }
}

void VnaPlanMarshaller::buildExcitationStorage(const VnaExcitationPlan &source, ExcitationStorage &destination)
{
    // C++ 强类型枚举和 chrono 在边界处转换为固定宽度 C ABI 表示。
    destination.sources.reserve(source.sources.size());
    for (const auto &item : source.sources) {
        destination.sources.push_back(VnaSourcePlanV1{item.port, 0, item.powerDbm, item.phaseDegrees});
    }
    destination.waves.reserve(source.requiredWaves.size());
    for (const auto &wave : source.requiredWaves) {
        const std::uint32_t kind = wave.kind == WaveKind::Incident ? VNA_WAVE_INCIDENT : VNA_WAVE_OUTGOING;
        destination.waves.push_back(VnaWaveKeyV1{kind, wave.port, 0});
    }
}

void VnaPlanMarshaller::buildViews(const VnaSweepPlan &plan)
{
    // reserve 后再 push，确保 pointViews_ 与 excitationViews_ 发布的 data 指针保持稳定。
    excitationViews_.resize(plan.points.size());
    pointViews_.reserve(plan.points.size());
    for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
        const auto &point = plan.points[pointIndex];
        auto &views = excitationViews_[pointIndex];
        views.reserve(point.excitations.size());
        for (std::size_t index = 0; index < point.excitations.size(); ++index) {
            const auto &excitation = point.excitations[index];
            const auto &storage = storage_[pointIndex][index];
            views.push_back(
                VnaExcitationPlanViewV1{
                    excitation.excitationId, 0, storage.sources.size(), storage.sources.data(), storage.waves.size(),
                    storage.waves.data()});
        }
        pointViews_.push_back(
            VnaPointPlanViewV1{
                point.pointId, 0, point.frequencyHz, point.ifBandwidthHz, point.dwellTime.count(), views.size(),
                views.data()});
    }
}

std::uint32_t VnaPlanMarshaller::policyFlags(const SweepPolicy &policy)
{
    // 每个布尔策略映射到独立位，未声明的保留位始终为零。
    std::uint32_t flags = 0;
    flags |= policy.allowQuantization ? VNA_POLICY_ALLOW_QUANTIZATION : 0;
    flags |= policy.allowMultiplePhysicalSweeps ? VNA_POLICY_ALLOW_MULTIPLE_SWEEPS : 0;
    flags |= policy.requirePhaseCoherence ? VNA_POLICY_REQUIRE_PHASE_COHERENCE : 0;
    flags |= policy.requireAtomicConfiguration ? VNA_POLICY_REQUIRE_ATOMIC_CONFIGURATION : 0;
    return flags;
}

}  // namespace vna::detail
