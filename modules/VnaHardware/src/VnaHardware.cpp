#include "VnaHardware.h"
#include "VnaHardwareManager.h"

#include <utility>

#include "VnaEventDispatcher.h"

namespace vna {

IVnaHardware::~IVnaHardware() = default;
IVnaHardwareEventSink::~IVnaHardwareEventSink() = default;
IVnaDiscoverySink::~IVnaDiscoverySink() = default;

Subscription::Subscription() = default;
Subscription::~Subscription()
{
    // RAII 句柄默认在析构时同步退订，避免 sink 先析构而队列仍持有其裸指针。
    reset();
}

Subscription::Subscription(std::weak_ptr<detail::VnaEventDispatcher> dispatcher, std::uint64_t id)
    : dispatcher_(std::move(dispatcher)), id_(id)
{
}

Subscription::Subscription(Subscription &&other) noexcept : dispatcher_(std::move(other.dispatcher_)), id_(other.id_)
{
    // 订阅只能移动；清空源 id 保证两次析构不会重复 remove。
    other.id_ = 0;
}

Subscription &Subscription::operator=(Subscription &&other) noexcept
{
    if (this != &other) {
        // 接管新订阅前先同步撤销当前订阅，避免短暂同时绑定两个 sink 生命周期。
        reset();
        dispatcher_ = std::move(other.dispatcher_);
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void Subscription::reset()
{
    if (id_ == 0) {
        return;
    }
    // 分发器可能已随会话关闭；weak_ptr 失效时无需再访问任何内部状态。
    if (const auto dispatcher = dispatcher_.lock()) {
        dispatcher->remove(id_);
    }
    id_ = 0;
    dispatcher_.reset();
}

}  // namespace vna
