#include "VnaIntegrationTestSupport.h"

#include <algorithm>
#include <utility>

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

void HardwareSink::onConnectCompleted(const ConnectCompletedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    completedCommands_.push_back(event.commandId);
    changed_.notify_all();
}

void HardwareSink::onPrepareCompleted(const PrepareCompletedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 同时记录最新配置和完成次数，支持验证失败 Prepare 不会覆盖既有配置。
    configuration_ = event.prepared.configurationId;
    ++prepareCount_;
    completedCommands_.push_back(event.commandId);
    changed_.notify_all();
}

void HardwareSink::onStartCompleted(const StartCompletedEvent &event)
{
    // 先取走一次性动作，但不能持测试状态锁调用硬件 API，避免回调重入造成锁反转。
    IVnaHardware *abortTarget = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abortTarget = std::exchange(abortOnStart_, nullptr);
    }
    // 在用户可观察 StartCompleted 的最早时刻入队，专门压测 Worker 尚未进入 Execute 的窗口。
    CommandId abortCommand = 0;
    if (abortTarget != nullptr) {
        abortCommand = abortTarget->abortAsync(event.sweepId);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 一次持锁发布关联字段，waitStarted() 成功后不会观察到半完成的回调快照。
        sweeps_.push_back(event.sweepId);
        completedCommands_.push_back(event.commandId);
        if (abortCommand != 0) {
            triggeredAbortCommand_ = abortCommand;
        }
    }
    changed_.notify_all();
}

void HardwareSink::onAbortCompleted(const AbortCompletedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    abortFollowedSweep_ = !completions_.empty() && (completions_.back().sweepId == event.sweepId);
    aborts_.push_back(event);
    completedCommands_.push_back(event.commandId);
    changed_.notify_all();
}

void HardwareSink::onDisconnectCompleted(const DisconnectCompletedEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectFollowedState_ = state_ == DeviceState::Disconnected;
    ++disconnectCount_;
    completedCommands_.push_back(event.commandId);
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
    failedCommands_.push_back(event.commandId);
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

bool HardwareSink::waitAborted(std::size_t count)
{
    return waitFor([this, count] { return aborts_.size() >= count; });
}

bool HardwareSink::waitDisconnected(std::size_t count)
{
    return waitFor([this, count] { return disconnectCount_ >= count; });
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

bool HardwareSink::abortFollowedSweep() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return abortFollowedSweep_;
}

bool HardwareSink::disconnectFollowedState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return disconnectFollowedState_;
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

std::uint32_t HardwareSink::blockQuality(std::size_t block, std::size_t wave, std::size_t point) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(block).quality(wave, point);
}

void HardwareSink::abortOnNextStart(IVnaHardware &hardware)
{
    std::lock_guard<std::mutex> lock(mutex_);
    abortOnStart_ = &hardware;
}

bool HardwareSink::completedCommand(CommandId commandId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(completedCommands_.begin(), completedCommands_.end(), commandId) != completedCommands_.end();
}

bool HardwareSink::failedCommand(CommandId commandId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(failedCommands_.begin(), failedCommands_.end(), commandId) != failedCommands_.end();
}

CommandId HardwareSink::triggeredAbortCommand() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return triggeredAbortCommand_;
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

}  // namespace vna::test
