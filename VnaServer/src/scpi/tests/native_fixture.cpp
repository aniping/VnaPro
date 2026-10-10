/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_fixture.h"

#include <stdexcept>

namespace VnaScpiTest {
NativeFixture::NativeFixture(const std::string &driver) : core(driver), service(core)
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
}  // namespace VnaScpiTest
