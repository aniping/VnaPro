/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_core/acquisition_core.h"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

#include "device/device_session_factory.h"
#include "device/plan_preparation_error.h"
#include "sweep_plan_validation.h"
#include "vna_core/frame_layout.h"
#include "vna_log/logger.h"

namespace VnaCore {
namespace {

constexpr std::size_t QUEUE_CAPACITY = 64U;
constexpr std::uint64_t CORE_IDENTITY_MAX = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::chrono::milliseconds g_continuousFrameInterval(10);

std::unique_ptr<Detail::DeviceSession> ValidateDevice(std::unique_ptr<Detail::DeviceSession> device)
{
    if (!device) {
        throw std::invalid_argument("core requires an owned device session");
    }
    return device;
}

std::unique_ptr<Detail::DeviceSession> CreateDevice(const std::string &path, DriverApi api)
{
    switch (api) {
        case DriverApi::TDC1VN08:
            return Detail::CreateNativeDevice(path);
        case DriverApi::TDC1VN10:
            return Detail::CreateBoardDevice(path);
        default:
            throw std::invalid_argument("unsupported driver API version");
    }
}

void LogCompletedCommand(Detail::CoreCommandKind kind, const CommandResult &result, const SweepPlan &plan, RunMode mode,
    const std::string &deviceIdn) noexcept
{
    switch (kind) {
        case Detail::CoreCommandKind::INITIALIZE:
            VnaLog::Info(VnaLog::Module::CORE, "设备初始化成功，标识={}", deviceIdn);
            break;
        case Detail::CoreCommandKind::CONFIGURE:
            VnaLog::Info(VnaLog::Module::CORE, "采集配置已提交，revision={}，points={}", result.configRevision,
                plan.frequencyRaw.size());
            break;
        case Detail::CoreCommandKind::START:
            VnaLog::Info(VnaLog::Module::CORE, "采集已启动，run={}，revision={}，mode={}", result.runId,
                result.configRevision, mode == RunMode::CONTINUOUS ? "CONTINUOUS" : "MANUAL");
            break;
        case Detail::CoreCommandKind::STOP:
            VnaLog::Info(VnaLog::Module::CORE, "采集停止命令已完成，run={}，frame={}", result.runId, result.frameSeq);
            break;
        case Detail::CoreCommandKind::RECOVER:
            VnaLog::Info(VnaLog::Module::CORE, "采集核心已恢复，revision={}", result.configRevision);
            break;
        case Detail::CoreCommandKind::CLOSE:
            VnaLog::Info(VnaLog::Module::CORE, "采集核心已关闭，run={}，frame={}", result.runId, result.frameSeq);
            break;
        default:
            break;
    }
}

}  // 匿名命名空间

AcquisitionCore::AcquisitionCore(const std::string &path) : AcquisitionCore(path, DriverApi::TDC1VN08)
{
}

AcquisitionCore::AcquisitionCore(const std::string &path, DriverApi api) : AcquisitionCore(CreateDevice(path, api))
{
}

Ticket AcquisitionCore::Initialize()
{
    return Submit(Detail::CoreCommandKind::INITIALIZE);
}

Ticket AcquisitionCore::Configure(SweepPlan plan, unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::CONFIGURE, std::move(plan), RunMode::MANUAL, 0U, 0U, channelId);
}

Ticket AcquisitionCore::CreateChannel(unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::CREATE_CHANNEL, SweepPlan(), RunMode::MANUAL, 0U, 0U, channelId);
}

Ticket AcquisitionCore::RemoveChannel(unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::REMOVE_CHANNEL, SweepPlan(), RunMode::MANUAL, 0U, 0U, channelId);
}

Ticket AcquisitionCore::Start(RunMode mode, unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::START, SweepPlan(), mode, 0U, 0U, channelId);
}

Ticket AcquisitionCore::TriggerOnce(std::uint64_t runId, std::uint64_t revision, unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::TRIGGER, SweepPlan(), RunMode::MANUAL, runId, revision, channelId);
}

Ticket AcquisitionCore::Stop(std::uint64_t runId, unsigned channelId)
{
    return Submit(Detail::CoreCommandKind::STOP, SweepPlan(), RunMode::MANUAL, runId, 0U, channelId);
}

Ticket AcquisitionCore::Recover()
{
    return Submit(Detail::CoreCommandKind::RECOVER);
}

AcquisitionCore::AcquisitionCore(std::unique_ptr<Detail::DeviceSession> device)
    : device_(ValidateDevice(std::move(device))), worker_(&AcquisitionCore::WorkerLoop, this)
{
}

AcquisitionCore::~AcquisitionCore()
{
    Close();
}

Ticket AcquisitionCore::Rejected(const std::string &message, unsigned channelId) const
{
    const CoreView view = ReadView(channelId);
    CommandResult answer;
    answer.message = message;
    answer.runId = view.runId;
    answer.configRevision = view.configRevision;
    answer.frameSeq = view.frame && view.frame->runId == view.runId ? view.frame->frameSeq : 0U;
    answer.channelId = channelId;
    std::promise<CommandResult> promise;
    auto ticket = promise.get_future().share();
    promise.set_value(std::move(answer));
    return ticket;
}

Ticket AcquisitionCore::Submit(Detail::CoreCommandKind kind, SweepPlan plan, RunMode mode, std::uint64_t runId,
    std::uint64_t revision, unsigned channelId)
{
    if (channelId == 0U) {
        return Rejected("channel identity must be nonzero", channelId);
    }
    if (kind == Detail::CoreCommandKind::CONFIGURE) {
        const auto error = Detail::ValidateSweepPlan(plan);
        if (!error.empty()) {
            return Rejected(error, channelId);
        }
    }
    std::promise<CommandResult> promise;
    Ticket ticket = promise.get_future().share();
    std::string rejection;
    {
        std::lock_guard<std::mutex> guard(queueMutex_);
        if (closing_) {
            rejection = "core is closing or closed";
        } else if (queue_.size() >= QUEUE_CAPACITY) {
            rejection = "core command queue is full";
        } else {
            queue_.push_back(
                Detail::CoreCommand{kind, std::move(plan), mode, runId, revision, std::move(promise), channelId});
        }
    }
    if (!rejection.empty()) {
        return Rejected(rejection, channelId);
    }
    wake_.notify_one();
    return ticket;
}

CoreView AcquisitionCore::ReadView(unsigned channelId) const
{
    std::lock_guard<std::mutex> guard(resultMutex_);
    const auto found = published_.find(channelId);
    if (found != published_.end()) {
        return found->second;
    }
    CoreView missing;
    missing.channelId = channelId;
    missing.error = "channel does not exist";
    return missing;
}

CommandResult AcquisitionCore::Close()
{
    // 生命周期所有者串行执行关闭和析构；命令提交仍然线程安全。
    {
        std::lock_guard<std::mutex> guard(queueMutex_);
        if (!closing_) {
            std::promise<CommandResult> promise;
            closeTicket_ = promise.get_future().share();
            queue_.push_back(Detail::CoreCommand{
                Detail::CoreCommandKind::CLOSE, SweepPlan(), RunMode::MANUAL, 0U, 0U, std::move(promise)});
            closing_ = true;  // 普通队列已满时，仍接受预留的关闭命令。
        }
    }
    wake_.notify_one();
    const CommandResult answer = closeTicket_.get();
    if (worker_.joinable()) {
        worker_.join();
    }
    return answer;
}

CommandResult AcquisitionCore::Result(bool ok, const std::string &message, unsigned channelId) const
{
    CommandResult answer;
    answer.ok = ok;
    answer.message = message;
    answer.channelId = channelId == 0U ? commandChannelId_ : channelId;
    const auto found = channels_.find(answer.channelId);
    if (found != channels_.end()) {
        answer.runId = found->second.runId;
        answer.configRevision = found->second.revision;
        answer.frameSeq = found->second.frameSeq;
    }
    return answer;
}

void AcquisitionCore::PublishView()
{
    std::map<unsigned, CoreView> views;
    for (const auto &entry : channels_) {
        CoreView view;
        view.channelId = entry.first;
        view.state = entry.second.state;
        view.runId = entry.second.runId;
        view.configRevision = entry.second.revision;
        view.frame = entry.second.frame;
        view.error = error_;
        view.deviceIdn = deviceIdn_;
        views.emplace(entry.first, std::move(view));
    }
    {
        std::lock_guard<std::mutex> guard(resultMutex_);
        published_.swap(views);
    }
    // 释放发布锁后，再销毁可能占用较大内存的上一帧。
}

void AcquisitionCore::Fault(const std::string &error)
{
    deviceState_ = CoreState::FAULTED;
    appliedChannelId_ = 0U;
    for (auto &entry : channels_) {
        entry.second.state = CoreState::FAULTED;
        entry.second.configured = false;
    }
    error_ = error;
    PublishView();
    VnaLog::Error(VnaLog::Module::CORE, "采集设备进入故障，所有通道暂停，原因={}", error_);
}

void AcquisitionCore::ApplyChannelPlan(unsigned channelId)
{
    const auto &channel = channels_.at(channelId);
    if (appliedChannelId_ != channelId || appliedRevision_ != channel.revision) {
        appliedChannelId_ = 0U;
        device_->ApplyPlan(channel.plan);
        appliedChannelId_ = channelId;
        appliedRevision_ = channel.revision;
    }
}

void AcquisitionCore::AcquireFrame(unsigned channelId)
{
    auto &channel = channels_.at(channelId);
    if (channel.frameSeq == CORE_IDENTITY_MAX) {
        throw std::overflow_error("frame sequence exhausted");
    }
    ApplyChannelPlan(channelId);
    MeasurementFrame completed;
    completed.channelId = channelId;
    completed.runId = channel.runId;
    completed.configRevision = channel.revision;
    completed.frameSeq = channel.frameSeq + 1U;
    completed.plan = channel.plan;
    completed.ports = device_->AcquireSweep(channel.plan);
    if (completed.ports.size() != channel.plan.sourcePorts.size()) {
        throw std::runtime_error("device returned an incomplete multi-source sweep");
    }
    for (std::size_t index = 0U; index < completed.ports.size(); ++index) {
        const auto &port = completed.ports[index];
        if (port.sourcePort != channel.plan.sourcePorts[index]) {
            throw std::runtime_error("device returned mismatched or reordered source ports");
        }
        if (port.iq.size() != IQ_VALUES_PER_POINT * channel.plan.frequencyRaw.size()) {
            throw std::runtime_error("device returned an incomplete IQ port frame");
        }
    }
    // 发布后不保留可变别名；后续端口采集失败时，不替换 frame_。
    FramePtr immutable = std::make_shared<const MeasurementFrame>(std::move(completed));
    channel.frameSeq = immutable->frameSeq;
    channel.frame = std::move(immutable);
    PublishView();
    VnaLog::Trace(VnaLog::Module::CORE,
        "完整采集帧已发布，channel={}，run={}，frame={}，revision={}，points={}，ports={}", channelId, channel.runId,
        channel.frameSeq, channel.revision, channel.plan.frequencyRaw.size(), channel.frame->ports.size());
}

CommandResult AcquisitionCore::Execute(Detail::CoreCommand &command)
{
    commandChannelId_ = command.channelId;
    try {
        const CommandResult answer = ExecuteCommand(command);
        if (answer.ok) {
            PublishView();
            const auto channel = channels_.find(commandChannelId_);
            LogCompletedCommand(command.kind, answer, channel == channels_.end() ? SweepPlan() : channel->second.plan,
                channel == channels_.end() ? RunMode::MANUAL : channel->second.mode, deviceIdn_);
        } else {
            VnaLog::Debug(VnaLog::Module::CORE, "采集命令未成功，command={}，run={}，revision={}，原因={}",
                static_cast<unsigned>(command.kind), answer.runId, answer.configRevision, answer.message);
        }
        return answer;
    } catch (const Detail::PlanPreparationError &error) {
        if (command.kind != Detail::CoreCommandKind::CONFIGURE) {
            Fault(error.what());
        }
        return Result(false, error.what());
    } catch (const std::exception &error) {
        Fault(error.what());
        return Result(false, error_);
    } catch (...) {
        Fault("unknown device or frame processing exception");
        if (command.kind == Detail::CoreCommandKind::CLOSE) {
            deviceState_ = CoreState::CLOSED;
            for (auto &entry : channels_) {
                entry.second.state = CoreState::CLOSED;
            }
            PublishView();
        }
        return Result(false, error_);
    }
}

CommandResult AcquisitionCore::ExecuteCommand(Detail::CoreCommand &command)
{
    const bool global = command.kind == Detail::CoreCommandKind::INITIALIZE ||
                        command.kind == Detail::CoreCommandKind::RECOVER ||
                        command.kind == Detail::CoreCommandKind::CLOSE;
    if (!global && command.kind != Detail::CoreCommandKind::CREATE_CHANNEL &&
        channels_.find(command.channelId) == channels_.end()) {
        return Result(false, "channel does not exist");
    }
    switch (command.kind) {
        case Detail::CoreCommandKind::INITIALIZE:
            return ExecuteInitialize();
        case Detail::CoreCommandKind::CREATE_CHANNEL:
            return ExecuteCreateChannel(command.channelId);
        case Detail::CoreCommandKind::REMOVE_CHANNEL:
            return ExecuteRemoveChannel(command.channelId);
        case Detail::CoreCommandKind::CONFIGURE:
            return ExecuteConfigure(command);
        case Detail::CoreCommandKind::START:
            return ExecuteStart(command);
        case Detail::CoreCommandKind::TRIGGER:
            return ExecuteTrigger(command);
        case Detail::CoreCommandKind::STOP:
            return ExecuteStop(command);
        case Detail::CoreCommandKind::RECOVER:
            return ExecuteRecover();
        case Detail::CoreCommandKind::CLOSE:
            return ExecuteClose();
        default:
            return Result(false, "unsupported core command");
    }
}

CommandResult AcquisitionCore::ExecuteInitialize()
{
    if (deviceState_ != CoreState::UNINITIALIZED) {
        return Result(false, "initialize requires an uninitialized core");
    }
    deviceIdn_ = device_->Initialize();
    deviceState_ = CoreState::READY;
    for (auto &entry : channels_) {
        entry.second.state = CoreState::READY;
    }
    error_.clear();
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteCreateChannel(unsigned channelId)
{
    if (deviceState_ != CoreState::READY) {
        return Result(false, "channel creation requires an initialized healthy device");
    }
    if (channels_.find(channelId) != channels_.end()) {
        return Result(false, "channel already exists");
    }
    if (channels_.size() >= 16U) {
        return Result(false, "channel capacity is 16");
    }
    ChannelContext channel;
    channel.state = CoreState::READY;
    const auto retired = retiredIdentities_.find(channelId);
    if (retired != retiredIdentities_.end()) {
        channel.runId = retired->second.first;
        channel.revision = retired->second.second;
    }
    channels_.emplace(channelId, std::move(channel));
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteRemoveChannel(unsigned channelId)
{
    if (channels_.size() == 1U) {
        return Result(false, "at least one channel must remain");
    }
    const auto answer = Result(true);
    retiredIdentities_[channelId] = std::make_pair(answer.runId, answer.configRevision);
    channels_.erase(channelId);
    if (appliedChannelId_ == channelId) {
        appliedChannelId_ = 0U;
    }
    return answer;
}

CommandResult AcquisitionCore::ExecuteConfigure(Detail::CoreCommand &command)
{
    auto &channel = channels_.at(command.channelId);
    if (channel.state != CoreState::READY && channel.state != CoreState::RUNNING) {
        return Result(false, "configure requires Ready or Running");
    }
    if (channel.revision == CORE_IDENTITY_MAX) {
        return Result(false, "configuration revision exhausted");
    }
    device_->ApplyPlan(command.plan);
    channel.plan = std::move(command.plan);
    channel.hasSuccessfulPlan = true;
    channel.configured = true;
    ++channel.revision;
    appliedChannelId_ = command.channelId;
    appliedRevision_ = channel.revision;
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteStart(const Detail::CoreCommand &command)
{
    auto &channel = channels_.at(command.channelId);
    if (channel.state != CoreState::READY || !channel.configured) {
        return Result(false, "start requires Ready with a valid configuration");
    }
    if (command.mode != RunMode::MANUAL && command.mode != RunMode::CONTINUOUS) {
        return Result(false, "unsupported run mode");
    }
    if (channel.runId == CORE_IDENTITY_MAX) {
        return Result(false, "run identity exhausted");
    }
    ApplyChannelPlan(command.channelId);
    device_->EnableRf();
    channel.mode = command.mode;
    ++channel.runId;
    channel.frameSeq = 0U;
    channel.state = CoreState::RUNNING;
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteTrigger(const Detail::CoreCommand &command)
{
    const auto &channel = channels_.at(command.channelId);
    if (channel.state != CoreState::RUNNING || channel.mode != RunMode::MANUAL || command.runId != channel.runId ||
        command.revision != channel.revision) {
        return Result(false, "trigger requires the current manual run and configuration");
    }
    AcquireFrame(command.channelId);
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteStop(const Detail::CoreCommand &command)
{
    auto &channel = channels_.at(command.channelId);
    if (command.runId == 0U || command.runId != channel.runId || channel.state == CoreState::UNINITIALIZED ||
        channel.state == CoreState::CLOSED) {
        return Result(false, "stop does not match an initialized current run");
    }
    if (channel.state == CoreState::RUNNING) {
        channel.state = CoreState::READY;
    }
    // 空闲或故障状态下，匹配当前运行的停止命令具有幂等性，不清除故障，也不关闭射频。
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteRecover()
{
    if (deviceState_ != CoreState::FAULTED) {
        return Result(false, "recover requires Faulted");
    }
    for (const auto &entry : channels_) {
        if (entry.second.hasSuccessfulPlan && entry.second.revision == CORE_IDENTITY_MAX) {
            return Result(false, "configuration revision exhausted");
        }
    }
    deviceIdn_ = device_->Initialize();
    appliedChannelId_ = 0U;
    // 先真实验证所有已提交计划，再一起推进各通道版本；任何失败保持全局故障。
    for (const auto &entry : channels_) {
        if (entry.second.hasSuccessfulPlan) {
            device_->ApplyPlan(entry.second.plan);
        }
    }
    for (auto &entry : channels_) {
        auto &channel = entry.second;
        if (channel.hasSuccessfulPlan) {
            channel.configured = true;
            ++channel.revision;
            appliedChannelId_ = entry.first;
            appliedRevision_ = channel.revision;
        }
        channel.state = CoreState::READY;
    }
    deviceState_ = CoreState::READY;
    error_.clear();
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteClose()
{
    try {
        device_->Shutdown();
    } catch (const std::exception &error) {
        deviceState_ = CoreState::CLOSED;
        for (auto &entry : channels_) {
            entry.second.state = CoreState::CLOSED;
            entry.second.configured = false;
        }
        error_ = error.what();
        PublishView();
        VnaLog::Error(VnaLog::Module::CORE, "设备关闭失败，原因={}", error_);
        return Result(false, error_);
    }
    deviceState_ = CoreState::CLOSED;
    for (auto &entry : channels_) {
        entry.second.state = CoreState::CLOSED;
        entry.second.configured = false;
    }
    return Result(true);
}

unsigned AcquisitionCore::NextContinuousChannel() const
{
    auto first = channels_.upper_bound(lastScheduledChannelId_);
    for (std::size_t count = 0U; count < channels_.size(); ++count) {
        if (first == channels_.end()) {
            first = channels_.begin();
        }
        if (first->second.state == CoreState::RUNNING && first->second.mode == RunMode::CONTINUOUS) {
            return first->first;
        }
        ++first;
    }
    return 0U;
}

void AcquisitionCore::WorkerLoop()
{
    auto nextFrame = std::chrono::steady_clock::now();
    for (;;) {
        std::unique_lock<std::mutex> guard(queueMutex_);
        while (queue_.empty()) {
            if (NextContinuousChannel() != 0U) {
                if (!wake_.wait_until(guard, nextFrame, [this] {
                        return !queue_.empty();
                    })) {
                    break;
                }
            } else {
                wake_.wait(guard, [this] {
                    return !queue_.empty();
                });
            }
        }
        if (!queue_.empty()) {
            Detail::CoreCommand command = std::move(queue_.front());
            queue_.pop_front();
            guard.unlock();
            auto answer = Execute(command);
            command.promise.set_value(std::move(answer));
            if (command.kind == Detail::CoreCommandKind::CLOSE) {
                return;
            }
        } else {
            guard.unlock();
        }
        // 每个命令周期也检查调度期限，队列持续有命令时不会饿死连续扫描。
        unsigned nextChannel = 0U;
        {
            std::lock_guard<std::mutex> scheduleGuard(queueMutex_);
            nextChannel = NextContinuousChannel();
        }
        if (nextChannel != 0U && std::chrono::steady_clock::now() >= nextFrame) {
            try {
                lastScheduledChannelId_ = nextChannel;
                AcquireFrame(nextChannel);
            } catch (const std::exception &error) {
                Fault(error.what());
            } catch (...) {
                Fault("unknown continuous acquisition exception");
            }
            nextFrame = std::chrono::steady_clock::now() + g_continuousFrameInterval;
        }
    }
}

}  // 命名空间 VnaCore
