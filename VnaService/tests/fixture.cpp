/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "fixture.h"

namespace VnaServiceTest {

namespace {
VnaService::InstrumentProfile LegacyProfile()
{
    VnaService::InstrumentProfile profile;
    profile.minFrequencyHz = 1.0;
    profile.maxFrequencyHz = 1000000000.0;
    profile.defaultStartHz = 1000000.0;
    profile.defaultStopHz = 100000000.0;
    return profile;
}
}  // 匿名命名空间

Fixture::Fixture() : Fixture(LegacyProfile())
{
}

Fixture::Fixture(VnaService::InstrumentProfile profile)
    : Fixture(std::make_unique<ControlledDevice>(), std::move(profile))
{
}

Fixture::Fixture(std::unique_ptr<ControlledDevice> owned, VnaService::InstrumentProfile profile)
    : device(owned.get()), core(std::move(owned)), service(core, std::move(profile))
{
    if (!core.Initialize().get().ok) {
        throw std::runtime_error("controlled initialization");
    }
    std::string error;
    if (!service.BootstrapProfile(error)) {
        throw std::runtime_error(error);
    }
}

Fixture::~Fixture()
{
    device->Release();
    core.Close();
}

}  // 命名空间 VnaServiceTest
