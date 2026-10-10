/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H
#define VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H

#include "controlled_device.h"

namespace VnaCoreTest {

struct ReleaseGateOnExit final {
    // The fixture must outlive this guard; copying would release the gate twice.
    explicit ReleaseGateOnExit(ControlledDevice &value) noexcept : device(value)
    {
    }

    ReleaseGateOnExit(const ReleaseGateOnExit &) = delete;
    ReleaseGateOnExit &operator=(const ReleaseGateOnExit &) = delete;
    ReleaseGateOnExit(ReleaseGateOnExit &&) = delete;
    ReleaseGateOnExit &operator=(ReleaseGateOnExit &&) = delete;
    ControlledDevice &device;

    ~ReleaseGateOnExit()
    {
        device.ReleaseGate();
    }
};

}  // namespace VnaCoreTest

#endif
