/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "fixture.h"

namespace VnaCoreTest {

Fixture::Fixture() : Fixture(std::make_unique<ControlledDevice>())
{
}

Fixture::Fixture(std::unique_ptr<ControlledDevice> owned) : device(owned.get()), core(std::move(owned))
{
    Require(Await(core.Initialize()).ok, "initialize failed");
    Require(core.ReadView().deviceIdn == "TestDevice", "identity was not published");
    Require(Await(core.Configure(MakePlan())).ok, "configure failed");
}

Fixture::~Fixture()
{
    device->ReleaseGate();
    core.Close();
}

}  // namespace VnaCoreTest
