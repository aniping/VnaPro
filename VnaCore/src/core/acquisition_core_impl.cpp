#include "core/acquisition_core_impl.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "device/plan_preparation_error.h"

namespace vna_core {
namespace detail {
namespace {

constexpr std::size_t kQueueCapacity = 64U;
constexpr std::size_t kFrameBudget = 64U * 1024U * 1024U;

std::string validate(const SweepPlan &plan)
{
    const std::size_t count = plan.frequencyRaw.size();
    if (count == 0U || count > (std::numeric_limits<std::uint32_t>::max() - 1U) / 9U) {
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
    if (plan.receiverMask == 0U || (plan.receiverMask & ~0xFU) != 0U) {
        return "receiver mask must select ports 1..4";
    }
    if (plan.sourcePorts.empty() || plan.sourcePorts.size() > 4U) {
        return "source ports must be a nonempty set of ports 1..4";
    }
    std::uint32_t sources = 0U;
    for (const auto source : plan.sourcePorts) {
        if (source < 1U || source > 4U || (sources & (1U << (source - 1U))) != 0U) {
            return "source port is out of range or duplicated";
        }
        sources |= 1U << (source - 1U);
    }
    if (count > kFrameBudget / (16U * sizeof(double) * plan.sourcePorts.size())) {
        return "IQ frame exceeds the core software memory budget";
    }
    if (plan.tableData.size() != count * 9U) {
        return "opaque table must contain exactly 9N values";
    }
    return std::string();
}

}  // namespace

AcquisitionCoreImpl::AcquisitionCoreImpl(std::unique_ptr<DeviceSession> device)
    : device_(std::move(device)), worker_(&AcquisitionCoreImpl::workerLoop, this)
{
}

AcquisitionCoreImpl::~AcquisitionCoreImpl()
{
    close();
}

Ticket AcquisitionCoreImpl::rejected(const std::string &message) const
{
    const CoreView view = readView();
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

Ticket AcquisitionCoreImpl::submit(Kind kind, SweepPlan plan, RunMode mode, std::uint64_t runId, std::uint64_t revision)
{
    if (kind == Kind::Configure) {
        const auto error = validate(plan);
        if (!error.empty()) {
            return rejected(error);
        }
    }
    auto promise = std::make_shared<std::promise<CommandResult>>();
    Ticket ticket = promise->get_future().share();
    std::string rejection;
    {
        std::lock_guard<std::mutex> guard(queueMutex_);
        if (closing_) {
            rejection = "core is closing or closed";
        } else if (queue_.size() >= kQueueCapacity) {
            rejection = "core command queue is full";
        } else {
            queue_.push_back(Command{kind, std::move(plan), mode, runId, revision, promise});
        }
    }
    if (!rejection.empty()) {
        return rejected(rejection);
    }
    wake_.notify_one();
    return ticket;
}

CoreView AcquisitionCoreImpl::readView() const
{
    std::lock_guard<std::mutex> guard(resultMutex_);
    return published_;
}

CommandResult AcquisitionCoreImpl::close()
{
    // Lifecycle owners serialize close/destruction; submissions remain thread-safe.
    {
        std::lock_guard<std::mutex> guard(queueMutex_);
        if (!closing_) {
            auto promise = std::make_shared<std::promise<CommandResult>>();
            closeTicket_ = promise->get_future().share();
            queue_.push_back(Command{Kind::Close, SweepPlan(), RunMode::Manual, 0U, 0U, promise});
            closing_ = true;  // Reserved close entry is admitted even when the normal queue is full.
        }
    }
    wake_.notify_one();
    CommandResult answer = closeTicket_.get();
    if (worker_.joinable()) {
        worker_.join();
    }
    return answer;
}

CommandResult AcquisitionCoreImpl::result(bool ok, const std::string &message) const
{
    CommandResult answer;
    answer.ok = ok;
    answer.message = message;
    answer.runId = runId_;
    answer.configRevision = revision_;
    answer.frameSeq = frameSeq_;
    return answer;
}

void AcquisitionCoreImpl::publishView()
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
    // The previous potentially large frame is destroyed after releasing the publication lock.
}

void AcquisitionCoreImpl::fault(const std::string &error)
{
    state_ = CoreState::Faulted;
    configured_ = false;
    error_ = error;
    publishView();
}

void AcquisitionCoreImpl::acquireFrame()
{
    MeasurementFrame completed;
    completed.runId = runId_;
    completed.configRevision = revision_;
    completed.frameSeq = frameSeq_ + 1U;
    completed.plan = plan_;
    completed.ports.reserve(plan_.sourcePorts.size());
    for (const auto source : plan_.sourcePorts) {
        PortFrame port;
        port.sourcePort = source;
        port.iq = device_->acquirePort(plan_, source);
        if (port.iq.size() != 16U * plan_.frequencyRaw.size()) {
            throw std::runtime_error("device returned an incomplete IQ port frame");
        }
        completed.ports.push_back(std::move(port));
    }
    // No mutable alias survives publication. A failed later port never replaces frame_.
    FramePtr immutable(new MeasurementFrame(std::move(completed)));
    frameSeq_ = immutable->frameSeq;
    frame_ = std::move(immutable);
    publishView();
}

CommandResult AcquisitionCoreImpl::execute(Command &command)
{
    try {
        switch (command.kind) {
            case Kind::Initialize:
                if (state_ != CoreState::Uninitialized) {
                    return result(false, "initialize requires an uninitialized core");
                }
                deviceIdn_ = device_->initialize();
                state_ = CoreState::Ready;
                error_.clear();
                break;
            case Kind::Configure:
                if (state_ != CoreState::Ready && state_ != CoreState::Running) {
                    return result(false, "configure requires Ready or Running");
                }
                device_->applyPlan(command.plan);
                plan_ = std::move(command.plan);
                hasSuccessfulPlan_ = configured_ = true;
                ++revision_;
                break;
            case Kind::Start:
                if (state_ != CoreState::Ready || !configured_) {
                    return result(false, "start requires Ready with a valid configuration");
                }
                if (command.mode != RunMode::Manual && command.mode != RunMode::Continuous) {
                    return result(false, "unsupported run mode");
                }
                device_->enableRf();
                mode_ = command.mode;
                ++runId_;
                frameSeq_ = 0U;
                state_ = CoreState::Running;
                break;
            case Kind::Trigger:
                if (state_ != CoreState::Running || mode_ != RunMode::Manual || command.runId != runId_ ||
                    command.revision != revision_) {
                    return result(false, "trigger requires the current manual run and configuration");
                }
                acquireFrame();
                break;
            case Kind::Stop:
                if (command.runId == 0U || command.runId != runId_ || state_ == CoreState::Uninitialized ||
                    state_ == CoreState::Closed) {
                    return result(false, "stop does not match an initialized current run");
                }
                if (state_ == CoreState::Running) {
                    state_ = CoreState::Ready;
                }
                // Matching idle/Faulted Stop is idempotent and does not clear a fault or turn RF off.
                break;
            case Kind::Recover:
                if (state_ != CoreState::Faulted) {
                    return result(false, "recover requires Faulted");
                }
                deviceIdn_ = device_->initialize();
                if (hasSuccessfulPlan_) {
                    device_->applyPlan(plan_);
                    configured_ = true;
                    ++revision_;
                }
                state_ = CoreState::Ready;
                error_.clear();
                break;
            case Kind::Close:
                try {
                    device_->shutdown();
                } catch (const std::exception &error) {
                    state_ = CoreState::Closed;
                    configured_ = false;
                    error_ = error.what();
                    publishView();
                    return result(false, error_);
                }
                state_ = CoreState::Closed;
                configured_ = false;
                break;
        }
        publishView();
        return result(true);
    } catch (const PlanPreparationError &error) {
        if (command.kind != Kind::Configure) {
            fault(error.what());
        }
        return result(false, error.what());
    } catch (const std::exception &error) {
        fault(error.what());
        return result(false, error_);
    } catch (...) {
        fault("unknown device or frame processing exception");
        if (command.kind == Kind::Close) {
            state_ = CoreState::Closed;
            publishView();
        }
        return result(false, error_);
    }
}

void AcquisitionCoreImpl::workerLoop()
{
    auto nextFrame = std::chrono::steady_clock::now();
    for (;;) {
        std::unique_lock<std::mutex> guard(queueMutex_);
        while (queue_.empty()) {
            if (state_ == CoreState::Running && mode_ == RunMode::Continuous) {
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
            Command command = std::move(queue_.front());
            queue_.pop_front();
            guard.unlock();
            const auto answer = execute(command);
            command.promise->set_value(answer);
            if (command.kind == Kind::Close) {
                return;
            }
        } else {
            guard.unlock();
            try {
                acquireFrame();
            } catch (const std::exception &error) {
                fault(error.what());
            } catch (...) {
                fault("unknown continuous acquisition exception");
            }
            nextFrame = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
        }
    }
}

}  // namespace detail
}  // namespace vna_core
