/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_ACQUISITION_CORE_H
#define VNAPRO_VNA_CORE_ACQUISITION_CORE_H

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

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
    // 同名原生函数存在不兼容签名，按明确指定的板卡接口选择加载器和整帧采集适配器。
    AcquisitionCore(const std::string &nativeLibraryPath, DriverApi api);
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
    Ticket Configure(SweepPlan plan, unsigned channelId = 1U);
    // 创建独立逻辑采集通道，最多保留 16 个；设备仍只有一个工作线程和会话。
    Ticket CreateChannel(unsigned channelId);
    // 在完整帧边界停止并删除通道，不影响其他通道；至少保留一个通道。
    Ticket RemoveChannel(unsigned channelId);
    // 要求处于 READY 状态且计划有效；成功后按请求模式启动新一轮运行。
    Ticket Start(RunMode mode, unsigned channelId = 1U);
    // 要求匹配当前 MANUAL 运行及配置版本；成功后发布完整且不可变的多源帧。
    Ticket TriggerOnce(std::uint64_t runId, std::uint64_t revision, unsigned channelId = 1U);
    // 拒绝零值或过期的运行标识；成功后形成先进先出的帧屏障，并保持射频启用。
    Ticket Stop(std::uint64_t runId, unsigned channelId = 1U);
    // 要求处于 FAULTED 状态；重新应用最近成功的计划，不重新启动采集。
    Ticket Recover();
    // 返回状态与帧一致的快照；被保留的帧不依赖核心对象的生命周期。
    CoreView ReadView(unsigned channelId = 1U) const;
    // 由所有者线程执行：停止接受命令，处理完已接受任务，尝试关闭射频，再等待工作线程结束。
    CommandResult Close();

private:
    friend class VnaCoreTest::AcquisitionCoreTestPeer;

    Ticket Submit(Detail::CoreCommandKind kind, SweepPlan plan = SweepPlan(), RunMode mode = RunMode::MANUAL,
        std::uint64_t runId = 0U, std::uint64_t revision = 0U, unsigned channelId = 1U);
    void WorkerLoop();
    CommandResult Execute(Detail::CoreCommand &command);
    CommandResult ExecuteCommand(Detail::CoreCommand &command);
    CommandResult ExecuteInitialize();
    CommandResult ExecuteCreateChannel(unsigned channelId);
    CommandResult ExecuteRemoveChannel(unsigned channelId);
    CommandResult ExecuteConfigure(Detail::CoreCommand &command);
    CommandResult ExecuteStart(const Detail::CoreCommand &command);
    CommandResult ExecuteTrigger(const Detail::CoreCommand &command);
    CommandResult ExecuteStop(const Detail::CoreCommand &command);
    CommandResult ExecuteRecover();
    CommandResult ExecuteClose();
    void AcquireFrame(unsigned channelId);
    void ApplyChannelPlan(unsigned channelId);
    unsigned NextContinuousChannel() const;
    void Fault(const std::string &error);
    void PublishView();
    CommandResult Result(bool ok, const std::string &message = std::string(), unsigned channelId = 0U) const;
    Ticket Rejected(const std::string &message, unsigned channelId = 1U) const;

    std::unique_ptr<Detail::DeviceSession> device_;
    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::deque<Detail::CoreCommand> queue_;
    bool closing_ = false;
    Ticket closeTicket_;
    mutable std::mutex resultMutex_;
    std::map<unsigned, CoreView> published_{{1U, CoreView()}};

    // 以下字段仅由工作线程访问。
    struct ChannelContext final {
        CoreState state = CoreState::UNINITIALIZED;
        RunMode mode = RunMode::MANUAL;
        SweepPlan plan;
        bool configured = false;
        bool hasSuccessfulPlan = false;
        std::uint64_t runId = 0U;
        std::uint64_t revision = 0U;
        std::uint64_t frameSeq = 0U;
        FramePtr frame;
    };

    std::map<unsigned, ChannelContext> channels_{{1U, ChannelContext()}};
    // 删除后再次创建相同 ID 时延续计数，禁止旧运行或配置票据命中新通道。
    std::map<unsigned, std::pair<std::uint64_t, std::uint64_t>> retiredIdentities_;
    CoreState deviceState_ = CoreState::UNINITIALIZED;
    unsigned commandChannelId_ = 1U;
    unsigned appliedChannelId_ = 0U;
    std::uint64_t appliedRevision_ = 0U;
    unsigned lastScheduledChannelId_ = 0U;
    std::string error_;
    std::string deviceIdn_;
    std::thread worker_;
};

}  // 命名空间 VnaCore

#endif
