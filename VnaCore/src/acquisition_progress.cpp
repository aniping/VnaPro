/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "vna_core/acquisition_core.h"
#include "vna_core/frame_layout.h"
#include "vna_log/logger.h"

namespace VnaCore {
namespace {

// 项目的显示节流参数；不是对商用仪器内部刷新门槛的推断。
constexpr std::chrono::milliseconds g_slowSweepDisplayInterval(100);
constexpr double PREVIEW_MIN_SWEEP_SECONDS = 1.0;
constexpr double PREVIEW_MIN_REMAINING_SECONDS = 0.5;
constexpr std::uint64_t CORE_IDENTITY_MAX = (std::numeric_limits<std::uint64_t>::max)();

bool SlowEnoughForPreview(const SweepPlan &plan, std::size_t completed, double elapsed)
{
    if (completed == 0U || completed >= plan.frequencyRaw.size()) {
        return false;
    }
    if (!plan.sweepTimeAuto && plan.sweepTimeSeconds >= PREVIEW_MIN_SWEEP_SECONDS &&
        plan.sweepTimeSeconds - elapsed >= PREVIEW_MIN_REMAINING_SECONDS) {
        return true;
    }
    const double remaining = elapsed * static_cast<double>(plan.frequencyRaw.size() - completed) / completed;
    return elapsed >= PREVIEW_MIN_SWEEP_SECONDS && remaining >= PREVIEW_MIN_REMAINING_SECONDS;
}

}  // 匿名命名空间

void AcquisitionCore::AcquireFrame(unsigned channelId)
{
    auto &channel = channels_.at(channelId);
    if (channel.frameSeq == CORE_IDENTITY_MAX) {
        throw std::overflow_error("frame sequence exhausted");
    }
    ApplyChannelPlan(channelId);
    MeasurementFrame completed;
    completed.channelId = channelId;
    completed.runId = channel.runId;
    completed.configRevision = channel.revision;
    completed.frameSeq = channel.frameSeq + 1U;
    completed.plan = channel.plan;
    const auto began = std::chrono::steady_clock::now();
    completed.ports = device_->AcquireSweep(channel.plan, CreateProgressReporter(channelId), CreateReadGate(channelId));
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    if (completed.ports.size() != channel.plan.sourcePorts.size()) {
        throw std::runtime_error("device returned an incomplete multi-source sweep");
    }
    for (std::size_t index = 0U; index < completed.ports.size(); ++index) {
        const auto &port = completed.ports[index];
        if (port.sourcePort != channel.plan.sourcePorts[index]) {
            throw std::runtime_error("device returned mismatched or reordered source ports");
        }
        if (port.iq.size() != IQ_VALUES_PER_POINT * channel.plan.frequencyRaw.size()) {
            throw std::runtime_error("device returned an incomplete IQ port frame");
        }
    }
    // 发布后不保留可变别名；后续端口采集失败时，不替换 frame_。
    FramePtr immutable = std::make_shared<const MeasurementFrame>(std::move(completed));
    channel.frameSeq = immutable->frameSeq;
    channel.frame = std::move(immutable);
    channel.lastSweepTimeSeconds = elapsed;
    channel.sweepProgress.reset();
    PublishView();
    VnaLog::Trace(VnaLog::Module::CORE,
        "完整采集帧已发布，channel={}，run={}，frame={}，revision={}，points={}，ports={}", channelId, channel.runId,
        channel.frameSeq, channel.revision, channel.plan.frequencyRaw.size(), channel.frame->ports.size());
}

Detail::SweepProgressCallback AcquisitionCore::CreateProgressReporter(unsigned channelId)
{
    try {
        return [this, channelId, began = std::chrono::steady_clock::now(), last = std::chrono::steady_clock::now(),
                   enabled = true, publishing = false](std::size_t count, const std::vector<PortFrame> &ports) mutable {
            const auto now = std::chrono::steady_clock::now();
            if (!enabled || now - last < g_slowSweepDisplayInterval) {
                return;
            }
            const auto elapsed = std::chrono::duration<double>(now - began).count();
            if (!publishing && !SlowEnoughForPreview(channels_.at(channelId).plan, count, elapsed)) {
                return;
            }
            try {
                PublishSweepProgress(channelId, count, ports);
                last = now;
                publishing = true;
            } catch (const std::bad_alloc &) {
                // 显示快照分配失败不使实际测量失败；本扫余下阶段只保留完整帧路径。
                enabled = false;
                ClearSweepProgress(channelId);
            }
        };
    } catch (const std::bad_alloc &) {
        return {};
    }
}

void AcquisitionCore::ClearSweepProgress(unsigned channelId)
{
    channels_.at(channelId).sweepProgress.reset();
    std::lock_guard<std::mutex> guard(resultMutex_);
    const auto found = published_.find(channelId);
    if (found != published_.end()) {
        found->second.sweepProgress.reset();
    }
}

void AcquisitionCore::PublishSweepProgress(
    unsigned channelId, std::size_t completedPoints, const std::vector<PortFrame> &ports)
{
    auto &channel = channels_.at(channelId);
    const auto total = channel.plan.frequencyRaw.size();
    if (completedPoints == 0U || completedPoints >= total || ports.size() != channel.plan.sourcePorts.size() ||
        (channel.sweepProgress && completedPoints <= channel.sweepProgress->completedPoints)) {
        throw std::runtime_error("device returned invalid sweep progress");
    }
    MeasurementFrame partial;
    partial.channelId = channelId;
    partial.runId = channel.runId;
    partial.configRevision = channel.revision;
    partial.frameSeq = channel.frameSeq + 1U;
    partial.plan = channel.plan;
    partial.ports.reserve(ports.size());
    for (std::size_t index = 0U; index < ports.size(); ++index) {
        const auto &port = ports[index];
        if (port.sourcePort != channel.plan.sourcePorts[index] || port.iq.size() != total * IQ_VALUES_PER_POINT) {
            throw std::runtime_error("device returned incomplete multi-source sweep progress");
        }
        PortFrame copied;
        copied.sourcePort = port.sourcePort;
        copied.iq.assign(total * IQ_VALUES_PER_POINT, std::numeric_limits<double>::quiet_NaN());
        std::copy_n(port.iq.begin(), completedPoints * IQ_VALUES_PER_POINT, copied.iq.begin());
        partial.ports.push_back(std::move(copied));
    }
    SweepProgress progress;
    progress.frame = std::make_shared<const MeasurementFrame>(std::move(partial));
    progress.completedPoints = completedPoints;
    channel.sweepProgress = std::make_shared<const SweepProgress>(std::move(progress));
    PublishView();
}

}  // 命名空间 VnaCore
