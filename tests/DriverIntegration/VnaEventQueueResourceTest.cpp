#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "VnaEventDispatcher.h"
#include "VnaEventQueue.h"

namespace {

class SelfRemovingSink final : public vna::IVnaHardwareEventSink {
public:
    explicit SelfRemovingSink(vna::Subscription &subscription) : subscription_(&subscription)
    {
    }

    void onStateChanged(const vna::StateChangedEvent &) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return removalAllowed_; });
        lock.unlock();
        *subscription_ = vna::Subscription{};
    }
    void onConnectCompleted(const vna::ConnectCompletedEvent &) override
    {
    }
    void onPrepareCompleted(const vna::PrepareCompletedEvent &) override
    {
    }
    void onStartCompleted(const vna::StartCompletedEvent &) override
    {
    }
    void onAbortCompleted(const vna::AbortCompletedEvent &) override
    {
    }
    void onDisconnectCompleted(const vna::DisconnectCompletedEvent &) override
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

    bool waitUntilEntered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(1), [this] { return entered_; });
    }

    void allowRemoval()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removalAllowed_ = true;
        changed_.notify_all();
    }

private:
    vna::Subscription *subscription_{nullptr};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{false};
    bool removalAllowed_{false};
};

bool waitUntilExpired(const std::weak_ptr<const vna::PreparedSweep> &snapshot)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!snapshot.expired() && (std::chrono::steady_clock::now() < deadline)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return snapshot.expired();
}

TEST(VnaEventQueueResourceTest, RetainsPreparedBudgetUntilCallbackLifetimeEnds)
{
    vna::detail::EventQueue queue(2, 10);
    vna::detail::QueuedEvent first;
    first.retainedPayloadBytes = 10;
    ASSERT_TRUE(queue.push(std::move(first)));

    vna::detail::QueuedEvent delivered;
    ASSERT_TRUE(queue.pop(delivered));

    vna::detail::QueuedEvent blocked;
    blocked.retainedPayloadBytes = 1;
    EXPECT_FALSE(queue.push(std::move(blocked), std::chrono::milliseconds(0)));

    queue.releaseRetainedPayload(delivered.retainedPayloadBytes);
    vna::detail::QueuedEvent admitted;
    admitted.retainedPayloadBytes = 1;
    EXPECT_TRUE(queue.push(std::move(admitted), std::chrono::milliseconds(0)));

    ASSERT_TRUE(queue.pop(delivered));
    queue.releaseRetainedPayload(delivered.retainedPayloadBytes);
    queue.close();
}

TEST(VnaEventQueueResourceTest, RejectsSinglePayloadAboveBudget)
{
    vna::detail::EventQueue queue(1, 10);
    vna::detail::QueuedEvent event;
    event.retainedPayloadBytes = 11;

    EXPECT_FALSE(queue.push(std::move(event), std::chrono::milliseconds(1)));
    queue.close();
}

TEST(VnaEventQueueResourceTest, DroppedEventReleasesSnapshotWithoutWaitingForAnotherEvent)
{
    auto dispatcher = vna::detail::VnaEventDispatcher::create(4, 1);
    vna::Subscription subscription;
    SelfRemovingSink sink(subscription);
    subscription = dispatcher->subscribe(sink);
    dispatcher->postStateChanged(vna::DeviceState::Idle, vna::DeviceState::Preparing);
    const bool entered = sink.waitUntilEntered();
    EXPECT_TRUE(entered);
    if (!entered) {
        sink.allowRemoval();
        dispatcher->shutdown();
        return;
    }

    auto prepared = std::make_shared<const vna::PreparedSweep>();
    std::weak_ptr<const vna::PreparedSweep> retained = prepared;
    dispatcher->postPrepareCompleted(1, std::move(prepared), 1);
    sink.allowRemoval();

    EXPECT_TRUE(waitUntilExpired(retained));
    dispatcher->shutdown();
}

}  // namespace
