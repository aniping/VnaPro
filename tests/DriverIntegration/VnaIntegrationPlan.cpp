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

    // 入射波幅度为 1；跨一个端口的出射波使用 0.5 增益和确定性传播相位。
    EXPECT_NEAR(events.blockValue(0, 0, 0).real(), 1.0, 1.0e-12);
    EXPECT_NEAR(events.blockValue(0, 0, 0).imag(), 0.0, 1.0e-12);
    // 质量位零表示有效样本，确保模拟器没有把默认 Missing 标志泄漏给已填充数据。
    EXPECT_EQ(events.blockQuality(0, 0, 0), 0U);
    constexpr double pi = 3.14159265358979323846;
    EXPECT_NEAR(events.blockValue(0, 1, 0).real(), 0.5 * std::cos(-0.2 * pi), 1.0e-12);
    // 尾块只有一点，专门验证第二个波使用本次 Acquire 容量作跨距。
    EXPECT_EQ(events.blockQuality(2, 1, 0), 0U);
    EXPECT_NEAR(events.blockValue(2, 1, 0).real(), 0.5 * std::cos(-0.2008 * pi), 1.0e-12);
}

}  // namespace vna::test
