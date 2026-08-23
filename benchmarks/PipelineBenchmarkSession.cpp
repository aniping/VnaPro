#include "PipelineBenchmarkSession.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "BenchmarkModel.h"
#include "PipelineBenchmarkEvents.h"

namespace vna::benchmark {
namespace {

    constexpr auto controlTimeout = std::chrono::seconds(15);

    std::shared_ptr<IVnaHardware> discoverAndOpen(VnaHardwareManager &manager)
    {
        detail::DiscoverySink discovery;
        try {
            (void)manager.discoverAsync(DiscoveryOptions{}, discovery);
            const auto descriptor = discovery.waitForSimulator();
            auto opened = manager.open(descriptor.deviceId);
            if (!opened.hasValue()) {
                detail::throwVnaError("opening simulator failed", opened.error());
            }
            return opened.takeValue();
        } catch (...) {
            // DiscoverySink 是管理器异步线程的借用；异常离开作用域前必须同步收走所有回调。
            manager.shutdown();
            throw;
        }
    }

    void connectHardware(const std::shared_ptr<IVnaHardware> &hardware)
    {
        detail::LifecycleSink sink;
        auto subscription = hardware->subscribe(sink);
        auto submitted = hardware->connectAsync();
        if (!submitted.hasValue()) {
            detail::throwVnaError("simulator connection was rejected", submitted.error());
        }
        sink.waitForCommand(submitted.value());
        if (hardware->state() != DeviceState::Idle) {
            throw std::runtime_error("simulator did not enter Idle after connection");
        }
    }

    void validateCompletion(const AcquisitionRunCompletion &completion)
    {
        const bool completed = (completion.termination == AcquisitionRunTermination::Completed) &&
                               (completion.completedSweepCount == 1) && !completion.error;
        if (!completed) {
            const std::string detail = completion.error ? completion.error->description : "unexpected terminal";
            throw std::runtime_error("pipeline run failed: " + detail);
        }
    }

}  // namespace

class PipelineBenchmarkSession::Impl final {
public:
    Impl(const std::string &driverDirectory, const std::string &configurationFile)
    {
        auto manager = VnaHardwareManager::create(driverDirectory, configurationFile);
        if (!manager.hasValue()) {
            detail::throwVnaError("hardware manager creation failed", manager.error());
        }
        manager_ = manager.takeValue();
        hardware_ = discoverAndOpen(*manager_);
        connectHardware(hardware_);
        const AcquisitionLimits limits{16U * 1024U * 1024U, 16U * 1024U * 1024U};
        auto engine = createAcquisitionEngine(hardware_, sink_, limits);
        if (!engine.hasValue()) {
            detail::throwVnaError("acquisition engine creation failed", engine.error());
        }
        engine_ = engine.takeValue();
    }

    ~Impl()
    {
        try {
            engine_.reset();
            if (manager_) {
                manager_->shutdown();
            }
        } catch (const std::exception &error) {
            std::fprintf(stderr, "benchmark shutdown failed: %s\n", error.what());
        } catch (...) {
            std::fputs("benchmark shutdown failed: unknown error\n", stderr);
        }
    }

    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    void prepare(double ifBandwidthHz)
    {
        auto submitted = engine_->prepareAsync(makeTargetPlan(ifBandwidthHz));
        if (!submitted.hasValue()) {
            detail::throwVnaError("pipeline prepare was rejected", submitted.error());
        }
        sink_.waitForPrepared(submitted.value());
    }

    double measureSweepSeconds()
    {
        const auto startedAt = detail::BenchmarkClock::now();
        auto submitted = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
        if (!submitted.hasValue()) {
            detail::throwVnaError("pipeline start was rejected", submitted.error());
        }
        const AcquisitionRunId runId = submitted.value();
        if (!sink_.waitForFrameOrCompletion(runId)) {
            (void)engine_->requestAbort(runId);
            (void)sink_.waitForCompletion(runId, std::chrono::seconds(5));
            throw std::runtime_error("pipeline did not publish a raw frame before termination or timeout");
        }
        const auto elapsed = sink_.frameArrival(runId) - startedAt;
        if (!sink_.waitForCompletion(runId, controlTimeout)) {
            throw std::runtime_error("pipeline run completion timed out");
        }
        validateCompletion(sink_.completion(runId));
        return std::chrono::duration<double>(elapsed).count();
    }

private:
    detail::AcquisitionSink sink_;
    std::unique_ptr<VnaHardwareManager> manager_;
    std::shared_ptr<IVnaHardware> hardware_;
    std::unique_ptr<IVnaAcquisitionEngine> engine_;
};

PipelineBenchmarkSession::PipelineBenchmarkSession(
    const std::string &driverDirectory, const std::string &configurationFile)
    : implementation_(std::make_unique<Impl>(driverDirectory, configurationFile))
{
}

PipelineBenchmarkSession::~PipelineBenchmarkSession() = default;

void PipelineBenchmarkSession::prepare(double ifBandwidthHz)
{
    implementation_->prepare(ifBandwidthHz);
}

double PipelineBenchmarkSession::measureSweepSeconds()
{
    return implementation_->measureSweepSeconds();
}

}  // namespace vna::benchmark
