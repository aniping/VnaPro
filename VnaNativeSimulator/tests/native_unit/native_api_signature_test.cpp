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
    VnaTest::Expect(VnaNative::NATIVE_PORT_COUNT == 4U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(VnaNative::PARAM_VALUES_PER_POINT == 6U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(VnaNative::TABLE_VALUES_PER_POINT == 9U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(VnaNative::IQ_VALUES_PER_POINT == 16U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvHwInitSymbol) == std::string("DrvHwInit"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvGetSweepDataSymbol) == std::string("DrvGetSweepData"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvStartSweepSymbol) == std::string("DrvStartSweep"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvActivateRxLinkSymbol) == std::string("DrvActivateRxLink"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvSetRfPowerOnSymbol) == std::string("DrvSetRfPowerOn"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvSetFastSweepModeSymbol) == std::string("DrvSetFastSweepMode"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvGetIdnSymbol) == std::string("DrvGetIDN"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvCheckSweepPortPowerSymbol) == std::string("DrvCheckSweepPortPower"),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(VnaNative::g_drvDataFrameDownloadSymbol) == std::string("DrvDataFrameDownload"),
        VnaTest::TestLocation{__FILE__, __LINE__});
}

namespace VnaTest {

void RunNativeApiSignatureTests(TestRunner &runner)
{
    runner.Run("NativeContractConstantsAreStable", NativeContractConstantsAreStable);
}

}  // 命名空间 VnaTest
