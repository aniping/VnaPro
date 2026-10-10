/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "signal_model.h"

#include <cstdint>
#include <vector>

#include "golden_iq_vectors.h"
#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"

using VnaNative::DRV_SIM_INVALID_ARGUMENT;
using VnaNative::DRV_SIM_OK;
using VnaNative::NormalFrameSnapshot;
namespace SignalModel = VnaNative::SignalModel;

static void SignalModelMatchesGoldenLayoutForAllPorts()
{
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        std::vector<std::uint64_t> frequencies;
        frequencies.push_back(VnaTest::g_goldenIqFrequencies[0]);
        frequencies.push_back(VnaTest::g_goldenIqFrequencies[1]);
        const NormalFrameSnapshot frame = VnaTest::MakeSnapshot(sourcePort, frequencies);
        std::vector<double> actual;
        VnaTest::Expect(SignalModel::Generate(frame, actual) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::ExpectVectorNear(VnaTest::GoldenIqForSource(sourcePort), actual);
        VnaTest::Expect(actual.size() == 32U, VnaTest::TestLocation{__FILE__, __LINE__});
    }
}

static void SignalModelUsesFrequencyHighWordGoldenVector()
{
    VnaTest::Expect((VnaTest::GOLDEN_IQ_HIGH_WORD_FREQUENCY & 0xffffffffULL) == VnaTest::g_goldenIqFrequencies[0],
        VnaTest::TestLocation{__FILE__, __LINE__});

    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(VnaTest::g_goldenIqFrequencies[0]);
    frequencies.push_back(VnaTest::GOLDEN_IQ_HIGH_WORD_FREQUENCY);
    const NormalFrameSnapshot frame = VnaTest::MakeSnapshot(0U, frequencies);
    std::vector<double> actual;
    VnaTest::Expect(SignalModel::Generate(frame, actual) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(actual.size() == 32U, VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < 16U; ++index) {
        VnaTest::ExpectNear(VnaTest::g_goldenIqBySource[0U][0U][index], actual[index], 1.0e-12,
            VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::ExpectNear(VnaTest::g_goldenIqHighWordSource0[index], actual[16U + index], 1.0e-12,
            VnaTest::TestLocation{__FILE__, __LINE__});
    }
    VnaTest::Expect(actual[17U] != actual[1U], VnaTest::TestLocation{__FILE__, __LINE__});
}

static void SignalModelUsesCompleteFrequencyValueAndIsDeterministic()
{
    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(1000000000ULL);
    frequencies.push_back(2000000000ULL);
    const NormalFrameSnapshot frame = VnaTest::MakeSnapshot(0U, frequencies);
    std::vector<double> first;
    std::vector<double> second;
    VnaTest::Expect(SignalModel::Generate(frame, first) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(SignalModel::Generate(frame, second) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(second.size() == first.size(), VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < first.size(); ++index) {
        VnaTest::Expect(second[index] == first[index], VnaTest::TestLocation{__FILE__, __LINE__});
    }
    VnaTest::Expect(first[17U] != first[1U], VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(first[18U] != first[2U], VnaTest::TestLocation{__FILE__, __LINE__});
}

static void SignalModelRejectsInvalidFramesWithoutChangingOutput()
{
    std::vector<double> output(3U, 42.0);
    NormalFrameSnapshot empty;
    VnaTest::Expect(
        SignalModel::Generate(empty, output) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output.size() == 3U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output[0] == 42.0, VnaTest::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot badPort = VnaTest::MakeSnapshot(4U, std::vector<std::uint64_t>(1U, 1U));
    VnaTest::Expect(
        SignalModel::Generate(badPort, output) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot inconsistent = VnaTest::MakeSnapshot(1U, std::vector<std::uint64_t>(1U, 1U));
    inconsistent.points[0].sourcePort = 2U;
    VnaTest::Expect(SignalModel::Generate(inconsistent, output) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(output.size() == 3U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output[0] == 42.0, VnaTest::TestLocation{__FILE__, __LINE__});
}

namespace VnaTest {

void RunSignalModelTests(TestRunner &runner)
{
    runner.Run("SignalModelMatchesGoldenLayoutForAllPorts", SignalModelMatchesGoldenLayoutForAllPorts);
    runner.Run("SignalModelUsesFrequencyHighWordGoldenVector", SignalModelUsesFrequencyHighWordGoldenVector);
    runner.Run("SignalModelUsesCompleteFrequencyValueAndIsDeterministic",
        SignalModelUsesCompleteFrequencyValueAndIsDeterministic);
    runner.Run(
        "SignalModelRejectsInvalidFramesWithoutChangingOutput", SignalModelRejectsInvalidFramesWithoutChangingOutput);
}

}  // namespace VnaTest
