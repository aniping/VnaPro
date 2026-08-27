#include "MeasurementInternal.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vna {
namespace {

    bool validRange(std::size_t offset, std::size_t count, std::size_t size) noexcept
    {
        return (offset <= size) && (count <= (size - offset));
    }

    const detail::BoundObservation *findBoundObservation(
        const detail::PreparedMeasurementData &prepared, ObservationId id) noexcept
    {
        const auto found = std::lower_bound(
            prepared.observations.begin(), prepared.observations.end(), id,
            [](const auto &entry, auto key) { return entry.id < key; });
        if ((found == prepared.observations.end()) || (found->id != id)) {
            return nullptr;
        }
        return &*found;
    }

    bool outputRangeIsValid(
        const detail::BoundObservation &observation, const detail::ObservationFrameData &frame) noexcept
    {
        return (observation.sampleCount != 0) &&
               validRange(observation.outputOffset, observation.sampleCount, frame.values.size()) &&
               validRange(observation.outputOffset, observation.sampleCount, frame.status.size());
    }

    bool pointRangeIsValid(
        const detail::BoundObservation &observation, const detail::PreparedExcitationMapping &mapping,
        const detail::PreparedMeasurementData &prepared) noexcept
    {
        return (mapping.pointCount == observation.sampleCount) &&
               validRange(mapping.pointIndexOffset, mapping.pointCount, prepared.pointIndices.size());
    }

}  // namespace

CompiledMeasurement::CompiledMeasurement(std::unique_ptr<const detail::CompiledMeasurementData> data)
    : data_(std::move(data))
{
}

CompiledMeasurement::~CompiledMeasurement() = default;

std::size_t CompiledMeasurement::observationCount() const noexcept
{
    return data_ == nullptr ? 0 : data_->observations.size();
}

const ObservationDefinition &CompiledMeasurement::observation(std::size_t index) const
{
    if (data_ == nullptr) {
        throw std::out_of_range("compiled measurement has no observation data");
    }
    return data_->observations.at(index);
}

PreparedMeasurement::PreparedMeasurement(
    std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition,
    std::unique_ptr<const detail::PreparedMeasurementData> data)
    : compiled_(std::move(compiled)), acquisition_(std::move(acquisition)), data_(std::move(data))
{
}

PreparedMeasurement::~PreparedMeasurement() = default;

const CompiledMeasurement &PreparedMeasurement::compiled() const noexcept
{
    return *compiled_;
}

const PreparedAcquisition &PreparedMeasurement::acquisition() const noexcept
{
    return *acquisition_;
}

std::size_t PreparedMeasurement::observationSampleCount() const noexcept
{
    return data_ == nullptr ? 0 : data_->observationSampleCount;
}

ObservationFrame::ObservationFrame(
    std::shared_ptr<const PreparedMeasurement> prepared, std::unique_ptr<const detail::ObservationFrameData> data)
    : prepared_(std::move(prepared)), data_(std::move(data))
{
}

ObservationFrame::~ObservationFrame() = default;

AcquisitionRunId ObservationFrame::runId() const noexcept
{
    return data_ == nullptr ? 0 : data_->runId;
}

std::uint64_t ObservationFrame::sweepIndex() const noexcept
{
    return data_ == nullptr ? 0 : data_->sweepIndex;
}

SweepId ObservationFrame::hardwareSweepId() const noexcept
{
    return data_ == nullptr ? 0 : data_->hardwareSweepId;
}

const PreparedMeasurement &ObservationFrame::prepared() const noexcept
{
    return *prepared_;
}

std::optional<ObservationSeriesView> ObservationFrame::findObservation(ObservationId id) const noexcept
{
    if ((prepared_ == nullptr) || (prepared_->data_ == nullptr) || (data_ == nullptr)) {
        return std::nullopt;
    }
    const auto *found = findBoundObservation(*prepared_->data_, id);
    if ((found == nullptr) || (found->excitationMappingIndex >= prepared_->data_->excitations.size())) {
        return std::nullopt;
    }
    const auto &mapping = prepared_->data_->excitations[found->excitationMappingIndex];
    if (!outputRangeIsValid(*found, *data_) || !pointRangeIsValid(*found, mapping, *prepared_->data_)) {
        return std::nullopt;
    }
    return ObservationSeriesView{
        prepared_->data_->pointIndices.data() + mapping.pointIndexOffset, data_->values.data() + found->outputOffset,
        data_->status.data() + found->outputOffset, found->sampleCount};
}

}  // namespace vna
