/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_fixture.h"

#include <stdexcept>

#include "vna_service/instrument_profile.h"

namespace VnaScpiTest {
namespace {

VnaService::InstrumentProfile LegacyProfile()
{
    // 历史黄金向量使用低频约定，避免为测试放宽正式仪器的能力配置。
    VnaService::InstrumentProfile profile;
    profile.minFrequencyHz = 1.0;
    profile.maxFrequencyHz = 1000000000.0;
    profile.defaultStartHz = 1000000.0;
    profile.defaultStopHz = 100000000.0;
    return profile;
}

}  // 匿名命名空间

NativeFixture::NativeFixture(const std::string &driver) : core(driver), service(core, LegacyProfile())
{
    if (!core.Initialize().get().ok) {
        throw std::runtime_error("initialize failed");
    }
    std::string error;
    if (!service.BootstrapMockProfile(error)) {
        throw std::runtime_error(error);
    }
}

NativeFixture::~NativeFixture() noexcept
{
    core.Close();
}
}  // 命名空间 VnaScpiTest
