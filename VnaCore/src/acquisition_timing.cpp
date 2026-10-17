/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>

#include "vna_core/acquisition_core.h"

namespace VnaCore {
namespace {

bool SupersedesReadout(const Detail::CoreCommand &command, unsigned channelId, std::uint64_t runId)
{
    if (command.channelId != channelId) {
        return false;
    }
    return command.kind == Detail::CoreCommandKind::CONFIGURE ||
           (command.kind == Detail::CoreCommandKind::STOP && command.runId == runId);
}

}  // 匿名命名空间

Detail::SweepReadGate AcquisitionCore::CreateReadGate(unsigned channelId)
{
    const auto &channel = channels_.at(channelId);
    if (channel.plan.sweepTimeAuto) {
        return {};
    }
    return [this, channelId, run = channel.runId, seconds = channel.plan.sweepTimeSeconds,
               total = channel.plan.frequencyRaw.size(), began = std::chrono::steady_clock::now()](std::size_t end) {
        const auto duration = std::chrono::duration<double>(seconds * (static_cast<double>(end) / total));
        const auto deadline = began + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        std::unique_lock<std::mutex> guard(queueMutex_);
        return !wake_.wait_until(guard, deadline, [this, channelId, run] {
            return closing_ ||
                   std::any_of(queue_.begin(), queue_.end(), [channelId, run](const Detail::CoreCommand &c) {
                       return SupersedesReadout(c, channelId, run);
                   });
        });
    };
}

}  // 命名空间 VnaCore
