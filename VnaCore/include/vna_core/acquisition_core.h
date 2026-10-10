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

namespace vna_core {

class AcquisitionCore final {
public:
    // Loads only an absolute driver path; initialization remains an asynchronous operation.
    explicit AcquisitionCore(const std::string &nativeLibraryPath);
    // Transfers device ownership; device calls are confined to the core worker.
    explicit AcquisitionCore(std::unique_ptr<Detail::DeviceSession> device);
    // The owner must release all other callers first; destruction drains and joins the worker.
    ~AcquisitionCore();

    AcquisitionCore(const AcquisitionCore &) = delete;
    AcquisitionCore &operator=(const AcquisitionCore &) = delete;
    AcquisitionCore(AcquisitionCore &&) = delete;
    AcquisitionCore &operator=(AcquisitionCore &&) = delete;

    // Returns a ticket for device initialization; only UNINITIALIZED accepts it.
    Ticket Initialize();
    // Copies the entire plan into the FIFO; success advances the committed configuration revision.
    Ticket Configure(SweepPlan plan);
    // Requires READY and a valid plan; success starts a new run with the requested mode.
    Ticket Start(RunMode mode);
    // Requires the current MANUAL run/revision; success publishes a complete immutable multi-source frame.
    Ticket TriggerOnce(std::uint64_t runId, std::uint64_t revision);
    // Rejects zero or stale run IDs; success is a FIFO frame barrier and keeps RF enabled.
    Ticket Stop(std::uint64_t runId);
    // Requires FAULTED; reapplies the last successful plan without restarting acquisition.
    Ticket Recover();
    // Returns one consistent state/frame snapshot; retained frames remain valid independently of the core.
    CoreView ReadView() const;
    // Owner-thread operation: closes admission, drains accepted work, attempts RF off, then joins.
    CommandResult Close();

private:
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

    // The following fields belong exclusively to the worker.
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

}  // namespace vna_core

#endif
