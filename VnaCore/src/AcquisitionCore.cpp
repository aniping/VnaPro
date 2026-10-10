#include "vna_core/AcquisitionCore.h"

#include <utility>

#include "core/AcquisitionCoreImpl.h"
#include "device/native/NativeDeviceSession.h"

namespace vna_core {

AcquisitionCore::AcquisitionCore(const std::string &path)
    : AcquisitionCore(std::unique_ptr<detail::DeviceSession>(new detail::NativeDeviceSession(path)))
{
}

AcquisitionCore::AcquisitionCore(std::unique_ptr<detail::DeviceSession> device)
    : impl_(new detail::AcquisitionCoreImpl(std::move(device)))
{
}

AcquisitionCore::~AcquisitionCore() = default;

Ticket AcquisitionCore::initialize()
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Initialize);
}

Ticket AcquisitionCore::configure(SweepPlan plan)
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Configure, std::move(plan));
}

Ticket AcquisitionCore::start(RunMode mode)
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Start, SweepPlan(), mode);
}

Ticket AcquisitionCore::triggerOnce(std::uint64_t runId, std::uint64_t revision)
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Trigger, SweepPlan(), RunMode::Manual, runId, revision);
}

Ticket AcquisitionCore::stop(std::uint64_t runId)
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Stop, SweepPlan(), RunMode::Manual, runId);
}

Ticket AcquisitionCore::recover()
{
    return impl_->submit(detail::AcquisitionCoreImpl::Kind::Recover);
}

CoreView AcquisitionCore::readView() const
{
    return impl_->readView();
}

CommandResult AcquisitionCore::close()
{
    return impl_->close();
}

}  // namespace vna_core
