#include "VnaIntegrationTestSupport.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

namespace vna::test {

VnaSweepPlan makePlan()
{
    // 五个点故意超过批大小 2，用一个计划同时覆盖完整批和尾部短批。
    VnaSweepPlan plan;
    for (std::uint32_t index = 0; index < 5; ++index) {
        VnaExcitationPlan excitation;
        excitation.excitationId = 1;
        excitation.sources.push_back(VnaSourcePlan{1, 0.0, 0.0});
        excitation.requiredWaves.push_back(VnaWaveKey{WaveKind::Incident, 1});
        excitation.requiredWaves.push_back(VnaWaveKey{WaveKind::Outgoing, 2});

        VnaPointPlan point;
        point.pointId = index + 1;
        point.frequencyHz = 1.0e9 + (static_cast<double>(index) * 1.0e6);
        point.ifBandwidthHz = 1000.0 + static_cast<double>(index);
        point.dwellTime = std::chrono::microseconds(10);
        point.excitations.push_back(std::move(excitation));
        plan.points.push_back(std::move(point));
    }
    return plan;
}

void verifyCompletedSweep(const HardwareSink &events)
{
    EXPECT_EQ(events.completion(0).reason, SweepTerminationReason::Completed);
    // 五个点按批次容量 2 被拆为 2、2、1 三个连续块。
    ASSERT_EQ(events.blockCount(), 3U);
    EXPECT_EQ(events.blockSequence(0), 0U);
    EXPECT_EQ(events.blockSequence(1), 1U);
    EXPECT_EQ(events.blockSequence(2), 2U);
    EXPECT_EQ(events.blockPointCount(0), 2U);
    EXPECT_EQ(events.blockPointCount(2), 1U);

    // 0 dB 源的入射波接近单位幅度，但接收机噪声会让每个 Sweep 的复值发生微小变化。
    const auto incident = events.blockValue(0, 0, 0);
    EXPECT_TRUE(std::isfinite(incident.real()));
    EXPECT_TRUE(std::isfinite(incident.imag()));
    EXPECT_NEAR(std::abs(incident), 1.0, 2.0e-3);
    // 质量位零表示有效样本，确保模拟器没有把默认 Missing 标志泄漏给已填充数据。
    EXPECT_EQ(events.blockQuality(0, 0, 0), 0U);
    // 内建 DUT 的不同端口彼此开路；跨端口只保留有限隔离泄漏和随 IFBW 变化的底噪。
    const auto isolated = events.blockValue(0, 1, 0);
    EXPECT_TRUE(std::isfinite(isolated.real()));
    EXPECT_TRUE(std::isfinite(isolated.imag()));
    EXPECT_LT(std::abs(isolated), 1.0e-3);
    // 尾块只有一点，专门验证第二个波使用本次 Acquire 容量作跨距。
    EXPECT_EQ(events.blockQuality(2, 1, 0), 0U);
    EXPECT_LT(std::abs(events.blockValue(2, 1, 0)), 1.0e-3);
}

}  // namespace vna::test
