#include "GoldenIqVectors.h"
#include "NativeUnitTestFixtures.h"
#include "SignalModel.h"

#include <cstdint>
#include <vector>

using vna_native::DRV_SIM_INVALID_ARGUMENT;
using vna_native::DRV_SIM_OK;
using vna_native::NormalFrameSnapshot;
using vna_native::SignalModel;

VNA_TEST(SignalModelMatchesGoldenLayoutForAllPorts) {
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        std::vector<std::uint64_t> frequencies;
        frequencies.push_back(vna_test::kGoldenIqFrequencies[0]);
        frequencies.push_back(vna_test::kGoldenIqFrequencies[1]);
        const NormalFrameSnapshot frame =
            vna_test::MakeSnapshot(sourcePort, frequencies);
        std::vector<double> actual;
        VNA_EXPECT_EQ(DRV_SIM_OK, SignalModel::Generate(frame, &actual));
        vna_test::ExpectVectorNear(vna_test::GoldenIqForSource(sourcePort),
                                   actual);
        VNA_EXPECT_EQ(32U, actual.size());
    }
}

VNA_TEST(SignalModelUsesFrequencyHighWordGoldenVector) {
    VNA_EXPECT_EQ(vna_test::kGoldenIqFrequencies[0],
                  vna_test::kGoldenIqHighWordFrequency &
                      UINT64_C(0xffffffff));

    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(vna_test::kGoldenIqFrequencies[0]);
    frequencies.push_back(vna_test::kGoldenIqHighWordFrequency);
    const NormalFrameSnapshot frame = vna_test::MakeSnapshot(0U, frequencies);
    std::vector<double> actual;
    VNA_EXPECT_EQ(DRV_SIM_OK, SignalModel::Generate(frame, &actual));
    VNA_EXPECT_EQ(32U, actual.size());
    for (std::size_t index = 0U; index < 16U; ++index) {
        VNA_EXPECT_NEAR(vna_test::kGoldenIqBySource[0U][0U][index],
                        actual[index], 1.0e-12);
        VNA_EXPECT_NEAR(vna_test::kGoldenIqHighWordSource0[index],
                        actual[16U + index], 1.0e-12);
    }
    VNA_EXPECT_NE(actual[1U], actual[17U]);
}

VNA_TEST(SignalModelUsesCompleteFrequencyValueAndIsDeterministic) {
    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(UINT64_C(1000000000));
    frequencies.push_back(UINT64_C(2000000000));
    const NormalFrameSnapshot frame = vna_test::MakeSnapshot(0U, frequencies);
    std::vector<double> first;
    std::vector<double> second;
    VNA_EXPECT_EQ(DRV_SIM_OK, SignalModel::Generate(frame, &first));
    VNA_EXPECT_EQ(DRV_SIM_OK, SignalModel::Generate(frame, &second));
    VNA_EXPECT_EQ(first.size(), second.size());
    for (std::size_t index = 0U; index < first.size(); ++index) {
        VNA_EXPECT_EQ(first[index], second[index]);
    }
    VNA_EXPECT_NE(first[1U], first[17U]);
    VNA_EXPECT_NE(first[2U], first[18U]);
}

VNA_TEST(SignalModelRejectsInvalidFramesWithoutChangingOutput) {
    std::vector<double> output(3U, 42.0);
    NormalFrameSnapshot empty;
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  SignalModel::Generate(empty, &output));
    VNA_EXPECT_EQ(3U, output.size());
    VNA_EXPECT_EQ(42.0, output[0]);

    NormalFrameSnapshot badPort =
        vna_test::MakeSnapshot(4U, std::vector<std::uint64_t>(1U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  SignalModel::Generate(badPort, &output));

    NormalFrameSnapshot inconsistent =
        vna_test::MakeSnapshot(1U, std::vector<std::uint64_t>(1U, 1U));
    inconsistent.points[0].sourcePort = 2U;
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  SignalModel::Generate(inconsistent, &output));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  SignalModel::Generate(inconsistent, NULL));
    VNA_EXPECT_EQ(3U, output.size());
    VNA_EXPECT_EQ(42.0, output[0]);
}
