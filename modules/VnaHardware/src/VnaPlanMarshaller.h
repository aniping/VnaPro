#ifndef VNA_HARDWARE_VNA_PLAN_MARSHALLER_H
#define VNA_HARDWARE_VNA_PLAN_MARSHALLER_H

#include <vector>

#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaSweepPlan.h"

namespace vna::detail {

// 拥有所有 ABI 数组的底层存储，保证传给 Prepare 的嵌套视图在调用期间地址稳定。
class VnaPlanMarshaller final {
public:
    explicit VnaPlanMarshaller(const VnaSweepPlan &plan);
    const VnaSweepPlanViewV1 &view() const;

private:
    struct ExcitationStorage {
        // 叶子 vector 真正拥有内存，上层 View 只保存这些 vector 的 data 指针。
        std::vector<VnaSourcePlanV1> sources;
        std::vector<VnaWaveKeyV1> waves;
    };

    static void buildExcitationStorage(const VnaExcitationPlan &source, ExcitationStorage &destination);
    void buildPointStorage(const VnaSweepPlan &plan);
    void buildViews(const VnaSweepPlan &plan);
    static std::uint32_t policyFlags(const SweepPolicy &policy);

    // 成员按“所有者 -> 子视图 -> 根视图”排列，析构时借用者先于所有者消失。
    std::vector<std::vector<ExcitationStorage>> storage_;
    std::vector<std::vector<VnaExcitationPlanViewV1>> excitationViews_;
    std::vector<VnaPointPlanViewV1> pointViews_;
    VnaSweepPlanViewV1 view_{};
};

}  // namespace vna::detail

#endif
