#include <type_traits>

#include "../test_support.h"
#include "vna_native/native_api_contract.h"

static_assert(std::is_same<vna_native::DrvHwInitFn, decltype(&::DrvHwInit)>::value, "DrvHwInit 签名不匹配");
static_assert(
    std::is_same<vna_native::DrvGetSweepDataFn, decltype(&::DrvGetSweepData)>::value, "DrvGetSweepData 签名不匹配");
static_assert(std::is_same<vna_native::DrvStartSweepFn, decltype(&::DrvStartSweep)>::value, "DrvStartSweep 签名不匹配");
static_assert(std::is_same<vna_native::DrvActivateRxLinkFn, decltype(&::DrvActivateRxLink)>::value,
    "DrvActivateRxLink 签名不匹配");
static_assert(
    std::is_same<vna_native::DrvSetRfPowerOnFn, decltype(&::DrvSetRfPowerOn)>::value, "DrvSetRfPowerOn 签名不匹配");
static_assert(std::is_same<vna_native::DrvSetFastSweepModeFn, decltype(&::DrvSetFastSweepMode)>::value,
    "DrvSetFastSweepMode 签名不匹配");
static_assert(std::is_same<vna_native::DrvGetIDNFn, decltype(&::DrvGetIDN)>::value, "DrvGetIDN 签名不匹配");
static_assert(std::is_same<vna_native::DrvCheckSweepPortPowerFn, decltype(&::DrvCheckSweepPortPower)>::value,
    "DrvCheckSweepPortPower 签名不匹配");
static_assert(std::is_same<vna_native::DrvDataFrameDownloadFn, decltype(&::DrvDataFrameDownload)>::value,
    "DrvDataFrameDownload 签名不匹配");

VNA_TEST(NativeContractConstantsAreStable)
{
    VNA_EXPECT_EQ(4U, vna_native::kNativePortCount);
    VNA_EXPECT_EQ(6U, vna_native::kParamValuesPerPoint);
    VNA_EXPECT_EQ(9U, vna_native::kTableValuesPerPoint);
    VNA_EXPECT_EQ(16U, vna_native::kIqValuesPerPoint);
    VNA_EXPECT_EQ(std::string("DrvHwInit"), std::string(vna_native::kDrvHwInitSymbol));
    VNA_EXPECT_EQ(std::string("DrvGetSweepData"), std::string(vna_native::kDrvGetSweepDataSymbol));
    VNA_EXPECT_EQ(std::string("DrvStartSweep"), std::string(vna_native::kDrvStartSweepSymbol));
    VNA_EXPECT_EQ(std::string("DrvActivateRxLink"), std::string(vna_native::kDrvActivateRxLinkSymbol));
    VNA_EXPECT_EQ(std::string("DrvSetRfPowerOn"), std::string(vna_native::kDrvSetRfPowerOnSymbol));
    VNA_EXPECT_EQ(std::string("DrvSetFastSweepMode"), std::string(vna_native::kDrvSetFastSweepModeSymbol));
    VNA_EXPECT_EQ(std::string("DrvGetIDN"), std::string(vna_native::kDrvGetIDNSymbol));
    VNA_EXPECT_EQ(std::string("DrvCheckSweepPortPower"), std::string(vna_native::kDrvCheckSweepPortPowerSymbol));
    VNA_EXPECT_EQ(std::string("DrvDataFrameDownload"), std::string(vna_native::kDrvDataFrameDownloadSymbol));
}
