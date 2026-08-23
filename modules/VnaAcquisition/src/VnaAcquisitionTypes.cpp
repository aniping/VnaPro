#include "PreparedMapping.h"

#include <utility>

namespace vna {
namespace {

    bool sameWave(VnaWaveKey left, VnaWaveKey right) noexcept
    {
        return (left.kind == right.kind) && (left.port == right.port);
    }

    const detail::PreparedLayoutMapping *findExcitationLayout(
        const detail::PreparedMapping &mapping, ExcitationId excitationId) noexcept
    {
        for (const auto &layout : mapping.layouts) {
            if (layout.excitationId == excitationId) {
                return &layout;
            }
        }
        return nullptr;
    }

    const detail::PreparedWaveMapping *findWaveMapping(
        const detail::PreparedMapping &mapping, const detail::PreparedLayoutMapping &layout, VnaWaveKey wave) noexcept
    {
        if ((layout.waveMappingOffset > mapping.waves.size()) ||
            (layout.waveCount > (mapping.waves.size() - layout.waveMappingOffset))) {
            return nullptr;
        }
        for (std::size_t index = 0; index < layout.waveCount; ++index) {
            const auto &candidate = mapping.waves[layout.waveMappingOffset + index];
            if (sameWave(candidate.wave, wave)) {
                return &candidate;
            }
        }
        return nullptr;
    }

    bool validSeriesRange(
        const detail::PreparedMapping &mapping, const detail::PreparedLayoutMapping &layout,
        const detail::PreparedWaveMapping &wave, std::size_t valueCount, std::size_t qualityCount) noexcept
    {
        if ((layout.pointIndexOffset > mapping.pointIndices.size()) ||
            (layout.pointCount > (mapping.pointIndices.size() - layout.pointIndexOffset))) {
            return false;
        }
        if ((wave.destinationOffset > valueCount) || (layout.pointCount > (valueCount - wave.destinationOffset))) {
            return false;
        }
        return (wave.destinationOffset <= qualityCount) &&
               (layout.pointCount <= (qualityCount - wave.destinationOffset));
    }

}  // namespace

PreparedAcquisition::PreparedAcquisition(
    std::shared_ptr<const PreparedSweep> hardwarePrepared, std::size_t recordCount, std::size_t sampleCount,
    std::unique_ptr<const detail::PreparedMapping> mapping)
    : hardwarePrepared_(std::move(hardwarePrepared)),
      pointExcitationRecordCount_(recordCount),
      waveSampleCountPerSweep_(sampleCount),
      mapping_(std::move(mapping))
{
}

PreparedAcquisition::~PreparedAcquisition() = default;

const PreparedSweep &PreparedAcquisition::hardwarePrepared() const noexcept
{
    return *hardwarePrepared_;
}

std::size_t PreparedAcquisition::pointExcitationRecordCount() const noexcept
{
    return pointExcitationRecordCount_;
}

std::size_t PreparedAcquisition::waveSampleCountPerSweep() const noexcept
{
    return waveSampleCountPerSweep_;
}

RawSweepFrame::RawSweepFrame(
    AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
    std::shared_ptr<const PreparedAcquisition> prepared, detail::RawFrameAssembly assembly)
    : runId_(runId),
      sweepIndex_(sweepIndex),
      hardwareSweepId_(hardwareSweepId),
      prepared_(std::move(prepared)),
      values_(std::move(assembly.values)),
      quality_(std::move(assembly.quality))
{
}

RawSweepFrame::~RawSweepFrame() = default;

AcquisitionRunId RawSweepFrame::runId() const noexcept
{
    return runId_;
}

std::uint64_t RawSweepFrame::sweepIndex() const noexcept
{
    return sweepIndex_;
}

SweepId RawSweepFrame::hardwareSweepId() const noexcept
{
    return hardwareSweepId_;
}

const PreparedAcquisition &RawSweepFrame::prepared() const noexcept
{
    return *prepared_;
}

std::optional<RawWaveSeriesView> RawSweepFrame::findWave(ExcitationId excitationId, VnaWaveKey wave) const noexcept
{
    if (!prepared_ || !prepared_->mapping_) {
        return std::nullopt;
    }
    const auto &mapping = *prepared_->mapping_;
    const auto *layout = findExcitationLayout(mapping, excitationId);
    if (layout == nullptr) {
        return std::nullopt;
    }
    const auto *waveMapping = findWaveMapping(mapping, *layout, wave);
    if ((waveMapping == nullptr) ||
        !validSeriesRange(mapping, *layout, *waveMapping, values_.size(), quality_.size())) {
        return std::nullopt;
    }
    return RawWaveSeriesView{
        mapping.pointIndices.data() + layout->pointIndexOffset, values_.data() + waveMapping->destinationOffset,
        quality_.data() + waveMapping->destinationOffset, layout->pointCount};
}

IVnaAcquisitionSink::~IVnaAcquisitionSink() = default;
IVnaAcquisitionEngine::~IVnaAcquisitionEngine() = default;

}  // namespace vna
