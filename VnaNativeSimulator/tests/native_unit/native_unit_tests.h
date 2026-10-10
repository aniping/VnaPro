/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_UNIT_TESTS_H
#define VNAPRO_TESTS_NATIVE_UNIT_TESTS_H

#include "test_runner.h"

namespace VnaTest {

void RunNativeApiSignatureTests(TestRunner &runner);
void RunNativeStateTests(TestRunner &runner);
void RunNoiseModeTests(TestRunner &runner);
void RunNormalFrameDecoderTests(TestRunner &runner);
void RunSignalModelTests(TestRunner &runner);

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_NATIVE_UNIT_TESTS_H
