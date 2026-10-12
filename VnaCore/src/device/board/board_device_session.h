/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_BOARD_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_BOARD_DEVICE_SESSION_H

#include "device/board/board_library.h"
#include "vna_core/device_session.h"

namespace VnaCore {
namespace Detail {

class BoardDeviceSession final : public DeviceSession {
public:
    explicit BoardDeviceSession(std::string path);
    BoardDeviceSession(const BoardDeviceSession &) = delete;
    BoardDeviceSession &operator=(const BoardDeviceSession &) = delete;
    BoardDeviceSession(BoardDeviceSession &&) = delete;
    BoardDeviceSession &operator=(BoardDeviceSession &&) = delete;
    ~BoardDeviceSession() override = default;

    // 此接口没有 IDN 查询；返回明确标注为本地模拟配置的身份。
    std::string Initialize() override;
    // 先准备所有列表，再暂存并一次性组帧；原始功率值不解释为 dBm。
    void ApplyPlan(const SweepPlan &plan) override;
    void EnableRf() override;
    // 一次启动全部工作源端口，分段等待读回后拆为核心的独立源缓冲区。
    std::vector<PortFrame> AcquireSweep(const SweepPlan &plan) override;
    // 射频关闭负责取消并回收模拟扫描线程，然后才能卸载库。
    void Shutdown() override;

private:
    const std::string path_;
    BoardLibrary library_;
    bool initializationAttempted_ = false;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
