#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <gtest/gtest.h>

#include "VnaHardwareManager.h"

namespace {

class SingleDeviceDiscovery final : public vna::IVnaDiscoverySink {
public:
    // 发现回调来自后台线程，测试状态统一在 mutex_ 下发布给等待线程。
    void onDeviceFound(vna::DiscoveryId, const vna::DeviceDescriptor &descriptor) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptor_ = descriptor;
    }

    void onDiscoveryCompleted(vna::DiscoveryId) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = true;
        changed_.notify_all();
    }

    void onDiscoveryFailed(vna::DiscoveryId, const vna::VnaError &) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        completed_ = true;
        changed_.notify_all();
    }

    bool wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return completed_; });
    }

    bool failed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_;
    }

    std::string deviceId() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return descriptor_.deviceId;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    vna::DeviceDescriptor descriptor_;
    bool completed_{false};
    bool failed_{false};
};

class ResetOnConnectSink final : public vna::IVnaHardwareEventSink {
public:
    explicit ResetOnConnectSink(std::shared_ptr<vna::IVnaHardware> &hardware) : hardware_(&hardware)
    {
    }

    void onStateChanged(const vna::StateChangedEvent &) override
    {
    }

    void onConnectCompleted(const vna::ConnectCompletedEvent &) override
    {
        // 关键故障场景：在分发线程回调栈内释放最后一个 session 引用。
        hardware_->reset();
        std::lock_guard<std::mutex> lock(mutex_);
        resetCompleted_ = true;
        changed_.notify_all();
    }

    void onPrepareCompleted(const vna::PrepareCompletedEvent &) override
    {
    }
    void onStartCompleted(const vna::StartCompletedEvent &) override
    {
    }
    void onWaveBlock(vna::VnaWaveBlock) override
    {
    }
    void onSweepCompleted(const vna::SweepCompletion &) override
    {
    }
    void onCommandFailed(const vna::CommandFailedEvent &) override
    {
    }

    bool wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return resetCompleted_; });
    }

private:
    std::shared_ptr<vna::IVnaHardware> *hardware_{nullptr};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool resetCompleted_{false};
};

TEST(VnaCallbackLifetimeTest, SessionCanBeReleasedInsideItsCallback)
{
    // 若线程自连接、订阅屏障或 owner 捕获有误，本用例会死锁或访问已释放对象。
    auto managerResult = vna::VnaHardwareManager::create(VNA_TEST_DRIVER_DIRECTORY);
    ASSERT_TRUE(managerResult.hasValue());
    auto manager = managerResult.takeValue();
    SingleDeviceDiscovery discovery;
    manager->discoverAsync(vna::DiscoveryOptions{}, discovery);
    ASSERT_TRUE(discovery.wait());
    ASSERT_FALSE(discovery.failed());
    auto hardwareResult = manager->open(discovery.deviceId());
    ASSERT_TRUE(hardwareResult.hasValue());
    auto hardware = hardwareResult.takeValue();
    ResetOnConnectSink sink(hardware);
    // subscription 保持到回调完成，确保测试的释放对象只有 session 而不是 sink。
    auto subscription = hardware->subscribe(sink);
    hardware->connectAsync();
    ASSERT_TRUE(sink.wait());
    EXPECT_EQ(hardware, nullptr);
    // session 已在回调内销毁，manager 仍必须能够幂等完成全局关闭。
    manager->shutdown();
}

}  // namespace
