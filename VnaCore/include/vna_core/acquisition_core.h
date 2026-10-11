/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_ACQUISITION_CORE_H
#define VNAPRO_VNA_CORE_ACQUISITION_CORE_H

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "vna_core/core_command.h"
#include "vna_core/core_types.h"
#include "vna_core/device_session.h"
#include "vna_core/sweep_plan.h"

namespace VnaCoreTest {
class AcquisitionCoreTestPeer;
}  // 命名空间 VnaCoreTest

namespace VnaCore {

class AcquisitionCore final {
public:
    // 驱动路径必须为绝对路径；设备初始化仍通过异步操作执行。
    explicit AcquisitionCore(const std::string &nativeLibraryPath);
    // 接管设备所有权；设备调用仅在核心工作线程执行。
    explicit AcquisitionCore(std::unique_ptr<Detail::DeviceSession> device);
    // 所有者必须先结束其他调用者的访问；析构时处理完已接受任务，并等待工作线程结束。
    ~AcquisitionCore();

    AcquisitionCore(const AcquisitionCore &) = delete;
    AcquisitionCore &operator=(const AcquisitionCore &) = delete;
    AcquisitionCore(AcquisitionCore &&) = delete;
    AcquisitionCore &operator=(AcquisitionCore &&) = delete;

    // 返回设备初始化的结果票据；仅 UNINITIALIZED 状态接受此操作。
    Ticket Initialize();
    // 将完整计划复制到先进先出队列；成功后递增已提交配置的版本号。
    Ticket Configure(SweepPlan plan);
    // 要求处于 READY 状态且计划有效；成功后按请求模式启动新一轮运行。
    Ticket Start(RunMode mode);
    // 要求匹配当前 MANUAL 运行及配置版本；成功后发布完整且不可变的多源帧。
    Ticket TriggerOnce(std::uint64_t runId, std::uint64_t revision);
    // 拒绝零值或过期的运行标识；成功后形成先进先出的帧屏障，并保持射频启用。
    Ticket Stop(std::uint64_t runId);
    // 要求处于 FAULTED 状态；重新应用最近成功的计划，不重新启动采集。
    Ticket Recover();
    // 返回状态与帧一致的快照；被保留的帧不依赖核心对象的生命周期。
    CoreView ReadView() const;
    // 由所有者线程执行：停止接受命令，处理完已接受任务，尝试关闭射频，再等待工作线程结束。
    CommandResult Close();

private:
    friend class VnaCoreTest::AcquisitionCoreTestPeer;

    Ticket Submit(Detail::CoreCommandKind kind, SweepPlan plan = SweepPlan(), RunMode mode = RunMode::MANUAL,
        std::uint64_t runId = 0U, std::uint64_t revision = 0U);
    void WorkerLoop();
    CommandResult Execute(Detail::CoreCommand &command);
    CommandResult ExecuteCommand(Detail::CoreCommand &command);
    CommandResult ExecuteInitialize();
    CommandResult ExecuteConfigure(Detail::CoreCommand &command);
    CommandResult ExecuteStart(const Detail::CoreCommand &command);
    CommandResult ExecuteTrigger(const Detail::CoreCommand &command);
    CommandResult ExecuteStop(const Detail::CoreCommand &command);
    CommandResult ExecuteRecover();
    CommandResult ExecuteClose();
    void AcquireFrame();
    void Fault(const std::string &error);
    void PublishView();
    CommandResult Result(bool ok, const std::string &message = std::string()) const;
    Ticket Rejected(const std::string &message) const;

    std::unique_ptr<Detail::DeviceSession> device_;
    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::deque<Detail::CoreCommand> queue_;
    bool closing_ = false;
    Ticket closeTicket_;
    mutable std::mutex resultMutex_;
    CoreView published_;

    // 以下字段仅由工作线程访问。
    CoreState state_ = CoreState::UNINITIALIZED;
    RunMode mode_ = RunMode::MANUAL;
    SweepPlan plan_;
    bool configured_ = false;
    bool hasSuccessfulPlan_ = false;
    std::uint64_t runId_ = 0U;
    std::uint64_t revision_ = 0U;
    std::uint64_t frameSeq_ = 0U;
    FramePtr frame_;
    std::string error_;
    std::string deviceIdn_;
    std::thread worker_;
};

}  // 命名空间 VnaCore

#endif
