/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_BOARD_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_BOARD_DEVICE_SESSION_H

#include <memory>

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
    ~BoardDeviceSession() override;

    // 此接口没有 IDN 查询；回报通用适配器标识，并显式关闭初始射频。
    std::string Initialize() override;
    // 先准备所有列表，再暂存并一次性组帧；逐点 double 功率单位为 dBm。
    void ApplyPlan(const SweepPlan &plan) override;
    void EnableRf() override;
    // 一次启动全部工作源端口，分段等待读回后拆为核心的独立源缓冲区。
    std::vector<PortFrame> AcquireSweep(const SweepPlan &plan) override;
    // 核心先排空在途调用，本方法只关闭端口并报告未完成扫描；库和借出缓冲保留到进程退出。
    void Shutdown() override;

private:
    struct DownloadLists {
        std::vector<std::uint64_t> frequencies;
        std::vector<double> powers;
        std::vector<std::uint32_t> bandwidths;
        std::vector<std::uint32_t> ports;
    };

    static std::unique_ptr<DownloadLists> PrepareDownload(const SweepPlan &plan);

    const std::string path_;
    BoardLibrary library_;
    // 显式拥有传入驱动的数组；失败下发同时保留上次成功配置和本次候选。
    std::unique_ptr<DownloadLists> activeLists_;
    std::unique_ptr<DownloadLists> stagedLists_;
    // 完整读取成功后可替换，错误时一直持有；会话析构后仍保留至进程退出。
    std::unique_ptr<std::vector<double>> readBuffer_;
    bool initializationAttempted_ = false;
    bool keepLibraryResident_ = false;
    // Start 已尝试，直到所有频点成功读回才能确认扫描结束；RF OFF 不清除此状态。
    bool sweepInProgress_ = false;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
