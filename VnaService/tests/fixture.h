/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVICE_TEST_FIXTURE_H
#define VNAPRO_SERVICE_TEST_FIXTURE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "controlled_device.h"
#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"

namespace VnaServiceTest {

struct Fixture final {
    ControlledDevice *device = nullptr;
    VnaCore::AcquisitionCore core;
    VnaService::InstrumentService service;

    // 先将受控设备的所有权移交给核心，再公开测试所用的借用句柄。
    Fixture();
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    Fixture(Fixture &&) = delete;
    Fixture &operator=(Fixture &&) = delete;

    // 等待工作线程完成前先释放所有阻塞关卡，断言失败时也执行此清理。
    ~Fixture();

private:
    explicit Fixture(std::unique_ptr<ControlledDevice> owned);
};

}  // 命名空间 VnaServiceTest

#endif
