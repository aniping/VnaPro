#include "SimulatorDevice.h"

#include <algorithm>
#include <cstdint>
#include <map>

namespace vna::simulator {
namespace {

    bool equalWaves(const std::vector<VnaWaveKeyV1> &left, const std::vector<Wave> &right)
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if ((left[index].kind != right[index].kind) || (left[index].port != right[index].port)) {
                return false;
            }
        }
        return true;
    }

    std::vector<VnaWaveKeyV1> copyWaveKeys(const Excitation &excitation)
    {
        std::vector<VnaWaveKeyV1> waves;
        waves.reserve(excitation.waves.size());
        for (const auto &wave : excitation.waves) {
            waves.push_back(VnaWaveKeyV1{wave.kind, wave.port, 0});
        }
        return waves;
    }

    VnaStatusV1 assignLayout(
        Excitation &excitation, std::map<std::uint16_t, std::size_t> &identifiers,
        std::vector<std::uint16_t> &layoutExcitations, std::vector<std::vector<VnaWaveKeyV1>> &layoutWaves)
    {
        // 同一 excitationId 跨频点复用 layoutId，宿主才能稳定解释每列波数据。
        const auto existing = identifiers.find(excitation.identifier);
        if (existing == identifiers.end()) {
            const std::size_t index = layoutWaves.size();
            identifiers.emplace(excitation.identifier, index);
            layoutExcitations.push_back(excitation.identifier);
            layoutWaves.push_back(copyWaveKeys(excitation));
            excitation.layoutId = static_cast<std::uint32_t>(index + 1);
            return simulatorStatus(VNA_STATUS_OK, "");
        }
        if (!equalWaves(layoutWaves[existing->second], excitation.waves)) {
            return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "wave layout changed");
        }
        excitation.layoutId = static_cast<std::uint32_t>(existing->second + 1);
        return simulatorStatus(VNA_STATUS_OK, "");
    }

}  // namespace

VnaStatusV1 SimulatorDevice::rebuildLayouts(VnaPrepareResultViewV1 &result)
{
    // 先重建自有容器，最后发布 view；view 中指针在下一次 Prepare/Disconnect 前保持稳定。
    layoutWaves_.clear();
    layoutExcitations_.clear();
    layoutViews_.clear();
    std::map<std::uint16_t, std::size_t> identifiers;
    // 估算峰值单频点载荷，而不是整次扫描总量，供宿主预估实时带宽。
    std::size_t estimatedBytes = 0;
    for (auto &point : points_) {
        std::size_t pointBytes = 0;
        for (auto &excitation : point.excitations) {
            const VnaStatusV1 status = assignLayout(excitation, identifiers, layoutExcitations_, layoutWaves_);
            if (status.code != VNA_STATUS_OK) {
                return status;
            }
            pointBytes += excitation.waves.size() * sizeof(VnaComplex64V1);
        }
        estimatedBytes = std::max(estimatedBytes, pointBytes);
    }
    for (std::size_t index = 0; index < layoutWaves_.size(); ++index) {
        layoutViews_.push_back(
            VnaWaveLayoutViewV1{
                static_cast<std::uint32_t>(index + 1), layoutExcitations_[index], 0, layoutWaves_[index].size(),
                layoutWaves_[index].data()});
    }
    result.structSize = static_cast<std::uint32_t>(sizeof(result));
    result.physicalSweepCount = 1;
    result.preferredBatchPoints = simulatorBatchPoints;
    result.estimatedBytesPerPoint = estimatedBytes;
    result.executionFlags = VNA_EXECUTION_ATOMIC | VNA_EXECUTION_PHASE_COHERENT;
    result.layoutCount = layoutViews_.size();
    result.layouts = layoutViews_.data();
    return simulatorStatus(VNA_STATUS_OK, "");
}

}  // namespace vna::simulator
