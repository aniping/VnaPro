/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_ACQUISITION_CORE_TEST_PEER_H
#define VNAPRO_CORE_TEST_ACQUISITION_CORE_TEST_PEER_H

#include <cstdint>

#include "vna_core/acquisition_core.h"

namespace VnaCoreTest {

class AcquisitionCoreTestPeer final {
public:
    // 测试调用前必须等待全部已提交票据完成，且不得存在其他提交者。
    // 仅对空闲或手动运行的核心注入边界；不改变状态、模式、设备或已发布的帧。
    static void SeedCounters(
        VnaCore::AcquisitionCore &core, std::uint64_t runId, std::uint64_t revision, std::uint64_t frameSeq);
};

}  // 命名空间 VnaCoreTest

#endif
