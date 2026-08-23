#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>

#include "VnaHardwareResourceLimits.h"
#include "VnaPreparedCapacity.h"
#include "VnaWavePoolCapacity.h"

namespace {

class TemporaryConfiguration final {
public:
    explicit TemporaryConfiguration(const std::string &content)
    {
        char pattern[] = "/tmp/vnapro-resource-test-XXXXXX";
        const int descriptor = mkstemp(pattern);
        if (descriptor < 0) {
            throw std::runtime_error("cannot create temporary configuration");
        }
        try {
            path_ = pattern;
            writeContent(descriptor, content);
        } catch (...) {
            close(descriptor);
            unlink(pattern);
            throw;
        }
        if (close(descriptor) != 0) {
            unlink(path_.c_str());
            throw std::runtime_error("cannot close temporary configuration");
        }
    }

    ~TemporaryConfiguration()
    {
        unlink(path_.c_str());
    }

    TemporaryConfiguration(const TemporaryConfiguration &) = delete;
    TemporaryConfiguration &operator=(const TemporaryConfiguration &) = delete;
    TemporaryConfiguration(TemporaryConfiguration &&) = delete;
    TemporaryConfiguration &operator=(TemporaryConfiguration &&) = delete;

    const std::string &path() const
    {
        return path_;
    }

private:
    static void writeContent(int descriptor, const std::string &content)
    {
        std::size_t offset = 0;
        while (offset < content.size()) {
            const ssize_t count = write(descriptor, content.data() + offset, content.size() - offset);
            if ((count < 0) && (errno == EINTR)) {
                continue;
            }
            if (count <= 0) {
                throw std::runtime_error("cannot write temporary configuration");
            }
            offset += static_cast<std::size_t>(count);
        }
    }

    std::string path_;
};

std::string resourceConfiguration(std::size_t preparedBytes, std::size_t queuedBytes)
{
    return "[hardware_resources]\n"
           "wave_pool_slot_count=2\n"
           "maximum_wave_pool_payload_bytes=1024\n"
           "maximum_requested_plan_payload_bytes=100\n"
           "maximum_prepared_sweep_payload_bytes=" +
           std::to_string(preparedBytes) +
           "\nmaximum_pending_commands_per_device=8\n"
           "maximum_pending_prepare_payload_bytes_per_device=" +
           std::to_string(queuedBytes) + "\n";
}

vna::VnaSweepPlan samplePlan()
{
    vna::VnaExcitationPlan excitation;
    excitation.excitationId = 1;
    excitation.sources.push_back(vna::VnaSourcePlan{1, -10.0, 0.0});
    excitation.requiredWaves = {
        vna::VnaWaveKey{vna::WaveKind::Incident, 1}, vna::VnaWaveKey{vna::WaveKind::Outgoing, 1}};

    vna::VnaSweepPlan plan;
    for (std::uint32_t point = 0; point < 3; ++point) {
        vna::VnaPointPlan item;
        item.pointId = point + 1;
        item.excitations.push_back(excitation);
        plan.points.push_back(std::move(item));
    }
    return plan;
}

vna::PreparedSweep samplePreparedSweep()
{
    vna::PreparedSweep prepared;
    prepared.effectivePlan = samplePlan();
    prepared.layouts.push_back(
        vna::VnaWaveLayout{
            7, 1, {vna::VnaWaveKey{vna::WaveKind::Incident, 1}, vna::VnaWaveKey{vna::WaveKind::Outgoing, 1}}});
    prepared.report.waveBlockPointCapacity = 2;
    prepared.report.warnings.push_back(
        vna::ValidationIssue{vna::ValidationSeverity::Warning, "points[0]", "driver warning"});
    return prepared;
}

template <typename Inspector>
void expectExactPayloadBoundary(const Inspector &inspect, vna::VnaErrorCode rejectionCode)
{
    auto measured = inspect(std::numeric_limits<std::size_t>::max());
    ASSERT_TRUE(measured.hasValue());
    ASSERT_GT(measured.value(), 0U);

    auto exact = inspect(measured.value());
    ASSERT_TRUE(exact.hasValue());
    EXPECT_EQ(exact.value(), measured.value());

    auto oneByteOver = inspect(measured.value() - 1);
    ASSERT_FALSE(oneByteOver.hasValue());
    EXPECT_EQ(oneByteOver.error().code, rejectionCode);
}

TEST(VnaHardwareResourceLimitsTest, RejectsMissingResourceSection)
{
    TemporaryConfiguration configuration("[logging]\nlevel=info\n");
    auto result = vna::detail::loadVnaHardwareResourceLimits(configuration.path());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, vna::VnaErrorCode::ConfigurationRejected);
}

TEST(VnaHardwareResourceLimitsTest, AcceptsEqualPayloadRelationships)
{
    TemporaryConfiguration configuration(resourceConfiguration(100, 100));
    auto result = vna::detail::loadVnaHardwareResourceLimits(configuration.path());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().maximumRequestedPlanPayloadBytes, 100U);
    EXPECT_EQ(result.value().maximumPreparedSweepPayloadBytes, 100U);
    EXPECT_EQ(result.value().maximumPendingPreparePayloadBytesPerDevice, 100U);
}

TEST(VnaHardwareResourceLimitsTest, RejectsPayloadLimitBelowRequestedLimit)
{
    const std::array<std::pair<std::size_t, std::size_t>, 2> invalidRelationships{{{99, 100}, {100, 99}}};
    for (const auto &[preparedBytes, queuedBytes] : invalidRelationships) {
        SCOPED_TRACE("prepared=" + std::to_string(preparedBytes) + ", queued=" + std::to_string(queuedBytes));
        TemporaryConfiguration configuration(resourceConfiguration(preparedBytes, queuedBytes));
        auto result = vna::detail::loadVnaHardwareResourceLimits(configuration.path());
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, vna::VnaErrorCode::ConfigurationRejected);
    }
}

TEST(VnaHardwareResourceLimitsTest, RejectsOverflowingDecimalValue)
{
    std::string content = resourceConfiguration(100, 100);
    const std::string valid = "wave_pool_slot_count=2";
    const std::size_t offset = content.find(valid);
    ASSERT_NE(offset, std::string::npos);
    content.replace(offset, valid.size(), "wave_pool_slot_count=184467440737095516160");
    TemporaryConfiguration configuration(content);

    auto result = vna::detail::loadVnaHardwareResourceLimits(configuration.path());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, vna::VnaErrorCode::ConfigurationRejected);
}

TEST(VnaPreparedCapacityTest, EnforcesRequestedPlanBoundaryExactly)
{
    const auto plan = samplePlan();
    expectExactPayloadBoundary(
        [&plan](std::size_t maximum) { return vna::detail::inspectRequestedPlanPayload(plan, maximum); },
        vna::VnaErrorCode::UnsupportedPlan);
}

TEST(VnaPreparedCapacityTest, CountsReservedContainerCapacity)
{
    auto plan = samplePlan();
    auto measured = vna::detail::inspectRequestedPlanPayload(plan, std::numeric_limits<std::size_t>::max());
    ASSERT_TRUE(measured.hasValue());

    plan.points.reserve(plan.points.capacity() + 64);
    auto result = vna::detail::inspectRequestedPlanPayload(plan, measured.value());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, vna::VnaErrorCode::UnsupportedPlan);
}

TEST(VnaPreparedCapacityTest, EnforcesDriverPreparedBoundaryExactly)
{
    const auto plan = samplePlan();
    const std::array<VnaWaveKeyV1, 2> waves{{{VNA_WAVE_INCIDENT, 1, 0}, {VNA_WAVE_OUTGOING, 1, 0}}};
    const VnaWaveLayoutViewV1 layout{7, 1, 0, waves.size(), waves.data()};
    VnaPrepareResultViewV1 view{};
    view.layoutCount = 1;
    view.layouts = &layout;
    expectExactPayloadBoundary(
        [&plan, &view](std::size_t maximum) { return vna::detail::inspectDriverPreparedPayload(plan, view, maximum); },
        vna::VnaErrorCode::ConfigurationRejected);
}

TEST(VnaPreparedCapacityTest, EnforcesStoredPreparedBoundaryExactly)
{
    const auto prepared = samplePreparedSweep();
    expectExactPayloadBoundary(
        [&prepared](std::size_t maximum) { return vna::detail::inspectPreparedSweepPayload(prepared, maximum); },
        vna::VnaErrorCode::ConfigurationRejected);
}

TEST(VnaWavePoolCapacityTest, EnforcesPoolBoundaryExactly)
{
    const auto prepared = samplePreparedSweep();
    vna::detail::VnaHardwareResourceLimits limits;
    limits.wavePoolSlotCount = 2;
    expectExactPayloadBoundary(
        [&prepared, &limits](std::size_t maximum) {
            limits.maximumWavePoolPayloadBytes = maximum;
            return vna::detail::inspectWavePoolPayload(prepared, limits);
        },
        vna::VnaErrorCode::ConfigurationRejected);
}

TEST(VnaWavePoolCapacityTest, AllowsCapacityAboveSinglePointWorkload)
{
    auto prepared = samplePreparedSweep();
    prepared.effectivePlan.points.resize(1);
    prepared.report.waveBlockPointCapacity = 2;
    vna::detail::VnaHardwareResourceLimits limits;
    limits.wavePoolSlotCount = 2;
    limits.maximumWavePoolPayloadBytes = 1024;

    auto result = vna::detail::inspectWavePoolPayload(prepared, limits);

    ASSERT_TRUE(result.hasValue());
    EXPECT_GT(result.value(), 0U);
}

}  // namespace
