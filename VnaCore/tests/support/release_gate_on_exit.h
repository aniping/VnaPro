/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H
#define VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H

#include "controlled_device.h"

namespace VnaCoreTest {

struct ReleaseGateOnExit final {
    // 测试夹具的生命周期必须长于本守卫；复制会导致屏障被释放两次。
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

}  // 命名空间 VnaCoreTest

#endif
