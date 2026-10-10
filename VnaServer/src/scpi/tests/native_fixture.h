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

    // Initializes the native mock before constructing each protocol test case.
    explicit NativeFixture(const std::string &driver);
    // Joins the core after the protocol test destroys its engines.
    ~NativeFixture() noexcept;
    NativeFixture(const NativeFixture &) = delete;
    NativeFixture &operator=(const NativeFixture &) = delete;
    NativeFixture(NativeFixture &&) = delete;
    NativeFixture &operator=(NativeFixture &&) = delete;
};

}  // namespace VnaScpiTest

#endif
