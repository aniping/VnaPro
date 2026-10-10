/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <type_traits>

#include "../test_support.h"
#include "native_unit_tests.h"
#include "vna_native/native_api_contract.h"

static_assert(std::is_same<VnaNative::DrvHwInitFn, decltype(&::DrvHwInit)>::value, "DrvHwInit 签名不匹配");
static_assert(
    std::is_same<VnaNative::DrvGetSweepDataFn, decltype(&::DrvGetSweepData)>::value, "DrvGetSweepData 签名不匹配");
static_assert(std::is_same<VnaNative::DrvStartSweepFn, decltype(&::DrvStartSweep)>::value, "DrvStartSweep 签名不匹配");
static_assert(std::is_same<VnaNative::DrvActivateRxLinkFn, decltype(&::DrvActivateRxLink)>::value,
    "DrvActivateRxLink 签名不匹配");
static_assert(
    std::is_same<VnaNative::DrvSetRfPowerOnFn, decltype(&::DrvSetRfPowerOn)>::value, "DrvSetRfPowerOn 签名不匹配");
static_assert(std::is_same<VnaNative::DrvSetFastSweepModeFn, decltype(&::DrvSetFastSweepMode)>::value,
    "DrvSetFastSweepMode 签名不匹配");
static_assert(std::is_same<VnaNative::DrvGetIDNFn, decltype(&::DrvGetIDN)>::value, "DrvGetIDN 签名不匹配");
static_assert(std::is_same<VnaNative::DrvCheckSweepPortPowerFn, decltype(&::DrvCheckSweepPortPower)>::value,
    "DrvCheckSweepPortPower 签名不匹配");
static_assert(std::is_same<VnaNative::DrvDataFrameDownloadFn, decltype(&::DrvDataFrameDownload)>::value,
    "DrvDataFrameDownload 签名不匹配");

static void NativeContractConstantsAreStable()
{
    vna_test::Expect(VnaNative::NATIVE_PORT_COUNT == 4U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(VnaNative::PARAM_VALUES_PER_POINT == 6U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(VnaNative::TABLE_VALUES_PER_POINT == 9U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(VnaNative::IQ_VALUES_PER_POINT == 16U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvHwInitSymbol) == std::string("DrvHwInit"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvGetSweepDataSymbol) == std::string("DrvGetSweepData"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvStartSweepSymbol) == std::string("DrvStartSweep"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvActivateRxLinkSymbol) == std::string("DrvActivateRxLink"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvSetRfPowerOnSymbol) == std::string("DrvSetRfPowerOn"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvSetFastSweepModeSymbol) == std::string("DrvSetFastSweepMode"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvGetIdnSymbol) == std::string("DrvGetIDN"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvCheckSweepPortPowerSymbol) == std::string("DrvCheckSweepPortPower"),
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(VnaNative::g_drvDataFrameDownloadSymbol) == std::string("DrvDataFrameDownload"),
        vna_test::TestLocation{__FILE__, __LINE__});
}

namespace vna_test {

void RunNativeApiSignatureTests(TestRunner &runner)
{
    runner.Run("NativeContractConstantsAreStable", NativeContractConstantsAreStable);
}

}  // namespace vna_test
