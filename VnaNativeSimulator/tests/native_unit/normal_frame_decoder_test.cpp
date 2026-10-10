/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"

using VnaNative::DRV_SIM_INTERNAL_ERROR;
using VnaNative::DRV_SIM_INVALID_ARGUMENT;
using VnaNative::DRV_SIM_NOT_SUPPORTED;
using VnaNative::DRV_SIM_OK;
using VnaNative::DRV_SIM_SIZE_OVERFLOW;
namespace NormalFrameDecoder = VnaNative::NormalFrameDecoder;
using VnaNative::NormalFrameSnapshot;

static void NormalFrameDecoderCreatesOwnedSnapshot()
{
    VnaTest::EncodedFrame input = VnaTest::MakeEncodedFrame(2U, 3U, 0x123456789abcdef0ULL);
    input.powerTable[0] = std::numeric_limits<double>::quiet_NaN();
    input.powerTable[1] = std::numeric_limits<double>::infinity();
    input.powerTable[2] = -std::numeric_limits<double>::infinity();
    const std::vector<double> expectedTable = input.powerTable;

    NormalFrameSnapshot output;
    VnaTest::Expect(
        NormalFrameDecoder::Decode(VnaNative::NormalFrameInput{2U, input.parameters.data(),
                                       static_cast<std::uint32_t>(input.parameters.size()), input.powerTable.data(),
                                       static_cast<std::uint32_t>(input.powerTable.size())},
            output) == DRV_SIM_OK,
        VnaTest::TestLocation{__FILE__, __LINE__});

    std::fill(input.parameters.begin(), input.parameters.end(), 0U);
    std::fill(input.powerTable.begin(), input.powerTable.end(), -1.0);
    std::vector<std::uint32_t>().swap(input.parameters);
    std::vector<double>().swap(input.powerTable);

    VnaTest::Expect(output.sourcePort == 3U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output.points.size() == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t point = 0U; point < output.points.size(); ++point) {
        VnaTest::Expect(output.points[point].frequencyRaw ==
                            0x123456789abcdef0ULL + static_cast<std::uint64_t>(point) * 0x100000003ULL,
            VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(output.points[point].sourcePort == 3U, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(output.points[point].ifBandwidthRaw == static_cast<std::uint32_t>(100U + point),
            VnaTest::TestLocation{__FILE__, __LINE__});
    }
    VnaTest::Expect(output.tableData.size() == expectedTable.size(), VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < expectedTable.size(); ++index) {
        if (std::isnan(expectedTable[index])) {
            VnaTest::Expect(std::isnan(output.tableData[index]), VnaTest::TestLocation{__FILE__, __LINE__});
        } else {
            VnaTest::Expect(output.tableData[index] == expectedTable[index], VnaTest::TestLocation{__FILE__, __LINE__});
        }
    }
}

static void NormalFrameDecoderRejectsShapeErrorsBeforeReading()
{
    std::uint32_t parameter = 0U;
    double table = 0.0;
    NormalFrameSnapshot output;

    VnaTest::Expect(NormalFrameDecoder::ValidateInputShape(VnaNative::NormalFrameInput{1U, nullptr, 7U, &table, 9U}) ==
                        DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(NormalFrameDecoder::ValidateInputShape(
                        VnaNative::NormalFrameInput{1U, &parameter, 7U, nullptr, 9U}) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(NormalFrameDecoder::ValidateInputShape(
                        VnaNative::NormalFrameInput{0U, &parameter, 1U, &table, 0U}) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(NormalFrameDecoder::ValidateInputShape(
                        VnaNative::NormalFrameInput{1U, &parameter, 6U, &table, 9U}) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(NormalFrameDecoder::ValidateInputShape(
                        VnaNative::NormalFrameInput{1U, &parameter, 7U, &table, 8U}) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        NormalFrameDecoder::ValidateInputShape(VnaNative::NormalFrameInput{(std::numeric_limits<std::uint32_t>::max)(),
            &parameter, (std::numeric_limits<std::uint32_t>::max)(), &table,
            (std::numeric_limits<std::uint32_t>::max)()}) == DRV_SIM_SIZE_OVERFLOW,
        VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NormalFrameDecoderRejectsUnsupportedAndInvalidFieldsAtomically()
{
    VnaTest::EncodedFrame input = VnaTest::MakeEncodedFrame(2U, 1U, 1000000000ULL);
    NormalFrameSnapshot output = VnaTest::MakeSnapshot(0U, std::vector<std::uint64_t>(1U, 77U));

    input.parameters[0] = 1U;
    VnaTest::Expect(NormalFrameDecoder::Decode(
                        VnaNative::NormalFrameInput{2U, input.parameters.data(), 13U, input.powerTable.data(), 18U},
                        output) == DRV_SIM_NOT_SUPPORTED,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output.points[0].frequencyRaw == 77U, VnaTest::TestLocation{__FILE__, __LINE__});

    input.parameters[0] = 0U;
    input.parameters[4U] = 1U;
    VnaTest::Expect(NormalFrameDecoder::Decode(
                        VnaNative::NormalFrameInput{2U, input.parameters.data(), 13U, input.powerTable.data(), 18U},
                        output) == DRV_SIM_NOT_SUPPORTED,
        VnaTest::TestLocation{__FILE__, __LINE__});
    input.parameters[4U] = 0U;
    input.parameters[6U] = 1U;
    VnaTest::Expect(NormalFrameDecoder::Decode(
                        VnaNative::NormalFrameInput{2U, input.parameters.data(), 13U, input.powerTable.data(), 18U},
                        output) == DRV_SIM_NOT_SUPPORTED,
        VnaTest::TestLocation{__FILE__, __LINE__});

    input.parameters[6U] = 0U;
    input.parameters[3U] = 4U;
    VnaTest::Expect(NormalFrameDecoder::Decode(
                        VnaNative::NormalFrameInput{2U, input.parameters.data(), 13U, input.powerTable.data(), 18U},
                        output) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    input.parameters[3U] = 1U;
    input.parameters[9U] = 2U;
    VnaTest::Expect(NormalFrameDecoder::Decode(
                        VnaNative::NormalFrameInput{2U, input.parameters.data(), 13U, input.powerTable.data(), 18U},
                        output) == DRV_SIM_INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(output.sourcePort == 0U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output.points.size() == 1U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(output.points[0].frequencyRaw == 77U, VnaTest::TestLocation{__FILE__, __LINE__});
}

namespace VnaTest {

void RunNormalFrameDecoderTests(TestRunner &runner)
{
    runner.Run("NormalFrameDecoderCreatesOwnedSnapshot", NormalFrameDecoderCreatesOwnedSnapshot);
    runner.Run("NormalFrameDecoderRejectsShapeErrorsBeforeReading", NormalFrameDecoderRejectsShapeErrorsBeforeReading);
    runner.Run("NormalFrameDecoderRejectsUnsupportedAndInvalidFieldsAtomically",
        NormalFrameDecoderRejectsUnsupportedAndInvalidFieldsAtomically);
}

}  // 命名空间 VnaTest
