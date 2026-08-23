#ifndef VNA_HARDWARE_VNA_EVENT_QUEUE_H
#define VNA_HARDWARE_VNA_EVENT_QUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "VnaHardwareEvent.h"

namespace vna::detail {

enum class EventKind : std::uint8_t {
    StateChanged,
    ConnectCompleted,
    PrepareCompleted,
    StartCompleted,
    AbortCompleted,
    DisconnectCompleted,
    WaveBlock,
    SweepCompleted,
    CommandFailed
};

struct QueuedEvent {
    // subscriptionId 是入队时的订阅代次；其余载荷按 kind 选择恰好一个使用。
    EventKind kind{EventKind::StateChanged};
    std::uint64_t subscriptionId{0};
    StateChangedEvent stateChanged;
    ConnectCompletedEvent connected;
    PrepareCompletedEvent prepared;
    StartCompletedEvent started;
    AbortCompletedEvent aborted;
    DisconnectCompletedEvent disconnected;
    VnaWaveBlock block;
    SweepCompletion completed;
    CommandFailedEvent failed;
};

// 固定容量环形队列：控制事件无限等待，数据事件使用有界等待形成背压。
class EventQueue final {
public:
    explicit EventQueue(std::size_t capacity);
    bool push(QueuedEvent event);
    bool push(QueuedEvent event, std::chrono::milliseconds timeout);
    bool pop(QueuedEvent &event);
    void close();

private:
    // [head_, head_ + count_) 为可读区，tail_ 指向下一可写槽位，三者均在 mutex_ 下修改。
    std::vector<std::optional<QueuedEvent>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    bool closed_{false};
    std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
};

// 订阅代次和在途计数阻止事件投递给已销毁或被替换的 sink。
class Subscriber final {
public:
    Subscriber(std::uint64_t identifier, IVnaHardwareEventSink &sink);
    std::uint64_t identifier() const;
    IVnaHardwareEventSink *beginCall();
    void endCall();
    void deactivate();

private:
    // sink_ 是调用者拥有的裸指针，仅 active_ 且 inFlight_ 已登记时允许取出。
    std::uint64_t identifier_{0};
    IVnaHardwareEventSink *sink_{nullptr};
    bool active_{true};
    std::size_t inFlight_{0};
    // 回调线程可销毁自身订阅；这种情况下不能等待自己退出。
    std::thread::id callbackThread_;
    std::mutex mutex_;
    // deactivate 在非回调线程等待 inFlight_ 清零，构成 sink 生命周期屏障。
    std::condition_variable idle_;
};

void dispatchEvent(IVnaHardwareEventSink &sink, QueuedEvent event);

}  // namespace vna::detail

#endif
