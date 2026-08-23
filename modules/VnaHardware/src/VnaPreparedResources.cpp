#include "VnaSessionController.h"

#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include "VnaBufferPool.h"
#include "VnaFrameTracker.h"
#include "VnaLogger.h"
#include "VnaPreparedCapacity.h"
#include "VnaWavePoolCapacity.h"

namespace vna::detail {
namespace {

    VnaError resourceError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

}  // namespace

Result<VnaSessionController::PreparedResources> VnaSessionController::buildPreparedResources(PreparedSweep prepared)
{
    auto preparedPayload = inspectPreparedSweepPayload(prepared, limits_.maximumPreparedSweepPayloadBytes);
    if (!preparedPayload.hasValue()) {
        return Result<PreparedResources>::failure(preparedPayload.error());
    }
    auto poolPayload = inspectWavePoolPayload(prepared, limits_);
    if (!poolPayload.hasValue()) {
        return Result<PreparedResources>::failure(poolPayload.error());
    }
    try {
        auto snapshot = std::make_shared<const PreparedSweep>(std::move(prepared));
        auto tracker = std::make_shared<VnaFrameTracker>(snapshot->effectivePlan, snapshot->layouts);
        auto pool = VnaBufferPool::create(
            snapshot->layouts, snapshot->report.waveBlockPointCapacity, limits_.wavePoolSlotCount,
            [this](VnaWaveBlock block) { return acceptBlock(std::move(block)); });
        return Result<PreparedResources>::success(
            PreparedResources{std::move(snapshot), std::move(tracker), std::move(pool), preparedPayload.value()});
    } catch (const std::bad_alloc &) {
        return Result<PreparedResources>::failure(
            resourceError(VnaErrorCode::InternalError, "prepared runtime allocation failed"));
    } catch (const std::length_error &error) {
        return Result<PreparedResources>::failure(resourceError(VnaErrorCode::ConfigurationRejected, error.what()));
    } catch (const std::invalid_argument &error) {
        return Result<PreparedResources>::failure(resourceError(VnaErrorCode::ProtocolError, error.what()));
    } catch (const std::exception &error) {
        return Result<PreparedResources>::failure(resourceError(VnaErrorCode::InternalError, error.what()));
    }
}

void VnaSessionController::installPreparedResources(PreparedResources resources)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    activeConfiguration_ = resources.prepared->configurationId;
    prepared_ = std::move(resources.prepared);
    frameTracker_ = std::move(resources.tracker);
    pool_ = std::move(resources.pool);
}

void VnaSessionController::publishPreparedResources(CommandId commandId, PreparedResources resources)
{
    const auto published = resources.prepared;
    const std::size_t payloadBytes = resources.payloadBytes;
    installPreparedResources(std::move(resources));
    VnaLogger::write(
        VnaLogLevel::Info, "扫描计划准备完成，deviceId=" + descriptor_.deviceId +
                               "，configurationId=" + std::to_string(published->configurationId));
    transition(DeviceState::Prepared);
    dispatcher_->postPrepareCompleted(commandId, published, payloadBytes);
}

void VnaSessionController::clearPreparedConfiguration()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    activeConfiguration_ = 0;
    prepared_.reset();
    frameTracker_.reset();
    pool_.reset();
}

}  // namespace vna::detail
