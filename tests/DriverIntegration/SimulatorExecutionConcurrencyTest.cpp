#include "SimulatorDevice.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

class BlockingHost final {
public:
    enum class Callback { None, Acquire, Commit, FailCommit, ThrowFromAcquire, ThrowAfterAcquire };

    explicit BlockingHost(Callback callback) : callback_(callback)
    {
        active_.store(this);
    }
    ~BlockingHost()
    {
        active_.store(nullptr);
    }
    BlockingHost(const BlockingHost &) = delete;
    BlockingHost &operator=(const BlockingHost &) = delete;
    BlockingHost(BlockingHost &&) = delete;
    BlockingHost &operator=(BlockingHost &&) = delete;

    VnaDriverHostApiV1 api() const
    {
        VnaDriverHostApiV1 result{};
        result.structSize = static_cast<std::uint32_t>(sizeof(result));
        result.abiMajor = VNA_DRIVER_ABI_MAJOR_V1;
        result.abiMinor = VNA_DRIVER_ABI_MINOR_V1;
        result.Log = &log;
        result.IsCancellationRequested = &isCancellationRequested;
        result.AcquireWaveBlock = &acquireWaveBlock;
        result.CommitWaveBlock = &commitWaveBlock;
        result.DiscardWaveBlock = &discardWaveBlock;
        return result;
    }

    bool waitUntilBlocked()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return entered_; });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

    std::uint32_t commitCount() const
    {
        return commits_;
    }
    std::uint32_t discardCount() const
    {
        return discards_;
    }

private:
    static BlockingHost &active()
    {
        // 测试对象先于 Execute 线程发布，且只在 future 完成后销毁。
        return *active_.load();
    }

    static VnaStatusV1 okStatus()
    {
        VnaStatusV1 result{};
        result.code = VNA_STATUS_OK;
        return result;
    }

    static void log(std::uint32_t, VnaStringViewV1)
    {
    }
    static std::uint8_t isCancellationRequested(VnaSweepIdV1)
    {
        auto &host = active();
        if ((host.callback_ == Callback::ThrowAfterAcquire) && host.handleAcquired_) {
            host.callback_ = Callback::None;
            throw std::runtime_error("injected post-acquire failure");
        }
        return 0;
    }

    static VnaStatusV1 acquireWaveBlock(
        VnaSweepIdV1, std::uint32_t, std::uint32_t, VnaBufferHandle *handle, VnaMutableWaveBlockViewV1 *view)
    {
        auto &host = active();
        if (host.callback_ == Callback::ThrowFromAcquire) {
            host.callback_ = Callback::None;
            throw std::runtime_error("injected host failure");
        }
        host.blockIfSelected(Callback::Acquire);
        *handle = &host;
        view->structSize = static_cast<std::uint32_t>(sizeof(*view));
        view->pointCapacity = 1;
        view->waveCount = 1;
        view->values = &host.value_;
        view->qualityFlags = &host.quality_;
        host.handleAcquired_ = true;
        return okStatus();
    }

    static VnaStatusV1 commitWaveBlock(VnaBufferHandle handle, std::uint32_t, std::uint32_t)
    {
        auto &host = active();
        if (handle != &host) {
            VnaStatusV1 result{};
            result.code = VNA_STATUS_INVALID_ARGUMENT;
            return result;
        }
        host.blockIfSelected(Callback::Commit);
        if (host.callback_ == Callback::FailCommit) {
            host.callback_ = Callback::None;
            VnaStatusV1 result{};
            result.code = VNA_STATUS_DATA_LOSS;
            return result;
        }
        ++host.commits_;
        return okStatus();
    }

    static void discardWaveBlock(VnaBufferHandle handle)
    {
        auto &host = active();
        if (handle == &host) {
            ++host.discards_;
        }
    }

    void blockIfSelected(Callback callback)
    {
        if (callback_ != callback) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    inline static std::atomic<BlockingHost *> active_{nullptr};
    Callback callback_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{false};
    bool released_{false};
    bool handleAcquired_{false};
    VnaComplex64V1 value_{};
    std::uint32_t quality_{0};
    std::uint32_t commits_{0};
    std::uint32_t discards_{0};
};

struct OnePointPlan {
    OnePointPlan()
    {
        source.port = 1;
        wave.kind = VNA_WAVE_INCIDENT;
        wave.port = 1;
        excitation.excitationId = 1;
        excitation.sourceCount = 1;
        excitation.sources = &source;
        excitation.requiredWaveCount = 1;
        excitation.requiredWaves = &wave;
        point.pointId = 1;
        point.frequencyHz = 1.0e9;
        point.ifBandwidthHz = 10.0e6;
        point.excitationCount = 1;
        point.excitations = &excitation;
        plan.structSize = static_cast<std::uint32_t>(sizeof(plan));
        plan.triggerMode = VNA_TRIGGER_INTERNAL;
        plan.pointCount = 1;
        plan.points = &point;
        result.structSize = static_cast<std::uint32_t>(sizeof(result));
    }

    VnaSourcePlanV1 source{};
    VnaWaveKeyV1 wave{};
    VnaExcitationPlanViewV1 excitation{};
    VnaPointPlanViewV1 point{};
    VnaSweepPlanViewV1 plan{};
    VnaPrepareResultViewV1 result{};
};

void prepare(vna::simulator::SimulatorDevice &device, OnePointPlan &storage)
{
    ASSERT_EQ(device.connect().code, VNA_STATUS_OK);
    ASSERT_EQ(device.prepare(&storage.plan, &storage.result).code, VNA_STATUS_OK);
}

TEST(SimulatorExecutionConcurrencyTest, RejectsAbortAfterFinalCommitAdmission)
{
    BlockingHost host(BlockingHost::Callback::Commit);
    vna::simulator::SimulatorDevice device(host.api());
    OnePointPlan storage;
    ASSERT_NO_FATAL_FAILURE(prepare(device, storage));

    auto execution = std::async(std::launch::async, [&device] { return device.execute(1); });
    const bool blocked = host.waitUntilBlocked();
    EXPECT_TRUE(blocked);
    const VnaStatusV1 abortStatus = device.requestAbort(1);
    host.release();
    const VnaStatusV1 executeStatus = execution.get();

    EXPECT_EQ(abortStatus.code, VNA_STATUS_INVALID_STATE);
    EXPECT_EQ(executeStatus.code, VNA_STATUS_OK);
    EXPECT_EQ(host.commitCount(), 1U);
    EXPECT_EQ(host.discardCount(), 0U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

TEST(SimulatorExecutionConcurrencyTest, AcceptsAbortBeforeFinalCommitAdmission)
{
    BlockingHost host(BlockingHost::Callback::Acquire);
    vna::simulator::SimulatorDevice device(host.api());
    OnePointPlan storage;
    ASSERT_NO_FATAL_FAILURE(prepare(device, storage));

    auto execution = std::async(std::launch::async, [&device] { return device.execute(2); });
    const bool blocked = host.waitUntilBlocked();
    EXPECT_TRUE(blocked);
    const VnaStatusV1 abortStatus = device.requestAbort(2);
    host.release();
    const VnaStatusV1 executeStatus = execution.get();

    EXPECT_EQ(abortStatus.code, VNA_STATUS_OK);
    EXPECT_EQ(executeStatus.code, VNA_STATUS_CANCELLED);
    EXPECT_EQ(host.commitCount(), 0U);
    EXPECT_EQ(host.discardCount(), 1U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

TEST(SimulatorExecutionConcurrencyTest, CleansActiveSweepWhenExecutionThrows)
{
    BlockingHost host(BlockingHost::Callback::ThrowFromAcquire);
    vna::simulator::SimulatorDevice device(host.api());
    OnePointPlan storage;
    ASSERT_NO_FATAL_FAILURE(prepare(device, storage));

    EXPECT_THROW((void)device.execute(3), std::runtime_error);
    EXPECT_EQ(device.requestAbort(3).code, VNA_STATUS_INVALID_STATE);
    EXPECT_EQ(device.execute(4).code, VNA_STATUS_OK);

    EXPECT_EQ(host.commitCount(), 1U);
    EXPECT_EQ(host.discardCount(), 0U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

TEST(SimulatorExecutionConcurrencyTest, DiscardsAnAcquiredBlockWhenExecutionThrows)
{
    BlockingHost host(BlockingHost::Callback::ThrowAfterAcquire);
    vna::simulator::SimulatorDevice device(host.api());
    OnePointPlan storage;
    ASSERT_NO_FATAL_FAILURE(prepare(device, storage));

    EXPECT_THROW((void)device.execute(5), std::runtime_error);
    EXPECT_EQ(host.commitCount(), 0U);
    EXPECT_EQ(host.discardCount(), 1U);
    EXPECT_EQ(device.requestAbort(5).code, VNA_STATUS_INVALID_STATE);
    EXPECT_EQ(device.execute(6).code, VNA_STATUS_OK);

    EXPECT_EQ(host.commitCount(), 1U);
    EXPECT_EQ(host.discardCount(), 1U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

TEST(SimulatorExecutionConcurrencyTest, DoesNotDiscardAfterCommitReturnsAnError)
{
    BlockingHost host(BlockingHost::Callback::FailCommit);
    vna::simulator::SimulatorDevice device(host.api());
    OnePointPlan storage;
    ASSERT_NO_FATAL_FAILURE(prepare(device, storage));

    EXPECT_EQ(device.execute(7).code, VNA_STATUS_DATA_LOSS);
    EXPECT_EQ(host.commitCount(), 0U);
    EXPECT_EQ(host.discardCount(), 0U);
    EXPECT_EQ(device.execute(8).code, VNA_STATUS_OK);

    EXPECT_EQ(host.commitCount(), 1U);
    EXPECT_EQ(host.discardCount(), 0U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

}  // namespace
