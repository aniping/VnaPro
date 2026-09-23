#include "NativeUnitTestFixtures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using vna_native::DRV_SIM_INTERNAL_ERROR;
using vna_native::DRV_SIM_INVALID_ARGUMENT;
using vna_native::DRV_SIM_NOT_SUPPORTED;
using vna_native::DRV_SIM_OK;
using vna_native::DRV_SIM_SIZE_OVERFLOW;
using vna_native::NormalFrameDecoder;
using vna_native::NormalFrameSnapshot;

VNA_TEST(NormalFrameDecoderCreatesOwnedSnapshot) {
    vna_test::EncodedFrame input =
        vna_test::MakeEncodedFrame(2U, 3U, UINT64_C(0x123456789abcdef0));
    input.powerTable[0] = std::numeric_limits<double>::quiet_NaN();
    input.powerTable[1] = std::numeric_limits<double>::infinity();
    input.powerTable[2] = -std::numeric_limits<double>::infinity();
    const std::vector<double> expectedTable = input.powerTable;

    NormalFrameSnapshot output;
    VNA_EXPECT_EQ(DRV_SIM_OK,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(),
                      static_cast<std::uint32_t>(input.parameters.size()),
                      input.powerTable.data(),
                      static_cast<std::uint32_t>(input.powerTable.size()),
                      &output));

    std::fill(input.parameters.begin(), input.parameters.end(), 0U);
    std::fill(input.powerTable.begin(), input.powerTable.end(), -1.0);
    std::vector<std::uint32_t>().swap(input.parameters);
    std::vector<double>().swap(input.powerTable);

    VNA_EXPECT_EQ(3U, output.sourcePort);
    VNA_EXPECT_EQ(2U, output.points.size());
    for (std::size_t point = 0U; point < output.points.size(); ++point) {
        VNA_EXPECT_EQ(UINT64_C(0x123456789abcdef0) +
                          static_cast<std::uint64_t>(point) *
                              UINT64_C(0x100000003),
                      output.points[point].frequencyRaw);
        VNA_EXPECT_EQ(3U, output.points[point].sourcePort);
        VNA_EXPECT_EQ(static_cast<std::uint32_t>(100U + point),
                      output.points[point].ifBandwidthRaw);
    }
    VNA_EXPECT_EQ(expectedTable.size(), output.tableData.size());
    for (std::size_t index = 0U; index < expectedTable.size(); ++index) {
        if (std::isnan(expectedTable[index])) {
            VNA_EXPECT_TRUE(std::isnan(output.tableData[index]));
        } else {
            VNA_EXPECT_EQ(expectedTable[index], output.tableData[index]);
        }
    }
}

VNA_TEST(NormalFrameDecoderRejectsShapeErrorsBeforeReading) {
    std::uint32_t parameter = 0U;
    double table = 0.0;
    NormalFrameSnapshot output;

    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::ValidateInputShape(
                      1U, NULL, 7U, &table, 9U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::ValidateInputShape(
                      1U, &parameter, 7U, NULL, 9U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::ValidateInputShape(
                      0U, &parameter, 1U, &table, 0U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::ValidateInputShape(
                      1U, &parameter, 6U, &table, 9U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::ValidateInputShape(
                      1U, &parameter, 7U, &table, 8U));
    VNA_EXPECT_EQ(DRV_SIM_SIZE_OVERFLOW,
                  NormalFrameDecoder::ValidateInputShape(
                      (std::numeric_limits<std::uint32_t>::max)(), &parameter,
                      (std::numeric_limits<std::uint32_t>::max)(), &table,
                      (std::numeric_limits<std::uint32_t>::max)()));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::Decode(
                      1U, &parameter, 7U, &table, 9U, NULL));
}

VNA_TEST(NormalFrameDecoderRejectsUnsupportedAndInvalidFieldsAtomically) {
    vna_test::EncodedFrame input =
        vna_test::MakeEncodedFrame(2U, 1U, UINT64_C(1000000000));
    NormalFrameSnapshot output =
        vna_test::MakeSnapshot(0U, std::vector<std::uint64_t>(1U, 77U));

    input.parameters[0] = 1U;
    VNA_EXPECT_EQ(DRV_SIM_NOT_SUPPORTED,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(), 13U,
                      input.powerTable.data(), 18U, &output));
    VNA_EXPECT_EQ(77U, output.points[0].frequencyRaw);

    input.parameters[0] = 0U;
    input.parameters[4U] = 1U;
    VNA_EXPECT_EQ(DRV_SIM_NOT_SUPPORTED,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(), 13U,
                      input.powerTable.data(), 18U, &output));
    input.parameters[4U] = 0U;
    input.parameters[6U] = 1U;
    VNA_EXPECT_EQ(DRV_SIM_NOT_SUPPORTED,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(), 13U,
                      input.powerTable.data(), 18U, &output));

    input.parameters[6U] = 0U;
    input.parameters[3U] = 4U;
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(), 13U,
                      input.powerTable.data(), 18U, &output));
    input.parameters[3U] = 1U;
    input.parameters[9U] = 2U;
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT,
                  NormalFrameDecoder::Decode(
                      2U, input.parameters.data(), 13U,
                      input.powerTable.data(), 18U, &output));

    VNA_EXPECT_EQ(0U, output.sourcePort);
    VNA_EXPECT_EQ(1U, output.points.size());
    VNA_EXPECT_EQ(77U, output.points[0].frequencyRaw);
}
