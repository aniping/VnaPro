/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_core/acquisition_core.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "device/native/native_device_session.h"
#include "device/plan_preparation_error.h"
#include "vna_core/frame_layout.h"

namespace VnaCore {
namespace {

constexpr std::size_t QUEUE_CAPACITY = 64U;
constexpr std::size_t FRAME_BUDGET = 64U * 1024U * 1024U;
constexpr std::uint64_t CORE_IDENTITY_MAX = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::chrono::milliseconds g_continuousFrameInterval(10);

std::string Validate(const SweepPlan &plan)
{
    const std::size_t count = plan.frequencyRaw.size();
    if (count == 0U || count > (std::numeric_limits<std::uint32_t>::max() - 1U) / TABLE_VALUES_PER_POINT) {
        return "frequency count is empty or exceeds native lengths";
    }
    if (!plan.frequencyHz.empty() && plan.frequencyHz.size() != count) {
        return "Hz axis length differs from raw axis";
    }
    for (const double value : plan.frequencyHz) {
        if (!std::isfinite(value)) {
            return "Hz axis contains a non-finite value";
        }
    }
    if (plan.receiverMask == 0U || (plan.receiverMask & ~ALL_RECEIVERS_MASK) != 0U) {
        return "receiver mask must select ports 1..4";
    }
    if (plan.sourcePorts.empty() || plan.sourcePorts.size() > PHYSICAL_PORT_COUNT) {
        return "source ports must be a nonempty set of ports 1..4";
    }
    std::uint32_t sources = 0U;
    for (const auto source : plan.sourcePorts) {
        if (source < 1U || source > PHYSICAL_PORT_COUNT || (sources & (1U << (source - 1U))) != 0U) {
            return "source port is out of range or duplicated";
        }
        sources |= 1U << (source - 1U);
    }
    if (count > FRAME_BUDGET / (IQ_VALUES_PER_POINT * sizeof(double) * plan.sourcePorts.size())) {
        return "IQ frame exceeds the core software memory budget";
    }
    if (plan.tableData.size() != count * TABLE_VALUES_PER_POINT) {
        return "opaque table must contain exactly 9N values";
    }
    return std::string();
}

std::unique_ptr<Detail::DeviceSession> ValidateDevice(std::unique_ptr<Detail::DeviceSession> device)
{
    if (!device) {
        throw std::invalid_argument("core requires an owned device session");
    }
    return device;
}

}  // 匿名命名空间

AcquisitionCore::AcquisitionCore(const std::string &path)
    : AcquisitionCore(std::make_unique<Detail::NativeDeviceSession>(path))
{
}

Ticket AcquisitionCore::Initialize()
{
    return Submit(Detail::CoreCommandKind::INITIALIZE);
}

Ticket AcquisitionCore::Configure(SweepPlan plan)
{
    return Submit(Detail::CoreCommandKind::CONFIGURE, std::move(plan));
}

Ticket AcquisitionCore::Start(RunMode mode)
{
    return Submit(Detail::CoreCommandKind::START, SweepPlan(), mode);
}

Ticket AcquisitionCore::TriggerOnce(std::uint64_t runId, std::uint64_t revision)
{
    return Submit(Detail::CoreCommandKind::TRIGGER, SweepPlan(), RunMode::MANUAL, runId, revision);
}

Ticket AcquisitionCore::Stop(std::uint64_t runId)
{
    return Submit(Detail::CoreCommandKind::STOP, SweepPlan(), RunMode::MANUAL, runId);
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

Ticket AcquisitionCore::Rejected(const std::string &message) const
{
    const CoreView view = ReadView();
    CommandResult answer;
    answer.message = message;
    answer.runId = view.runId;
    answer.configRevision = view.configRevision;
    answer.frameSeq = view.frame && view.frame->runId == view.runId ? view.frame->frameSeq : 0U;
    std::promise<CommandResult> promise;
    auto ticket = promise.get_future().share();
    promise.set_value(std::move(answer));
    return ticket;
}

Ticket AcquisitionCore::Submit(
    Detail::CoreCommandKind kind, SweepPlan plan, RunMode mode, std::uint64_t runId, std::uint64_t revision)
{
    if (kind == Detail::CoreCommandKind::CONFIGURE) {
        const auto error = Validate(plan);
        if (!error.empty()) {
            return Rejected(error);
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
            queue_.push_back(Detail::CoreCommand{kind, std::move(plan), mode, runId, revision, std::move(promise)});
        }
    }
    if (!rejection.empty()) {
        return Rejected(rejection);
    }
    wake_.notify_one();
    return ticket;
}

CoreView AcquisitionCore::ReadView() const
{
    std::lock_guard<std::mutex> guard(resultMutex_);
    return published_;
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

CommandResult AcquisitionCore::Result(bool ok, const std::string &message) const
{
    CommandResult answer;
    answer.ok = ok;
    answer.message = message;
    answer.runId = runId_;
    answer.configRevision = revision_;
    answer.frameSeq = frameSeq_;
    return answer;
}

void AcquisitionCore::PublishView()
{
    CoreView view;
    view.state = state_;
    view.runId = runId_;
    view.configRevision = revision_;
    view.frame = frame_;
    view.error = error_;
    view.deviceIdn = deviceIdn_;
    {
        std::lock_guard<std::mutex> guard(resultMutex_);
        std::swap(published_, view);
    }
    // 释放发布锁后，再销毁可能占用较大内存的上一帧。
}

void AcquisitionCore::Fault(const std::string &error)
{
    state_ = CoreState::FAULTED;
    configured_ = false;
    error_ = error;
    PublishView();
}

void AcquisitionCore::AcquireFrame()
{
    if (frameSeq_ == CORE_IDENTITY_MAX) {
        throw std::overflow_error("frame sequence exhausted");
    }
    MeasurementFrame completed;
    completed.runId = runId_;
    completed.configRevision = revision_;
    completed.frameSeq = frameSeq_ + 1U;
    completed.plan = plan_;
    completed.ports.reserve(plan_.sourcePorts.size());
    for (const auto source : plan_.sourcePorts) {
        PortFrame port;
        port.sourcePort = source;
        port.iq = device_->AcquirePort(plan_, source);
        if (port.iq.size() != IQ_VALUES_PER_POINT * plan_.frequencyRaw.size()) {
            throw std::runtime_error("device returned an incomplete IQ port frame");
        }
        completed.ports.push_back(std::move(port));
    }
    // 发布后不保留可变别名；后续端口采集失败时，不替换 frame_。
    FramePtr immutable = std::make_shared<const MeasurementFrame>(std::move(completed));
    frameSeq_ = immutable->frameSeq;
    frame_ = std::move(immutable);
    PublishView();
}

CommandResult AcquisitionCore::Execute(Detail::CoreCommand &command)
{
    try {
        const CommandResult answer = ExecuteCommand(command);
        if (answer.ok) {
            PublishView();
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
            state_ = CoreState::CLOSED;
            PublishView();
        }
        return Result(false, error_);
    }
}

CommandResult AcquisitionCore::ExecuteCommand(Detail::CoreCommand &command)
{
    switch (command.kind) {
        case Detail::CoreCommandKind::INITIALIZE:
            return ExecuteInitialize();
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
    if (state_ != CoreState::UNINITIALIZED) {
        return Result(false, "initialize requires an uninitialized core");
    }
    deviceIdn_ = device_->Initialize();
    state_ = CoreState::READY;
    error_.clear();
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteConfigure(Detail::CoreCommand &command)
{
    if (state_ != CoreState::READY && state_ != CoreState::RUNNING) {
        return Result(false, "configure requires Ready or Running");
    }
    if (revision_ == CORE_IDENTITY_MAX) {
        return Result(false, "configuration revision exhausted");
    }
    device_->ApplyPlan(command.plan);
    plan_ = std::move(command.plan);
    hasSuccessfulPlan_ = true;
    configured_ = true;
    ++revision_;
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteStart(const Detail::CoreCommand &command)
{
    if (state_ != CoreState::READY || !configured_) {
        return Result(false, "start requires Ready with a valid configuration");
    }
    if (command.mode != RunMode::MANUAL && command.mode != RunMode::CONTINUOUS) {
        return Result(false, "unsupported run mode");
    }
    if (runId_ == CORE_IDENTITY_MAX) {
        return Result(false, "run identity exhausted");
    }
    device_->EnableRf();
    mode_ = command.mode;
    ++runId_;
    frameSeq_ = 0U;
    state_ = CoreState::RUNNING;
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteTrigger(const Detail::CoreCommand &command)
{
    if (state_ != CoreState::RUNNING || mode_ != RunMode::MANUAL || command.runId != runId_ ||
        command.revision != revision_) {
        return Result(false, "trigger requires the current manual run and configuration");
    }
    AcquireFrame();
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteStop(const Detail::CoreCommand &command)
{
    if (command.runId == 0U || command.runId != runId_ || state_ == CoreState::UNINITIALIZED ||
        state_ == CoreState::CLOSED) {
        return Result(false, "stop does not match an initialized current run");
    }
    if (state_ == CoreState::RUNNING) {
        state_ = CoreState::READY;
    }
    // 空闲或故障状态下，匹配当前运行的停止命令具有幂等性，不清除故障，也不关闭射频。
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteRecover()
{
    if (state_ != CoreState::FAULTED) {
        return Result(false, "recover requires Faulted");
    }
    if (hasSuccessfulPlan_ && revision_ == CORE_IDENTITY_MAX) {
        return Result(false, "configuration revision exhausted");
    }
    deviceIdn_ = device_->Initialize();
    if (hasSuccessfulPlan_) {
        device_->ApplyPlan(plan_);
        configured_ = true;
        ++revision_;
    }
    state_ = CoreState::READY;
    error_.clear();
    return Result(true);
}

CommandResult AcquisitionCore::ExecuteClose()
{
    try {
        device_->Shutdown();
    } catch (const std::exception &error) {
        state_ = CoreState::CLOSED;
        configured_ = false;
        error_ = error.what();
        PublishView();
        return Result(false, error_);
    }
    state_ = CoreState::CLOSED;
    configured_ = false;
    return Result(true);
}

void AcquisitionCore::WorkerLoop()
{
    auto nextFrame = std::chrono::steady_clock::now();
    for (;;) {
        std::unique_lock<std::mutex> guard(queueMutex_);
        while (queue_.empty()) {
            if (state_ == CoreState::RUNNING && mode_ == RunMode::CONTINUOUS) {
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
            try {
                AcquireFrame();
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
