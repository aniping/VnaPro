#include "HostServices.h"

#include <iterator>
#include <map>
#include <mutex>
#include <string>

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

    void logMessage(std::uint32_t, VnaStringViewV1) noexcept
    {
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

    VnaStatusV1 acquireBlock(
        VnaSweepIdV1 sweepId, std::uint32_t layoutId, std::uint32_t pointCapacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view) noexcept
    {
        try {
            std::shared_ptr<VnaBufferPool> pool;
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
            }
            // 不持有全局注册表锁等待缓冲槽，避免背压反向阻塞取消和注销。
            const VnaStatusV1 result = pool->acquire(sweepId, layoutId, pointCapacity, handle, view);
            if (result.code == VNA_STATUS_OK) {
                std::lock_guard<std::mutex> lock(registry().mutex);
                // 记录裸 handle 的 SweepId 所有权，后续 Commit/Discard 才能验证来源。
                registry().handles[*handle] = sweepId;
            }
            return result;
        } catch (...) {
            return status(VNA_STATUS_INTERNAL_ERROR, "host acquire failed");
        }
    }

    VnaStatusV1 commitBlock(VnaBufferHandle handle, std::uint32_t firstPointIndex, std::uint32_t pointCount) noexcept
    {
        try {
            std::shared_ptr<VnaBufferPool> pool;
            std::uint64_t sequence = 0;
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
                sequence = context->second.nextSequence++;
                registry().handles.erase(owner);
            }
            // 先移除裸句柄再发布波块，使重复 Commit 必然失败。
            const VnaStatusV1 result = pool->commit(handle, firstPointIndex, pointCount, sequence);
            if (result.code != VNA_STATUS_OK) {
                pool->discard(handle);
            }
            return result;
        } catch (...) {
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

void HostServices::unregisterSweep(SweepId sweepId)
{
    std::lock_guard<std::mutex> lock(registry().mutex);
    // 先移除所有尚未归还的裸句柄，再移除上下文，迟到操作只能得到“未知句柄”。
    for (auto handle = registry().handles.begin(); handle != registry().handles.end();) {
        handle = handle->second == sweepId ? registry().handles.erase(handle) : std::next(handle);
    }
    registry().sweeps.erase(sweepId);
}

}  // namespace vna::detail
