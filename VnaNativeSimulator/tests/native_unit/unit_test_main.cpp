/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_unit_tests.h"
#include "scoped_simulator_environment.h"

int main()
{
    VnaTest::ScopedSimulatorEnvironment environment(nullptr, nullptr);
    VnaTest::TestRunner runner;
    VnaTest::RunNativeApiSignatureTests(runner);
    VnaTest::RunNativeStateTests(runner);
    VnaTest::RunNoiseModeTests(runner);
    VnaTest::RunNormalFrameDecoderTests(runner);
    VnaTest::RunSignalModelTests(runner);
    return runner.Finish();
}
