/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "fixture.h"

namespace VnaServiceTest {

Fixture::Fixture() : Fixture(std::make_unique<ControlledDevice>())
{
}

Fixture::Fixture(std::unique_ptr<ControlledDevice> owned) : device(owned.get()), core(std::move(owned)), service(core)
{
    if (!core.Initialize().get().ok) {
        throw std::runtime_error("controlled initialization");
    }
    std::string error;
    if (!service.BootstrapMockProfile(error)) {
        throw std::runtime_error(error);
    }
}

Fixture::~Fixture()
{
    device->Release();
    core.Close();
}

}  // 命名空间 VnaServiceTest
