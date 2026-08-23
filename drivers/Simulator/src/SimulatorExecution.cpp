#include "SimulatorDevice.h"
#include "SimulatorTiming.h"
#include "SimulatorWaveBlockLease.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vna::simulator {
namespace {

    using MeasurementClock = std::chrono::steady_clock;
    constexpr auto cancellationPollInterval = std::chrono::milliseconds(10);

    VnaStringViewV1 literalView(const char *message)
    {
        return VnaStringViewV1{message, std::char_traits<char>::length(message)};
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
        const std::size_t end = std::min(points.size(), firstPoint + simulatorBatchPoints);
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
        return (view.structSize >= sizeof(view)) && (view.reserved == 0) && (view.pointCapacity >= pointCount) &&
               (view.waveCount == waveCount) && validStorage;
    }

    bool advanceMeasurementDeadline(MeasurementClock::time_point &deadline, std::chrono::nanoseconds duration) noexcept
    {
        using FloatingClockDuration = std::chrono::duration<long double, MeasurementClock::period>;
        const long double roundedTicks = std::ceil(FloatingClockDuration{duration}.count());
        const long double maximumTicks =
            static_cast<long double>(std::numeric_limits<MeasurementClock::duration::rep>::max());
        // maximumTicks 在窄 long double 平台可能向上舍入，等于该值时也必须拒绝转换。
        if (!std::isfinite(roundedTicks) || (roundedTicks < 0.0L) || (roundedTicks >= maximumTicks)) {
            return false;
        }
        const auto clockDuration =
            MeasurementClock::duration{static_cast<MeasurementClock::duration::rep>(roundedTicks)};
        const auto maximumDeadlineTicks = MeasurementClock::time_point::max().time_since_epoch().count();
        const auto currentTicks = deadline.time_since_epoch().count();
        if (currentTicks > (maximumDeadlineTicks - clockDuration.count())) {
            return false;
        }
        deadline += clockDuration;
        return true;
    }

}  // namespace

bool SimulatorDevice::cancellationRequested(VnaSweepIdV1 sweepId) const
{
    // 同时尊重驱动本地 abort 和宿主会话关闭取消，两条路径任一成立都应尽快退出。
    return abortRequested_.load() || (hostApi_.IsCancellationRequested(sweepId) != 0);
}

bool SimulatorDevice::waitUntilMeasurementReady(VnaSweepIdV1 sweepId, MeasurementClock::time_point deadline)
{
    while (true) {
        // Host API 属于未知外部代码，必须在未持 sweepMutex_ 时轮询。
        if (cancellationRequested(sweepId)) {
            return false;
        }
        const auto now = MeasurementClock::now();
        if (now >= deadline) {
            return true;
        }
        const auto pollDeadline = std::min(deadline, now + cancellationPollInterval);
        std::unique_lock<std::mutex> lock(sweepMutex_);
        const bool interrupted = sweepChanged_.wait_until(
            lock, pollDeadline, [this, sweepId] { return abortRequested_.load() || (activeSweep_.load() != sweepId); });
        if (interrupted) {
            return false;
        }
    }
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
    try {
        auto measurementDeadline = MeasurementClock::now();
        hostApi_.Log(VNA_LOG_INFO, literalView("simulator sweep started"));
        VnaStatusV1 status = simulatorStatus(VNA_STATUS_OK, "");
        // 按布局输出能让每个块保持固定 waveCount 和 waveIndex 语义。
        for (std::size_t index = 0; index < layoutViews_.size(); ++index) {
            status = executeLayout(sweepId, index, measurementDeadline);
            if (status.code != VNA_STATUS_OK) {
                break;
            }
        }
        // 收尾与 RequestAbort 用同一把锁交握，避免请求返回 OK 却没有导致 Cancelled。
        status = finishExecution(status);
        hostApi_.Log(VNA_LOG_INFO, literalView("simulator sweep finished"));
        return status;
    } catch (...) {
        // beginSweep 发布状态后，任何异常都必须撤销活动 Sweep；C ABI 外层负责转换错误码。
        (void)finishExecution(simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator execution failed"));
        throw;
    }
}

bool SimulatorDevice::beginSweep(VnaSweepIdV1 sweepId)
{
    std::lock_guard<std::mutex> lock(sweepMutex_);
    const bool ready = connected_ && prepared_ && (sweepId != 0);
    if (!ready || (activeSweep_.load() != 0)) {
        return false;
    }
    // 中止只属于一次 SweepId，不能污染同一配置的后续重复执行。
    abortRequested_.store(false);
    abortAdmissionOpen_ = true;
    activeSweep_.store(sweepId);
    return true;
}

VnaStatusV1 SimulatorDevice::finishExecution(VnaStatusV1 status)
{
    {
        std::lock_guard<std::mutex> lock(sweepMutex_);
        // 若 RequestAbort 先获锁并返回 OK，本次 Execute 必须以 Cancelled 结束。
        if ((status.code == VNA_STATUS_OK) && abortRequested_.load()) {
            status = simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
        }
        // 若本函数先获锁清空标识，稍后的 RequestAbort 只能返回 InvalidState。
        abortAdmissionOpen_ = false;
        activeSweep_.store(0);
    }
    sweepChanged_.notify_all();
    return status;
}

VnaStatusV1 SimulatorDevice::executeLayout(
    VnaSweepIdV1 sweepId, std::size_t layoutIndex, MeasurementClock::time_point &measurementDeadline)
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
        const auto timing = calculateBatchMeasurementTiming(points_, pointIndex, count, layoutId);
        if (!timing.valid || !advanceMeasurementDeadline(measurementDeadline, timing.duration)) {
            return simulatorStatus(VNA_STATUS_PROTOCOL_ERROR, "simulator measurement timing overflowed");
        }
        // 累计绝对截止时间让处理开销与下一批测量重叠，调度超时不会逐批累积。
        if (!waitUntilMeasurementReady(sweepId, measurementDeadline)) {
            return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
        }
        const bool finalLayout = (layoutIndex + 1) == layoutViews_.size();
        const bool finalBatch =
            finalLayout && (nextPointWithLayout(points_, pointIndex + count, layoutId) == points_.size());
        const VnaStatusV1 status =
            emitBatch(sweepId, layoutIndex, static_cast<std::uint32_t>(pointIndex), count, finalBatch);
        if (status.code != VNA_STATUS_OK) {
            return status;
        }
        pointIndex += count;
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::emitBatch(
    VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount, bool finalBatch)
{
    if (cancellationRequested(sweepId)) {
        return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
    }
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));
    // Acquire 成功后只有 Commit 或 Discard 两个出口，保证宿主槽位不会泄漏。
    VnaStatusV1 status =
        hostApi_.AcquireWaveBlock(sweepId, static_cast<std::uint32_t>(layoutIndex + 1), pointCount, &handle, &view);
    if (status.code != VNA_STATUS_OK) {
        return status;
    }
    // Acquire 成功后立刻绑定租约；Commit 正常返回前的任何返回或异常都自动且仅 Discard 一次。
    SimulatorWaveBlockLease lease(hostApi_, handle);
    if (!validWaveBuffer(view, pointCount, layoutWaves_[layoutIndex].size())) {
        return simulatorStatus(VNA_STATUS_PROTOCOL_ERROR, "host returned an invalid wave buffer");
    }
    if (cancellationRequested(sweepId)) {
        return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
    }
    status = fillBatch(sweepId, layoutIndex, firstPoint, pointCount, view);
    if (status.code != VNA_STATUS_OK) {
        return status;
    }
    if (cancellationRequested(sweepId)) {
        return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
    }
    // 最终 Commit 的准入与 RequestAbort 共用 sweepMutex_ 线性化；之后迟到的中止只能失败。
    if (finalBatch && !closeAbortAdmissionForFinalCommit(sweepId)) {
        return simulatorStatus(VNA_STATUS_CANCELLED, "simulator sweep was cancelled");
    }
    return lease.commit(firstPoint, pointCount);
}

bool SimulatorDevice::closeAbortAdmissionForFinalCommit(VnaSweepIdV1 sweepId)
{
    std::lock_guard<std::mutex> lock(sweepMutex_);
    if ((activeSweep_.load() != sweepId) || abortRequested_.load() || !abortAdmissionOpen_) {
        return false;
    }
    abortAdmissionOpen_ = false;
    return true;
}

VnaStatusV1 SimulatorDevice::fillBatch(
    VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount,
    VnaMutableWaveBlockViewV1 &view) const
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
            view.values[index] = simulateWave(sweepId, point, *excitation, excitation->waves[wave]);
            view.qualityFlags[index] = VNA_QUALITY_VALID;
        }
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::requestAbort(VnaSweepIdV1 sweepId)
{
    // sweepId 防止迟到的中止请求误伤后来复用同一已准备配置的新扫描。
    {
        std::lock_guard<std::mutex> lock(sweepMutex_);
        if ((sweepId == 0) || (activeSweep_.load() != sweepId) || !abortAdmissionOpen_) {
            return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator sweep is not active");
        }
        // 持锁写入使收尾要么看到请求，要么先将扫频置为非活动并让本请求失败。
        abortRequested_.store(true);
    }
    sweepChanged_.notify_all();
    hostApi_.Log(VNA_LOG_WARNING, literalView("simulator abort requested"));
    return simulatorStatus(VNA_STATUS_OK, "");
}

}  // namespace vna::simulator
