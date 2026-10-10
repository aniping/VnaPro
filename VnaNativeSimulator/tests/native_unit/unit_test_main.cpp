/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_unit_tests.h"
#include "scoped_simulator_environment.h"

int main()
{
    vna_test::ScopedSimulatorEnvironment environment(nullptr, nullptr);
    vna_test::TestRunner runner;
    vna_test::RunNativeApiSignatureTests(runner);
    vna_test::RunNativeStateTests(runner);
    vna_test::RunNoiseModeTests(runner);
    vna_test::RunNormalFrameDecoderTests(runner);
    vna_test::RunSignalModelTests(runner);
    return runner.Finish();
}
