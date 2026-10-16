/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "acquisition_core_test_peer.h"

#include <mutex>

#include "concurrency_assertions.h"

namespace VnaCoreTest {

void AcquisitionCoreTestPeer::SeedCounters(
    VnaCore::AcquisitionCore &core, std::uint64_t runId, std::uint64_t revision, std::uint64_t frameSeq)
{
    // 票据完成后，工作线程只会在此锁下检查状态并等待命令；禁止后台连续采集。
    std::lock_guard<std::mutex> guard(core.queueMutex_);
    Require(core.queue_.empty() && !core.closing_, "边界注入要求队列为空且核心尚未关闭");
    auto &channel = core.channels_.at(1U);
    for (const auto &entry : core.channels_) {
        Require(entry.second.state != VnaCore::CoreState::RUNNING || entry.second.mode == VnaCore::RunMode::MANUAL,
            "禁止在后台连续采集时注入计数器边界");
    }
    channel.runId = runId;
    channel.revision = revision;
    channel.frameSeq = frameSeq;
    core.PublishView();
}

}  // 命名空间 VnaCoreTest
