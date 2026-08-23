#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "HostServices.h"
#include "VnaBufferPool.h"

namespace {

VnaStatusV1 acceptBlock(vna::VnaWaveBlock)
{
    VnaStatusV1 result{};
    result.code = VNA_STATUS_OK;
    return result;
}

std::shared_ptr<vna::detail::VnaBufferPool> singleSlotPool()
{
    std::vector<vna::VnaWaveLayout> layouts{vna::VnaWaveLayout{7, 1, {vna::VnaWaveKey{vna::WaveKind::Incident, 1}}}};
    return vna::detail::VnaBufferPool::create(layouts, 1, 1, &acceptBlock);
}

class SweepRegistration final {
public:
    SweepRegistration(
        vna::SweepId sweepId, const std::shared_ptr<vna::detail::VnaBufferPool> &pool,
        const std::atomic<bool> &cancellation)
        : sweepId_(sweepId), registered_(vna::detail::HostServices::registerSweep(sweepId, pool, cancellation))
    {
    }

    ~SweepRegistration()
    {
        if (registered_) {
            vna::detail::HostServices::unregisterSweep(sweepId_);
        }
    }

    SweepRegistration(const SweepRegistration &) = delete;
    SweepRegistration &operator=(const SweepRegistration &) = delete;
    SweepRegistration(SweepRegistration &&) = delete;
    SweepRegistration &operator=(SweepRegistration &&) = delete;

    bool registered() const
    {
        return registered_;
    }

    void unregister()
    {
        if (registered_) {
            vna::detail::HostServices::unregisterSweep(sweepId_);
            registered_ = false;
        }
    }

private:
    vna::SweepId sweepId_{0};
    bool registered_{false};
};

VnaStatusV1 acquire(vna::SweepId sweepId, VnaBufferHandle &handle)
{
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));
    return vna::detail::HostServices::api().AcquireWaveBlock(sweepId, 7, 1, &handle, &view);
}

TEST(VnaHostServicesResourceTest, UnregisterReturnsEveryAcquiredHandleToPool)
{
    constexpr vna::SweepId firstSweep = UINT64_C(0x7f000001);
    constexpr vna::SweepId secondSweep = UINT64_C(0x7f000002);
    const auto pool = singleSlotPool();
    std::atomic<bool> cancellation{false};
    SweepRegistration first(firstSweep, pool, cancellation);
    ASSERT_TRUE(first.registered());

    VnaBufferHandle abandoned = nullptr;
    ASSERT_EQ(acquire(firstSweep, abandoned).code, VNA_STATUS_OK);
    ASSERT_NE(abandoned, nullptr);
    first.unregister();
    EXPECT_EQ(vna::detail::HostServices::api().CommitWaveBlock(abandoned, 0, 1).code, VNA_STATUS_INVALID_ARGUMENT);

    SweepRegistration second(secondSweep, pool, cancellation);
    ASSERT_TRUE(second.registered());
    VnaBufferHandle reused = nullptr;
    ASSERT_EQ(acquire(secondSweep, reused).code, VNA_STATUS_OK);
    ASSERT_NE(reused, nullptr);
    vna::detail::HostServices::api().DiscardWaveBlock(reused);
}

}  // namespace
