/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_FIXTURE_H
#define VNAPRO_CORE_TEST_FIXTURE_H

#include <memory>
#include <utility>

#include "controlled_device.h"
#include "vna_core/acquisition_core.h"

namespace VnaCoreTest {

struct Fixture final {
    ControlledDevice *device = nullptr;
    VnaCore::AcquisitionCore core;

    // Transfers the controlled device into the core before exposing test handles.
    Fixture();
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    Fixture(Fixture &&) = delete;
    Fixture &operator=(Fixture &&) = delete;

    // Releases all gates before draining the worker, including on assertion failure.
    ~Fixture();

private:
    explicit Fixture(std::unique_ptr<ControlledDevice> owned);
};

}  // namespace VnaCoreTest

#endif
