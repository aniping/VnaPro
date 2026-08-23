#include "SimulatorDevice.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <thread>

namespace vna::simulator {
namespace {

    constexpr std::uint32_t batchPoints = 2;

    VnaStringViewV1 literalView(const char *message)
    {
        return VnaStringViewV1{message, std::char_traits<char>::length(message)};
    }

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

    std::size_t nextPointWithLayout(const std::vector<Point> &points, std::size_t firstPoint, std::uint32_t layoutId)
    {
        // 某些频点可能没有该激励；按布局扫描时跳到下一个实际出现的位置。
        const auto match = std::find_if(
            points.begin() + static_cast<std::ptrdiff_t>(firstPoint), points.end(), [layoutId](const Point &point) {
                return std::any_of(
                    point.excitations.begin(), point.excitations.end(),
                    [layoutId](const Excitation &excitation) { return excitation.layoutId == layoutId; });
            });
        return static_cast<std::size_t>(std::distance(points.begin(), match));
    }

    std::uint32_t contiguousBatchSize(const std::vector<Point> &points, std::size_t firstPoint, std::uint32_t layoutId)
    {
        // 一个块只能描述连续 pointIndex；遇到布局缺口必须结束当前批次。
        const std::size_t end = std::min(points.size(), firstPoint + batchPoints);
        const std::size_t nextMissing = nextPointWithLayout(points, firstPoint, layoutId);
        if (nextMissing != firstPoint) {
            return 0;
        }
        std::size_t cursor = firstPoint;
        while ((cursor < end) && (nextPointWithLayout(points, cursor, layoutId) == cursor)) {
            ++cursor;
        }
        return static_cast<std::uint32_t>(cursor - firstPoint);
    }

    bool validWaveBuffer(const VnaMutableWaveBlockViewV1 &view, std::uint32_t pointCount, std::size_t waveCount)
    {
        const bool validStorage = (view.values != nullptr) && (view.qualityFlags != nullptr);
        return (view.pointCapacity >= pointCount) && (view.waveCount == waveCount) && validStorage;
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
    result.preferredBatchPoints = batchPoints;
    result.estimatedBytesPerPoint = estimatedBytes;
    result.executionFlags = VNA_EXECUTION_ATOMIC | VNA_EXECUTION_PHASE_COHERENT;
    result.layoutCount = layoutViews_.size();
    result.layouts = layoutViews_.data();
    return simulatorStatus(VNA_STATUS_OK, "");
}

bool SimulatorDevice::cancellationRequested(VnaSweepIdV1 sweepId) const
{
    // 同时尊重驱动本地 abort 和宿主会话关闭取消，两条路径任一成立都应尽快退出。
    return abortRequested_.load() || (hostApi_.IsCancellationRequested(sweepId) != 0);
}

const Excitation *SimulatorDevice::findExcitation(const Point &point, std::uint32_t layoutId) const
{
    const auto result = std::find_if(
        point.excitations.begin(), point.excitations.end(),
        [layoutId](const auto &excitation) { return excitation.layoutId == layoutId; });
    return result == point.excitations.end() ? nullptr : &*result;
}

VnaStatusV1 SimulatorDevice::execute(VnaSweepIdV1 sweepId)
{
    if (!beginSweep(sweepId)) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator cannot execute now");
    }
    hostApi_.Log(VNA_LOG_INFO, literalView("simulator sweep started"));
    VnaStatusV1 status = simulatorStatus(VNA_STATUS_OK, "");
    // 按布局输出能让每个块保持固定 waveCount 和 waveIndex 语义。
    for (std::size_t index = 0; index < layoutViews_.size(); ++index) {
        status = executeLayout(sweepId, index);
        if (status.code != VNA_STATUS_OK) {
            break;
        }
    }
    // 无论成功、取消还是驱动错误，都只在一个出口释放活动扫频标记。
    activeSweep_.store(0);
    hostApi_.Log(VNA_LOG_INFO, literalView("simulator sweep finished"));
    return status;
}

bool SimulatorDevice::beginSweep(VnaSweepIdV1 sweepId)
{
    VnaSweepIdV1 inactiveSweep = 0;
    const bool ready = connected_ && prepared_ && (sweepId != 0);
    if (!ready || !activeSweep_.compare_exchange_strong(inactiveSweep, sweepId)) {
        return false;
    }
    // 中止只属于一次 SweepId，不能污染同一配置的后续重复执行。
    abortRequested_.store(false);
    return true;
}

VnaStatusV1 SimulatorDevice::executeLayout(VnaSweepIdV1 sweepId, std::size_t layoutIndex)
{
    const std::uint32_t layoutId = static_cast<std::uint32_t>(layoutIndex + 1);
    std::size_t pointIndex = 0;
    while (pointIndex < points_.size()) {
        pointIndex = nextPointWithLayout(points_, pointIndex, layoutId);
        if (pointIndex == points_.size()) {
            break;
        }
        // 先计算连续批次再获取宿主缓冲区，绝不为不存在的布局申请容量。
        const std::uint32_t count = contiguousBatchSize(points_, pointIndex, layoutId);
        if (cancellationRequested(sweepId)) {
            return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
        }
        const VnaStatusV1 status = emitBatch(sweepId, layoutIndex, static_cast<std::uint32_t>(pointIndex), count);
        if (status.code != VNA_STATUS_OK) {
            return status;
        }
        pointIndex += count;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::emitBatch(
    VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount)
{
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));
    // Acquire 成功后只有 Commit 或 Discard 两个出口，保证宿主槽位不会泄漏。
    VnaStatusV1 status =
        hostApi_.AcquireWaveBlock(sweepId, static_cast<std::uint32_t>(layoutIndex + 1), pointCount, &handle, &view);
    if (status.code != VNA_STATUS_OK) {
        return status;
    }
    if (!validWaveBuffer(view, pointCount, layoutWaves_[layoutIndex].size())) {
        hostApi_.DiscardWaveBlock(handle);
        return simulatorStatus(VNA_STATUS_PROTOCOL_ERROR, "host returned an invalid wave buffer");
    }
    status = fillBatch(layoutIndex, firstPoint, pointCount, view);
    if (status.code != VNA_STATUS_OK) {
        hostApi_.DiscardWaveBlock(handle);
        return status;
    }
    return hostApi_.CommitWaveBlock(handle, firstPoint, pointCount);
}

VnaStatusV1 SimulatorDevice::fillBatch(
    std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount, VnaMutableWaveBlockViewV1 &view) const
{
    for (std::uint32_t offset = 0; offset < pointCount; ++offset) {
        const Point &point = points_[firstPoint + offset];
        const Excitation *excitation = findExcitation(point, static_cast<std::uint32_t>(layoutIndex + 1));
        if (excitation == nullptr) {
            return simulatorStatus(VNA_STATUS_PROTOCOL_ERROR, "simulator layout invariant was violated");
        }
        for (std::uint32_t wave = 0; wave < view.waveCount; ++wave) {
            // ABI 缓冲区按“波主序”排布，跨距使用 capacity 而不是本批 pointCount。
            const std::size_t index = (wave * view.pointCapacity) + offset;
            view.values[index] = simulateWave(point, *excitation, excitation->waves[wave]);
            view.qualityFlags[index] = VNA_QUALITY_VALID;
        }
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::requestAbort(VnaSweepIdV1 sweepId)
{
    // sweepId 防止迟到的中止请求误伤后来复用同一已准备配置的新扫描。
    if ((sweepId == 0) || (activeSweep_.load() != sweepId)) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator sweep is not active");
    }
    abortRequested_.store(true);
    hostApi_.Log(VNA_LOG_WARNING, literalView("simulator abort requested"));
    return simulatorStatus(VNA_STATUS_OK, "");
}

}  // namespace vna::simulator
