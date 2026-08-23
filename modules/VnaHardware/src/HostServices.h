#ifndef VNA_HARDWARE_HOST_SERVICES_H
#define VNA_HARDWARE_HOST_SERVICES_H

#include <atomic>
#include <cstdint>
#include <memory>

#include "VnaDriverSdk/VnaDriverApi.h"
#include "VnaBufferPool.h"

namespace vna::detail {

// 把无状态 C 回调桥接到按 SweepId 注册的缓冲池；注册表只保存扫频执行期资源。
class HostServices final {
public:
    // api() 返回进程期只读函数表，可被动态驱动保存但不可修改。
    static const VnaDriverHostApiV1 &api();
    // register/unregister 划定一个 SweepId 能够申请和提交缓冲区的唯一时间窗口。
    static bool registerSweep(
        SweepId sweepId, std::shared_ptr<VnaBufferPool> pool, const std::atomic<bool> &cancellation);
    // 注销是幂等且不抛异常的收尾操作，可从 Worker 和关闭兜底路径重复调用。
    static void unregisterSweep(SweepId sweepId) noexcept;
};

}  // namespace vna::detail

#endif
