#include "AcquisitionTestSupport.h"

#include <utility>

namespace vna::test {

Result<CommandId> FakeVnaHardware::commandResultLocked(std::optional<VnaError> &rejection)
{
    if (rejection) {
        VnaError error = std::move(*rejection);
        rejection.reset();
        return Result<CommandId>::failure(std::move(error));
    }
    return Result<CommandId>::success(nextCommandId_++);
}

Result<CommandId> FakeVnaHardware::connectAsync()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<VnaError> rejection;
    return commandResultLocked(rejection);
}

Result<CommandId> FakeVnaHardware::prepareAsync(VnaSweepPlan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++prepareCalls_;
    commandChanged_.notify_all();
    return commandResultLocked(nextPrepareRejection_);
}

Result<CommandId> FakeVnaHardware::startAsync(ConfigurationId configurationId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++startCalls_;
    auto result = commandResultLocked(nextStartRejection_);
    if (result.hasValue()) {
        lastStartConfiguration_ = configurationId;
        lastStartCommand_ = result.value();
    }
    commandChanged_.notify_all();
    return result;
}

Result<CommandId> FakeVnaHardware::abortAsync(SweepId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++abortCalls_;
    auto result = commandResultLocked(nextAbortRejection_);
    if (result.hasValue()) {
        lastAbortCommand_ = result.value();
    }
    commandChanged_.notify_all();
    return result;
}

Result<CommandId> FakeVnaHardware::disconnectAsync()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<VnaError> rejection;
    return commandResultLocked(rejection);
}

void FakeVnaHardware::rejectNextPrepare(VnaError error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextPrepareRejection_ = std::move(error);
}

void FakeVnaHardware::rejectNextStart(VnaError error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextStartRejection_ = std::move(error);
}

void FakeVnaHardware::rejectNextAbort(VnaError error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextAbortRejection_ = std::move(error);
}

}  // namespace vna::test
