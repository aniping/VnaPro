/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_TEST_NATIVE_FIXTURE_H
#define VNAPRO_SCPI_TEST_NATIVE_FIXTURE_H

#include <string>

#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"

namespace VnaScpiTest {

struct NativeFixture final {
    VnaCore::AcquisitionCore core;
    VnaService::InstrumentService service;

    // 构造每个协议测试用例前，先初始化原生模拟设备。
    explicit NativeFixture(const std::string &driver);
    // 协议测试销毁引擎后，再等待核心工作线程退出。
    ~NativeFixture() noexcept;
    NativeFixture(const NativeFixture &) = delete;
    NativeFixture &operator=(const NativeFixture &) = delete;
    NativeFixture(NativeFixture &&) = delete;
    NativeFixture &operator=(NativeFixture &&) = delete;
};

}  // 命名空间 VnaScpiTest

#endif
