/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "batch_gate_release.h"

namespace BoardTest {

BatchGateRelease::BatchGateRelease(BatchGate &gate) : gate_(gate)
{
}

BatchGateRelease::~BatchGateRelease()
{
    gate_.Release();
}

}  // 命名空间 BoardTest
