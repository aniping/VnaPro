#include "VnaIntegrationTestSupport.h"

#include <cmath>
#include <utility>

#include <gtest/gtest.h>

namespace vna::test {

void DiscoverySink::onDeviceFound(DiscoveryId, const DeviceDescriptor &descriptor)
{
    // descriptor 是回调只读引用，复制后测试线程才可在回调结束后继续使用。
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.push_back(descriptor);
}

void DiscoverySink::onDiscoveryCompleted(DiscoveryId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completed_ = true;
    changed_.notify_all();
}

void DiscoverySink::onDiscoveryFailed(DiscoveryId, const VnaError &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = error;
    completed_ = true;
    changed_.notify_all();
}

bool DiscoverySink::wait()
{
    // 所有异步等待均设两秒上限，线程死锁会变成确定的测试失败而不是永久挂起。
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return completed_; });
}

DeviceDescriptor DiscoverySink::firstDevice() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.at(0);
}

bool DiscoverySink::succeeded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_.code == VnaErrorCode::None;
}

void HardwareSink::onStateChanged(const StateChangedEvent &event)
{
    // 回调只记录快照并唤醒等待者，不在分发线程内执行断言或阻塞业务操作。
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = event.current;
    changed_.notify_all();
}

void HardwareSink::onConnectCompleted(const ConnectCompletedEvent &)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    changed_.notify_all();
}

void HardwareSink::onPrepareCompleted(const PrepareCompletedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 同时记录最新配置和完成次数，支持验证失败 Prepare 不会覆盖既有配置。
    configuration_ = event.prepared.configurationId;
    ++prepareCount_;
    changed_.notify_all();
}

void HardwareSink::onStartCompleted(const StartCompletedEvent &event)
{
    // 保存所有 sweepId，后续用例可区分中止请求是否误作用于下一次执行。
    std::lock_guard<std::mutex> lock(mutex_);
    sweeps_.push_back(event.sweepId);
    changed_.notify_all();
}

void HardwareSink::onWaveBlock(VnaWaveBlock block)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 保存移动块会持续占用池槽，测试随后 clearBlocks() 验证析构归还路径。
    blocks_.push_back(std::move(block));
    changed_.notify_all();
}

void HardwareSink::onSweepCompleted(const SweepCompletion &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completions_.push_back(event);
    changed_.notify_all();
}

void HardwareSink::onCommandFailed(const CommandFailedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    failures_.push_back(event.error);
    changed_.notify_all();
}

bool HardwareSink::waitConnected()
{
    return waitFor([this] { return connected_; });
}

bool HardwareSink::waitPrepared(std::size_t count)
{
    return waitFor([this, count] { return prepareCount_ >= count; });
}

bool HardwareSink::waitStarted(std::size_t count)
{
    return waitFor([this, count] { return sweeps_.size() >= count; });
}

bool HardwareSink::waitCompleted(std::size_t count)
{
    return waitFor([this, count] { return completions_.size() >= count; });
}

bool HardwareSink::waitFailures(std::size_t count)
{
    return waitFor([this, count] { return failures_.size() >= count; });
}

bool HardwareSink::waitState(DeviceState state)
{
    return waitFor([this, state] { return state_ == state; });
}

ConfigurationId HardwareSink::configuration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return configuration_;
}

SweepId HardwareSink::sweep(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sweeps_.at(index);
}

SweepCompletion HardwareSink::completion(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return completions_.at(index);
}

std::size_t HardwareSink::blockCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

std::uint64_t HardwareSink::blockSequence(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(index).sequence();
}

std::uint32_t HardwareSink::blockPointCount(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(index).pointCount();
}

std::complex<double> HardwareSink::blockValue(std::size_t block, std::size_t wave, std::size_t point) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(block).value(wave, point);
}

void HardwareSink::clearBlocks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // clear 触发 VnaWaveBlock 析构，归还测试刻意持有的所有缓冲槽。
    blocks_.clear();
}

std::size_t HardwareSink::failureCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failures_.size();
}

VnaSweepPlan makePlan()
{
    // 五个点故意超过批大小 2，用一个计划同时覆盖完整批和尾部短批。
    VnaSweepPlan plan;
    for (std::uint32_t index = 0; index < 5; ++index) {
        VnaExcitationPlan excitation;
        excitation.excitationId = 1;
        excitation.sources.push_back(VnaSourcePlan{1, 0.0, 0.0});
        excitation.requiredWaves.push_back(VnaWaveKey{WaveKind::Incident, 1});
        excitation.requiredWaves.push_back(VnaWaveKey{WaveKind::Outgoing, 2});

        VnaPointPlan point;
        point.pointId = index + 1;
        point.frequencyHz = 1.0e9 + (static_cast<double>(index) * 1.0e6);
        point.ifBandwidthHz = 1000.0 + static_cast<double>(index);
        point.dwellTime = std::chrono::microseconds(10);
        point.excitations.push_back(std::move(excitation));
        plan.points.push_back(std::move(point));
    }
    return plan;
}

void verifyCompletedSweep(const HardwareSink &events)
{
    EXPECT_EQ(events.completion(0).reason, SweepTerminationReason::Completed);
    // 五个点按批次容量 2 被拆为 2、2、1 三个连续块。
    ASSERT_EQ(events.blockCount(), 3U);
    EXPECT_EQ(events.blockSequence(0), 0U);
    EXPECT_EQ(events.blockSequence(1), 1U);
    EXPECT_EQ(events.blockSequence(2), 2U);
    EXPECT_EQ(events.blockPointCount(0), 2U);
    EXPECT_EQ(events.blockPointCount(2), 1U);

    // 入射波幅度为 1；跨一个端口的出射波使用 0.5 增益和确定性传播相位。
    EXPECT_NEAR(events.blockValue(0, 0, 0).real(), 1.0, 1.0e-12);
    EXPECT_NEAR(events.blockValue(0, 0, 0).imag(), 0.0, 1.0e-12);
    constexpr double pi = 3.14159265358979323846;
    EXPECT_NEAR(events.blockValue(0, 1, 0).real(), 0.5 * std::cos(-0.2 * pi), 1.0e-12);
}

}  // namespace vna::test
