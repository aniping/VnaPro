#include "VnaEventQueue.h"

#include <utility>

namespace vna::detail {

EventQueue::EventQueue(std::size_t capacity) : slots_(capacity)
{
    // 容量在构造后固定，环形槽位避免事件高频流中反复分配队列节点。
}

bool EventQueue::push(QueuedEvent event)
{
    // 控制事件使用无超时反压：只要队列未关闭，就等待消费者腾出空间。
    std::unique_lock<std::mutex> lock(mutex_);
    writable_.wait(lock, [this] { return closed_ || (count_ < slots_.size()); });
    if (closed_) {
        return false;
    }
    slots_[tail_].emplace(std::move(event));
    tail_ = (tail_ + 1) % slots_.size();
    ++count_;
    readable_.notify_one();
    return true;
}

bool EventQueue::push(QueuedEvent event, std::chrono::milliseconds timeout)
{
    // 数据事件采用有界等待，让上游能把拥塞转换为明确的 DATA_LOSS。
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = writable_.wait_for(lock, timeout, [this] { return closed_ || (count_ < slots_.size()); });
    if (!ready || closed_) {
        return false;
    }
    slots_[tail_].emplace(std::move(event));
    tail_ = (tail_ + 1) % slots_.size();
    ++count_;
    readable_.notify_one();
    return true;
}

bool EventQueue::pop(QueuedEvent &event)
{
    std::unique_lock<std::mutex> lock(mutex_);
    readable_.wait(lock, [this] { return closed_ || (count_ != 0); });
    // 关闭后仍会排空已有事件；只有“已关闭且为空”才结束消费线程。
    if (count_ == 0) {
        return false;
    }
    event = std::move(*slots_[head_]);
    slots_[head_].reset();
    head_ = (head_ + 1) % slots_.size();
    --count_;
    writable_.notify_one();
    return true;
}

void EventQueue::close()
{
    // 同时唤醒生产者和消费者，使所有阻塞操作都能观察 closed_ 并退出。
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    readable_.notify_all();
    writable_.notify_all();
}

Subscriber::Subscriber(std::uint64_t identifier, IVnaHardwareEventSink &sink) : identifier_(identifier), sink_(&sink)
{
}

std::uint64_t Subscriber::identifier() const
{
    return identifier_;
}

IVnaHardwareEventSink *Subscriber::beginCall()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        return nullptr;
    }
    // inFlight_ 是退订同步屏障的计数；记录线程用于识别回调内自退订。
    ++inFlight_;
    callbackThread_ = std::this_thread::get_id();
    return sink_;
}

void Subscriber::endCall()
{
    // 每次 beginCall 成功后必须恰好调用一次，归零时唤醒等待析构的订阅句柄。
    std::lock_guard<std::mutex> lock(mutex_);
    --inFlight_;
    callbackThread_ = {};
    idle_.notify_all();
}

void Subscriber::deactivate()
{
    std::unique_lock<std::mutex> lock(mutex_);
    active_ = false;
    sink_ = nullptr;
    // 回调线程不能等待自己结束；先清空 sink，返回后 endCall 会完成剩余收尾。
    if (callbackThread_ == std::this_thread::get_id()) {
        return;
    }
    idle_.wait(lock, [this] { return inFlight_ == 0; });
}

void dispatchEvent(IVnaHardwareEventSink &sink, QueuedEvent event)
{
    // 保持事件种类的穷举分派；这里的分支数量来自公开协议，而非业务流程嵌套。
    switch (event.kind) {
        case EventKind::StateChanged:
            sink.onStateChanged(event.stateChanged);
            break;
        case EventKind::ConnectCompleted:
            sink.onConnectCompleted(event.connected);
            break;
        case EventKind::PrepareCompleted:
            sink.onPrepareCompleted(event.prepared);
            break;
        case EventKind::StartCompleted:
            sink.onStartCompleted(event.started);
            break;
        case EventKind::WaveBlock:
            sink.onWaveBlock(std::move(event.block));
            break;
        case EventKind::SweepCompleted:
            sink.onSweepCompleted(event.completed);
            break;
        case EventKind::CommandFailed:
            sink.onCommandFailed(event.failed);
            break;
    }
}

}  // namespace vna::detail
