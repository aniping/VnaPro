#include "HostServices.h"

#include <map>
#include <limits>
#include <mutex>
#include <string>

#include "VnaLogger.h"

namespace vna::detail {
namespace {

    struct SweepContext {
        // cancellation 指向会话成员；注册表项会在会话销毁该成员之前注销。
        std::shared_ptr<VnaBufferPool> pool;
        const std::atomic<bool> *cancellation{nullptr};
        std::uint64_t nextSequence{0};
    };

    struct Registry {
        // handles 记录 Acquire 后尚未 Commit/Discard 的所有权，拒绝伪造或重复提交。
        std::mutex mutex;
        std::map<SweepId, SweepContext> sweeps;
        std::map<VnaBufferHandle, SweepId> handles;
    };

    Registry &registry()
    {
        // 函数局部静态对象保证线程安全初始化，并让 C 回调无需携带宿主 context 指针。
        static Registry instance;
        return instance;
    }

    VnaStatusV1 status(std::uint32_t code, const char *message)
    {
        // message 均为静态字面量，C 视图在驱动读取期间始终有效。
        VnaStatusV1 result{};
        result.code = code;
        result.message = VnaStringViewV1{message, std::char_traits<char>::length(message)};
        return result;
    }

    void logMessage(std::uint32_t level, VnaStringViewV1 message) noexcept
    {
        // 驱动日志先做长度和空指针校验，再复制到宿主日志后端，绝不保留 ABI 视图。
        VnaLogger::writeDriver(level, message);
    }

    std::uint8_t isCancellationRequested(VnaSweepIdV1 sweepId) noexcept
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        const auto context = registry().sweeps.find(sweepId);
        // 未注册也视为取消，防止注销后的迟到驱动线程继续产生数据。
        if (context == registry().sweeps.end()) {
            return 1;
        }
        return context->second.cancellation->load() ? 1 : 0;
    }

    VnaStatusV1 findActivePool(VnaSweepIdV1 sweepId, std::shared_ptr<VnaBufferPool> &pool)
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        const auto context = registry().sweeps.find(sweepId);
        if (context == registry().sweeps.end()) {
            return status(VNA_STATUS_INVALID_STATE, "sweep is not active");
        }
        if (context->second.cancellation->load()) {
            return status(VNA_STATUS_CANCELLED, "sweep cancellation requested");
        }
        pool = context->second.pool;
        return status(VNA_STATUS_OK, "");
    }

    VnaStatusV1 registerAcquiredHandle(
        VnaSweepIdV1 sweepId, const std::shared_ptr<VnaBufferPool> &pool, VnaBufferHandle handle)
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        const auto context = registry().sweeps.find(sweepId);
        if ((context == registry().sweeps.end()) || (context->second.pool != pool)) {
            return status(VNA_STATUS_INVALID_STATE, "sweep ended while acquiring a wave block");
        }
        if (context->second.cancellation->load()) {
            return status(VNA_STATUS_CANCELLED, "sweep cancellation requested");
        }
        if (!registry().handles.emplace(handle, sweepId).second) {
            return status(VNA_STATUS_INVALID_ARGUMENT, "buffer handle is already registered");
        }
        return status(VNA_STATUS_OK, "");
    }

    void rollbackAcquiredBlock(
        const std::shared_ptr<VnaBufferPool> &pool, VnaBufferHandle acquiredHandle, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view) noexcept
    {
        if (pool && (acquiredHandle != nullptr)) {
            pool->discard(acquiredHandle);
        }
        if (handle != nullptr) {
            *handle = nullptr;
        }
        if (view != nullptr) {
            *view = {};
        }
    }

    VnaStatusV1 acquireBlock(
        VnaSweepIdV1 sweepId, std::uint32_t layoutId, std::uint32_t pointCapacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view) noexcept
    {
        std::shared_ptr<VnaBufferPool> pool;
        VnaBufferHandle acquiredHandle = nullptr;
        try {
            const VnaStatusV1 route = findActivePool(sweepId, pool);
            if (route.code != VNA_STATUS_OK) {
                return route;
            }
            // 不持有全局注册表锁等待缓冲槽，避免背压反向阻塞取消和注销。
            const VnaStatusV1 result = pool->acquire(sweepId, layoutId, pointCapacity, handle, view);
            if (result.code != VNA_STATUS_OK) {
                return result;
            }
            acquiredHandle = *handle;
            // Acquire 可能在等待槽位期间与注销交错，返回后必须重新验证同一条路由。
            const VnaStatusV1 registration = registerAcquiredHandle(sweepId, pool, acquiredHandle);
            if (registration.code != VNA_STATUS_OK) {
                rollbackAcquiredBlock(pool, acquiredHandle, handle, view);
                return registration;
            }
            acquiredHandle = nullptr;
            return result;
        } catch (...) {
            // Acquire 已取得槽位后，handles map 分配失败也必须归还租约。
            rollbackAcquiredBlock(pool, acquiredHandle, handle, view);
            return status(VNA_STATUS_INTERNAL_ERROR, "host acquire failed");
        }
    }

    VnaStatusV1 commitBlock(VnaBufferHandle handle, std::uint32_t firstPointIndex, std::uint32_t pointCount) noexcept
    {
        std::shared_ptr<VnaBufferPool> pool;
        bool ownershipRemoved = false;
        try {
            std::uint64_t sequence = 0;
            bool sequenceExhausted = false;
            {
                std::lock_guard<std::mutex> lock(registry().mutex);
                const auto owner = registry().handles.find(handle);
                if (owner == registry().handles.end()) {
                    return status(VNA_STATUS_INVALID_ARGUMENT, "unknown buffer handle");
                }
                auto context = registry().sweeps.find(owner->second);
                if (context == registry().sweeps.end()) {
                    return status(VNA_STATUS_INVALID_STATE, "sweep is not active");
                }
                pool = context->second.pool;
                sequenceExhausted = context->second.nextSequence == std::numeric_limits<std::uint64_t>::max();
                if (!sequenceExhausted) {
                    sequence = context->second.nextSequence++;
                }
                registry().handles.erase(owner);
                ownershipRemoved = true;
            }
            if (sequenceExhausted) {
                pool->discard(handle);
                return status(VNA_STATUS_DATA_LOSS, "wave block sequence is exhausted");
            }
            // 先移除裸句柄再发布波块，使重复 Commit 必然失败。
            const VnaStatusV1 result = pool->commit(handle, firstPointIndex, pointCount, sequence);
            if (result.code != VNA_STATUS_OK) {
                pool->discard(handle);
            }
            return result;
        } catch (...) {
            // 注册表已经交出裸句柄所有权但 Commit 尚未完成时，必须回收 Acquired 槽位。
            if (pool && ownershipRemoved) {
                pool->discard(handle);
            }
            return status(VNA_STATUS_INTERNAL_ERROR, "host commit failed");
        }
    }

    void discardBlock(VnaBufferHandle handle) noexcept
    {
        try {
            std::shared_ptr<VnaBufferPool> pool;
            {
                std::lock_guard<std::mutex> lock(registry().mutex);
                const auto owner = registry().handles.find(handle);
                if (owner == registry().handles.end()) {
                    return;
                }
                const auto context = registry().sweeps.find(owner->second);
                if (context != registry().sweeps.end()) {
                    pool = context->second.pool;
                }
                registry().handles.erase(owner);
            }
            // 扫描可能已注销；此时句柄登记已删除，槽位随 pool 的剩余所有权统一回收。
            if (pool) {
                pool->discard(handle);
            }
        } catch (...) {
        }
    }

}  // namespace

const VnaDriverHostApiV1 &HostServices::api()
{
    // 不可变函数表只初始化一次，地址可安全交给所有同时加载的驱动模块。
    static const VnaDriverHostApiV1 result = [] {
        VnaDriverHostApiV1 value{};
        value.structSize = static_cast<std::uint32_t>(sizeof(value));
        value.abiMajor = VNA_DRIVER_ABI_MAJOR_V1;
        value.abiMinor = VNA_DRIVER_ABI_MINOR_V1;
        value.Log = &logMessage;
        value.IsCancellationRequested = &isCancellationRequested;
        value.AcquireWaveBlock = &acquireBlock;
        value.CommitWaveBlock = &commitBlock;
        value.DiscardWaveBlock = &discardBlock;
        return value;
    }();
    return result;
}

bool HostServices::registerSweep(
    SweepId sweepId, std::shared_ptr<VnaBufferPool> pool, const std::atomic<bool> &cancellation)
{
    // SweepId 是进程级路由键，重复注册意味着状态机错误，因此 emplace 失败即拒绝。
    std::lock_guard<std::mutex> lock(registry().mutex);
    return registry().sweeps.emplace(sweepId, SweepContext{std::move(pool), &cancellation, 0}).second;
}

void HostServices::unregisterSweep(SweepId sweepId) noexcept
{
    // 不能吞掉注销失败后继续析构会话，否则注册表会留下指向 cancellation_ 的悬空借用。
    // 本函数为 noexcept，极端的互斥锁故障会终止进程，不伪装成已安全收尾。
    std::lock_guard<std::mutex> lock(registry().mutex);
    const auto context = registry().sweeps.find(sweepId);
    const std::shared_ptr<VnaBufferPool> pool = context == registry().sweeps.end() ? nullptr : context->second.pool;
    // 同一锁内封闭路由和句柄，避免 Discard 在两步之间丢失 pool；pool 不反向持有 registry 锁。
    for (auto handle = registry().handles.begin(); handle != registry().handles.end();) {
        if (handle->second == sweepId) {
            if (pool) {
                pool->discard(handle->first);
            }
            handle = registry().handles.erase(handle);
        } else {
            ++handle;
        }
    }
    registry().sweeps.erase(sweepId);
}

}  // namespace vna::detail
