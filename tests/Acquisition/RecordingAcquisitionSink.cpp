#include "AcquisitionTestSupport.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace vna::test {

void CallbackBarrier::arriveAndWait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    arrived_ = true;
    changed_.notify_all();
    changed_.wait_for(lock, std::chrono::seconds(2), [this] { return released_; });
}

bool CallbackBarrier::waitUntilArrived()
{
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(2), [this] { return arrived_; });
}

void CallbackBarrier::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    changed_.notify_all();
}

void RecordingAcquisitionSink::onPrepared(
    CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome)
{
    std::function<void()> entryHook;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entryHook = std::move(preparedEntryHook_);
    }
    if (entryHook) {
        entryHook();
    }
    bool shouldThrow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        PreparedNotice notice;
        notice.commandId = commandId;
        if (outcome.hasValue()) {
            notice.prepared = outcome.value();
        } else {
            notice.error = outcome.error();
        }
        preparedNotices_.push_back(std::move(notice));
        shouldThrow = std::exchange(throwPrepared_, false);
    }
    changed_.notify_all();
    if (shouldThrow) {
        throw std::runtime_error("injected onPrepared failure");
    }
}

void RecordingAcquisitionSink::onSweepReady(std::shared_ptr<const RawSweepFrame> frame)
{
    std::function<void()> entryHook;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entryHook = std::move(frameEntryHook_);
    }
    if (entryHook) {
        entryHook();
    }
    bool shouldThrow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.push_back(std::move(frame));
        shouldThrow = std::exchange(throwFrame_, false);
    }
    changed_.notify_all();
    if (shouldThrow) {
        throw std::runtime_error("injected onSweepReady failure");
    }
}

void RecordingAcquisitionSink::onRunCompleted(const AcquisitionRunCompletion &completion)
{
    std::function<void()> entryHook;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entryHook = std::move(completionEntryHook_);
    }
    if (entryHook) {
        entryHook();
    }
    bool shouldThrow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        completions_.push_back(completion);
        shouldThrow = std::exchange(throwCompletion_, false);
    }
    changed_.notify_all();
    if (shouldThrow) {
        throw std::runtime_error("injected onRunCompleted failure");
    }
}

bool RecordingAcquisitionSink::waitForCount(NoticeKind kind, std::size_t count)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(2), [this, kind, count] {
        switch (kind) {
            case NoticeKind::Prepared:
                return preparedNotices_.size() >= count;
            case NoticeKind::Frame:
                return frames_.size() >= count;
            case NoticeKind::Completion:
                return completions_.size() >= count;
        }
        return false;
    });
}

bool RecordingAcquisitionSink::waitForPrepared(std::size_t count)
{
    return waitForCount(NoticeKind::Prepared, count);
}

bool RecordingAcquisitionSink::waitForFrames(std::size_t count)
{
    return waitForCount(NoticeKind::Frame, count);
}

bool RecordingAcquisitionSink::waitForCompletions(std::size_t count)
{
    return waitForCount(NoticeKind::Completion, count);
}

std::size_t RecordingAcquisitionSink::preparedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return preparedNotices_.size();
}

std::size_t RecordingAcquisitionSink::frameCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
}

std::size_t RecordingAcquisitionSink::completionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return completions_.size();
}

PreparedNotice RecordingAcquisitionSink::prepared(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return preparedNotices_.at(index);
}

std::shared_ptr<const RawSweepFrame> RecordingAcquisitionSink::frame(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.at(index);
}

AcquisitionRunCompletion RecordingAcquisitionSink::completion(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return completions_.at(index);
}

void RecordingAcquisitionSink::throwOnNextPrepared()
{
    std::lock_guard<std::mutex> lock(mutex_);
    throwPrepared_ = true;
}

void RecordingAcquisitionSink::throwOnNextFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    throwFrame_ = true;
}

void RecordingAcquisitionSink::throwOnNextCompletion()
{
    std::lock_guard<std::mutex> lock(mutex_);
    throwCompletion_ = true;
}

void RecordingAcquisitionSink::setPreparedEntryHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(mutex_);
    preparedEntryHook_ = std::move(hook);
}

void RecordingAcquisitionSink::setFrameEntryHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(mutex_);
    frameEntryHook_ = std::move(hook);
}

void RecordingAcquisitionSink::setCompletionEntryHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(mutex_);
    completionEntryHook_ = std::move(hook);
}

}  // namespace vna::test
