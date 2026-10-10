#ifndef VNAPRO_VNA_CORE_ACQUISITION_CORE_IMPL_H
#define VNAPRO_VNA_CORE_ACQUISITION_CORE_IMPL_H

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "device/DeviceSession.h"
#include "vna_core/CoreTypes.h"

namespace vna_core {
namespace detail {

class AcquisitionCoreImpl {
public:
    enum class Kind {
        Initialize,
        Configure,
        Start,
        Trigger,
        Stop,
        Recover,
        Close
    };
    explicit AcquisitionCoreImpl(std::unique_ptr<DeviceSession> device);
    ~AcquisitionCoreImpl();
    Ticket submit(Kind kind, SweepPlan plan = SweepPlan(), RunMode mode = RunMode::Manual, std::uint64_t runId = 0U,
        std::uint64_t revision = 0U);
    CoreView readView() const;
    CommandResult close();

private:
    struct Command {
        Kind kind;
        SweepPlan plan;
        RunMode mode;
        std::uint64_t runId;
        std::uint64_t revision;
        std::shared_ptr<std::promise<CommandResult>> promise;
    };

    void workerLoop();
    CommandResult execute(Command &command);
    void acquireFrame();
    void fault(const std::string &error);
    void publishView();
    CommandResult result(bool ok, const std::string &message = std::string()) const;
    Ticket rejected(const std::string &message) const;

    std::unique_ptr<DeviceSession> device_;
    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::deque<Command> queue_;
    bool closing_ = false;
    Ticket closeTicket_;
    mutable std::mutex resultMutex_;
    CoreView published_;

    // The following fields belong exclusively to the worker.
    CoreState state_ = CoreState::Uninitialized;
    RunMode mode_ = RunMode::Manual;
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

}  // namespace detail
}  // namespace vna_core

#endif
