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
        frequencies.push_back(vna_test::g_goldenIqFrequencies[0]);
        frequencies.push_back(vna_test::g_goldenIqFrequencies[1]);
        const NormalFrameSnapshot frame = vna_test::MakeSnapshot(sourcePort, frequencies);
        std::vector<double> actual;
        vna_test::Expect(
            SignalModel::Generate(frame, actual) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::ExpectVectorNear(vna_test::GoldenIqForSource(sourcePort), actual);
        vna_test::Expect(actual.size() == 32U, vna_test::TestLocation{__FILE__, __LINE__});
    }
}

static void SignalModelUsesFrequencyHighWordGoldenVector()
{
    vna_test::Expect((vna_test::GOLDEN_IQ_HIGH_WORD_FREQUENCY & 0xffffffffULL) == vna_test::g_goldenIqFrequencies[0],
        vna_test::TestLocation{__FILE__, __LINE__});

    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(vna_test::g_goldenIqFrequencies[0]);
    frequencies.push_back(vna_test::GOLDEN_IQ_HIGH_WORD_FREQUENCY);
    const NormalFrameSnapshot frame = vna_test::MakeSnapshot(0U, frequencies);
    std::vector<double> actual;
    vna_test::Expect(SignalModel::Generate(frame, actual) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(actual.size() == 32U, vna_test::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < 16U; ++index) {
        vna_test::ExpectNear(vna_test::g_goldenIqBySource[0U][0U][index], actual[index], 1.0e-12,
            vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::ExpectNear(vna_test::g_goldenIqHighWordSource0[index], actual[16U + index], 1.0e-12,
            vna_test::TestLocation{__FILE__, __LINE__});
    }
    vna_test::Expect(actual[17U] != actual[1U], vna_test::TestLocation{__FILE__, __LINE__});
}

static void SignalModelUsesCompleteFrequencyValueAndIsDeterministic()
{
    std::vector<std::uint64_t> frequencies;
    frequencies.push_back(1000000000ULL);
    frequencies.push_back(2000000000ULL);
    const NormalFrameSnapshot frame = vna_test::MakeSnapshot(0U, frequencies);
    std::vector<double> first;
    std::vector<double> second;
    vna_test::Expect(SignalModel::Generate(frame, first) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(SignalModel::Generate(frame, second) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(second.size() == first.size(), vna_test::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < first.size(); ++index) {
        vna_test::Expect(second[index] == first[index], vna_test::TestLocation{__FILE__, __LINE__});
    }
    vna_test::Expect(first[17U] != first[1U], vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(first[18U] != first[2U], vna_test::TestLocation{__FILE__, __LINE__});
}

static void SignalModelRejectsInvalidFramesWithoutChangingOutput()
{
    std::vector<double> output(3U, 42.0);
    NormalFrameSnapshot empty;
    vna_test::Expect(
        SignalModel::Generate(empty, output) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(output.size() == 3U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(output[0] == 42.0, vna_test::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot badPort = vna_test::MakeSnapshot(4U, std::vector<std::uint64_t>(1U, 1U));
    vna_test::Expect(
        SignalModel::Generate(badPort, output) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot inconsistent = vna_test::MakeSnapshot(1U, std::vector<std::uint64_t>(1U, 1U));
    inconsistent.points[0].sourcePort = 2U;
    vna_test::Expect(SignalModel::Generate(inconsistent, output) == DRV_SIM_INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(output.size() == 3U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(output[0] == 42.0, vna_test::TestLocation{__FILE__, __LINE__});
}

namespace vna_test {

void RunSignalModelTests(TestRunner &runner)
{
    runner.Run("SignalModelMatchesGoldenLayoutForAllPorts", SignalModelMatchesGoldenLayoutForAllPorts);
    runner.Run("SignalModelUsesFrequencyHighWordGoldenVector", SignalModelUsesFrequencyHighWordGoldenVector);
    runner.Run("SignalModelUsesCompleteFrequencyValueAndIsDeterministic",
        SignalModelUsesCompleteFrequencyValueAndIsDeterministic);
    runner.Run(
        "SignalModelRejectsInvalidFramesWithoutChangingOutput", SignalModelRejectsInvalidFramesWithoutChangingOutput);
}

}  // namespace vna_test
