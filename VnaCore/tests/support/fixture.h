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

    // 先将受控设备的所有权移交核心，再向测试暴露访问句柄。
    Fixture();
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    Fixture(Fixture &&) = delete;
    Fixture &operator=(Fixture &&) = delete;

    // 在等待工作线程结束前释放所有屏障，断言失败时也如此。
    ~Fixture();

private:
    explicit Fixture(std::unique_ptr<ControlledDevice> owned);
};

}  // 命名空间 VnaCoreTest

#endif
