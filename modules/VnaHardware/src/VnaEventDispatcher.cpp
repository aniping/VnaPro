#include "VnaEventDispatcher.h"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "VnaEventQueue.h"
#include "VnaLogger.h"

namespace vna::detail {

class VnaEventDispatcher::Impl final {
public:
    explicit Impl(std::size_t capacity) : queue_(capacity)
    {
    }
    ~Impl()
    {
        shutdown();
    }

    void start(std::shared_ptr<VnaEventDispatcher> owner)
    {
        // 工作线程持有分发器自身，确保从最后一个外部引用析构时线程仍有完整上下文。
        thread_ = std::thread([this, owner = std::move(owner)] {
            static_cast<void>(owner);
            run();
        });
    }

    std::uint64_t add(IVnaHardwareEventSink &sink)
    {
        std::lock_guard<std::mutex> lock(subscriberMutex_);
        // 波数据块是移动专属租约，V1 明确采用单订阅者，避免隐式复制或广播语义。
        if (subscriber_) {
            throw std::logic_error("VNA session supports one event subscriber");
        }
        subscriber_ = std::make_shared<Subscriber>(nextIdentifier_++, sink);
        return subscriber_->identifier();
    }

    void remove(std::uint64_t identifier)
    {
        std::shared_ptr<Subscriber> removed;
        {
            std::lock_guard<std::mutex> lock(subscriberMutex_);
            if (subscriber_ && (subscriber_->identifier() == identifier)) {
                removed = std::move(subscriber_);
            }
        }
        // deactivate 可能等待正在执行的回调，不能在持有 subscriberMutex_ 时调用。
        if (removed) {
            removed->deactivate();
        }
    }

    bool postControl(QueuedEvent event)
    {
        // 控制事件不能静默丢弃，因此使用队列的阻塞入队路径。
        if (!bindToCurrentSubscriber(event)) {
            return false;
        }
        return queue_.push(std::move(event));
    }

    bool postData(QueuedEvent event, std::chrono::milliseconds timeout)
    {
        // 数据事件仅有界等待；超时会沿 commit 返回 DATA_LOSS，进而终止本次扫描。
        if (!bindToCurrentSubscriber(event)) {
            return false;
        }
        return queue_.push(std::move(event), timeout);
    }

    void shutdown()
    {
        // 先关闭队列唤醒 pop，再等待线程，最后停用订阅者，形成明确的退出顺序。
        queue_.close();
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id()) {
                thread_.detach();
            } else {
                thread_.join();
            }
        }
        std::shared_ptr<Subscriber> removed;
        {
            std::lock_guard<std::mutex> lock(subscriberMutex_);
            removed = std::move(subscriber_);
        }
        if (removed) {
            removed->deactivate();
        }
    }

private:
    bool bindToCurrentSubscriber(QueuedEvent &event)
    {
        std::lock_guard<std::mutex> lock(subscriberMutex_);
        if (!subscriber_) {
            return false;
        }
        // 事件记录订阅代次，退订后积压的旧事件不会误投给后来注册的新对象。
        event.subscriptionId = subscriber_->identifier();
        return true;
    }

    std::shared_ptr<Subscriber> subscriberFor(const QueuedEvent &event)
    {
        std::lock_guard<std::mutex> lock(subscriberMutex_);
        if (subscriber_ && (subscriber_->identifier() == event.subscriptionId)) {
            return subscriber_;
        }
        return {};
    }

    static void deliver(const std::shared_ptr<Subscriber> &subscriber, QueuedEvent event)
    {
        // begin/endCall 构成回调屏障；任何异常都被截断在 SDK 边界并停用该订阅者。
        IVnaHardwareEventSink *sink = subscriber->beginCall();
        if (sink == nullptr) {
            return;
        }
        try {
            dispatchEvent(*sink, std::move(event));
            subscriber->endCall();
        } catch (...) {
            subscriber->endCall();
            subscriber->deactivate();
            VnaLogger::write(VnaLogLevel::Error, "硬件事件回调抛出异常，订阅已停用");
        }
    }

    void run()
    {
        QueuedEvent event;
        while (queue_.pop(event)) {
            const auto subscriber = subscriberFor(event);
            if (subscriber) {
                // 回调期间不持有订阅锁，允许回调安全地销毁自身订阅。
                deliver(subscriber, std::move(event));
            }
        }
    }

    EventQueue queue_;
    std::thread thread_;
    std::mutex subscriberMutex_;
    std::shared_ptr<Subscriber> subscriber_;
    std::uint64_t nextIdentifier_{1};
};

std::shared_ptr<VnaEventDispatcher> VnaEventDispatcher::create(std::size_t queueCapacity)
{
    // 构造完成 shared_ptr 后再启动线程，线程捕获 owner 时 shared_from_this 关系才有效。
    auto dispatcher =
        std::shared_ptr<VnaEventDispatcher>(new VnaEventDispatcher(std::make_unique<Impl>(queueCapacity)));
    dispatcher->implementation_->start(dispatcher);
    return dispatcher;
}

VnaEventDispatcher::VnaEventDispatcher(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation))
{
}

VnaEventDispatcher::~VnaEventDispatcher() = default;

Subscription VnaEventDispatcher::subscribe(IVnaHardwareEventSink &sink)
{
    // Subscription 是 RAII 退订句柄，其析构会通过 remove 建立同步回调屏障。
    return Subscription(shared_from_this(), implementation_->add(sink));
}

void VnaEventDispatcher::remove(std::uint64_t subscriptionId)
{
    implementation_->remove(subscriptionId);
}

void VnaEventDispatcher::postStateChanged(DeviceState previous, DeviceState current)
{
    QueuedEvent event;
    event.kind = EventKind::StateChanged;
    event.stateChanged = StateChangedEvent{previous, current};
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postConnectCompleted(CommandId commandId)
{
    QueuedEvent event;
    event.kind = EventKind::ConnectCompleted;
    event.connected.commandId = commandId;
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postPrepareCompleted(CommandId commandId, PreparedSweep prepared)
{
    QueuedEvent event;
    event.kind = EventKind::PrepareCompleted;
    event.prepared.commandId = commandId;
    event.prepared.prepared = std::move(prepared);
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postStartCompleted(CommandId commandId, SweepId sweepId)
{
    QueuedEvent event;
    event.kind = EventKind::StartCompleted;
    event.started = StartCompletedEvent{commandId, sweepId};
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postAbortCompleted(CommandId commandId, SweepId sweepId)
{
    QueuedEvent event;
    event.kind = EventKind::AbortCompleted;
    event.aborted = AbortCompletedEvent{commandId, sweepId};
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postDisconnectCompleted(CommandId commandId)
{
    QueuedEvent event;
    event.kind = EventKind::DisconnectCompleted;
    event.disconnected.commandId = commandId;
    implementation_->postControl(std::move(event));
}

bool VnaEventDispatcher::postWaveBlock(VnaWaveBlock block)
{
    QueuedEvent event;
    event.kind = EventKind::WaveBlock;
    event.block = std::move(block);
    return implementation_->postData(std::move(event), std::chrono::milliseconds(250));
}

void VnaEventDispatcher::postSweepCompleted(SweepCompletion completion)
{
    QueuedEvent event;
    event.kind = EventKind::SweepCompleted;
    event.completed = completion;
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::postCommandFailed(CommandId commandId, VnaError error)
{
    QueuedEvent event;
    event.kind = EventKind::CommandFailed;
    event.failed.commandId = commandId;
    event.failed.error = std::move(error);
    implementation_->postControl(std::move(event));
}

void VnaEventDispatcher::shutdown()
{
    implementation_->shutdown();
}

}  // namespace vna::detail
