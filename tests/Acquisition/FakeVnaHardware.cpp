#include "AcquisitionTestSupport.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <utility>

#include "VnaBufferPool.h"
#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaEventDispatcher.h"

namespace vna::test {
namespace {

    VnaStatusV1 blockStatus(std::uint32_t code, const char *message)
    {
        VnaStatusV1 result{};
        result.code = code;
        result.message.data = message;
        result.message.size = std::char_traits<char>::length(message);
        return result;
    }

    bool uniformBlockShape(const WaveBlockSpec &specification, std::size_t &pointCount)
    {
        if (specification.waves.empty() || specification.waves.front().empty() ||
            specification.waves.front().size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        pointCount = specification.waves.front().size();
        return std::all_of(specification.waves.begin(), specification.waves.end(), [pointCount](const auto &wave) {
            return wave.size() == pointCount;
        });
    }

}  // namespace

FakeVnaHardware::FakeVnaHardware(bool supportsAbort) : dispatcher_(detail::VnaEventDispatcher::create(128, 1))
{
    capabilities_.supportsAbort = supportsAbort;
    capabilities_.portCount = 4;
}

FakeVnaHardware::~FakeVnaHardware()
{
    pool_.reset();
    dispatcher_->shutdown();
}

DeviceDescriptor FakeVnaHardware::descriptor() const
{
    return DeviceDescriptor{"fake", "test", "fake", "1", 4};
}

VnaCapabilities FakeVnaHardware::capabilities() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

DeviceState FakeVnaHardware::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

ValidationReport FakeVnaHardware::validate(const VnaSweepPlan &) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++validationCalls_;
    return {};
}

Subscription FakeVnaHardware::subscribe(IVnaHardwareEventSink &sink)
{
    return dispatcher_->subscribe(sink);
}

void FakeVnaHardware::publishState(DeviceState current)
{
    DeviceState previous = DeviceState::Disconnected;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous = state_;
        state_ = current;
    }
    dispatcher_->postStateChanged(previous, current);
}

void FakeVnaHardware::publishPrepareSucceeded(CommandId commandId, PreparedSweep prepared)
{
    publishPrepareSnapshot(commandId, std::make_shared<const PreparedSweep>(std::move(prepared)));
}

void FakeVnaHardware::publishPrepareSnapshot(CommandId commandId, std::shared_ptr<const PreparedSweep> prepared)
{
    if (prepared && !prepared->layouts.empty()) {
        const std::size_t capacity = std::max<std::size_t>(prepared->report.waveBlockPointCapacity, 1);
        auto dispatcher = dispatcher_;
        pool_ = detail::VnaBufferPool::create(prepared->layouts, capacity, 8, [dispatcher](VnaWaveBlock block) {
            return dispatcher->postWaveBlock(std::move(block))
                       ? blockStatus(VNA_STATUS_OK, "")
                       : blockStatus(VNA_STATUS_DATA_LOSS, "test dispatcher rejected wave block");
        });
    }
    publishState(DeviceState::Prepared);
    dispatcher_->postPrepareCompleted(commandId, std::move(prepared), 1);
}

void FakeVnaHardware::publishStartSucceeded(CommandId commandId, SweepId sweepId)
{
    publishState(DeviceState::Running);
    dispatcher_->postStartCompleted(commandId, sweepId);
}

void FakeVnaHardware::publishAbortSucceeded(CommandId commandId, SweepId sweepId)
{
    dispatcher_->postAbortCompleted(commandId, sweepId);
}

void FakeVnaHardware::publishCommandFailed(CommandId commandId, VnaError error)
{
    dispatcher_->postCommandFailed(commandId, std::move(error));
}

bool FakeVnaHardware::publishWaveBlock(const WaveBlockSpec &specification)
{
    std::size_t pointCount = 0;
    if (!pool_ || !uniformBlockShape(specification, pointCount)) {
        return false;
    }
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = sizeof(view);
    const auto acquired = pool_->acquire(
        specification.sweepId, specification.layoutId, static_cast<std::uint32_t>(pointCount), &handle, &view);
    if (acquired.code != VNA_STATUS_OK) {
        return false;
    }
    if (view.waveCount != specification.waves.size()) {
        pool_->discard(handle);
        return false;
    }
    for (std::size_t wave = 0; wave < specification.waves.size(); ++wave) {
        for (std::size_t point = 0; point < pointCount; ++point) {
            const std::size_t index = (wave * view.pointCapacity) + point;
            view.values[index] = {
                specification.waves[wave][point].value.real(), specification.waves[wave][point].value.imag()};
            view.qualityFlags[index] = specification.waves[wave][point].quality;
        }
    }
    const auto committed = pool_->commit(
        handle, specification.firstPointIndex, static_cast<std::uint32_t>(pointCount), specification.sequence);
    if (committed.code != VNA_STATUS_OK) {
        pool_->discard(handle);
    }
    return committed.code == VNA_STATUS_OK;
}

void FakeVnaHardware::publishSweepFinished(SweepCompletion completion, DeviceState stateAfter)
{
    publishState(stateAfter);
    dispatcher_->postSweepCompleted(std::move(completion));
}

bool FakeVnaHardware::waitForStartCalls(std::size_t count)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return commandChanged_.wait_for(lock, std::chrono::seconds(2), [this, count] { return startCalls_ >= count; });
}

bool FakeVnaHardware::waitForAbortCalls(std::size_t count)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return commandChanged_.wait_for(lock, std::chrono::seconds(2), [this, count] { return abortCalls_ >= count; });
}

std::size_t FakeVnaHardware::validationCallCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return validationCalls_;
}

std::size_t FakeVnaHardware::prepareCallCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return prepareCalls_;
}

std::size_t FakeVnaHardware::startCallCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return startCalls_;
}

std::size_t FakeVnaHardware::abortCallCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return abortCalls_;
}

CommandId FakeVnaHardware::lastStartCommand() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastStartCommand_;
}

CommandId FakeVnaHardware::lastAbortCommand() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastAbortCommand_;
}

ConfigurationId FakeVnaHardware::lastStartConfiguration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastStartConfiguration_;
}

}  // namespace vna::test
