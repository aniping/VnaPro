#include "MeasurementInternal.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vna::detail {
namespace {

    enum class ExpressionKind : std::uint8_t { Direct, Ratio };

    struct ExpressionIdentity {
        ExcitationId excitationId{0};
        ExpressionKind kind{ExpressionKind::Direct};
        VnaWaveKey numerator{};
        VnaWaveKey denominator{};

        bool operator==(const ExpressionIdentity &other) const noexcept
        {
            return (excitationId == other.excitationId) && (kind == other.kind) &&
                   (numerator.kind == other.numerator.kind) && (numerator.port == other.numerator.port) &&
                   (denominator.kind == other.denominator.kind) && (denominator.port == other.denominator.port);
        }
    };

    struct ExpressionIdentityHash {
        std::size_t operator()(const ExpressionIdentity &value) const noexcept
        {
            std::size_t result = value.excitationId;
            result = (result * 131U) + static_cast<std::uint8_t>(value.kind);
            result = (result * 131U) + static_cast<std::uint8_t>(value.numerator.kind);
            result = (result * 131U) + value.numerator.port;
            result = (result * 131U) + static_cast<std::uint8_t>(value.denominator.kind);
            return (result * 131U) + value.denominator.port;
        }
    };

    struct ObservedExcitation {
        std::size_t occurrenceCount{0};
        std::size_t observationCount{0};
        std::vector<VnaWaveKey> waves;
    };

    struct InspectionState {
        MeasurementShape shape{};
        bool hasRatio{false};
        std::unordered_map<ExcitationId, ObservedExcitation> excitations;
        std::unordered_set<ObservationId> observationIds;
        std::unordered_set<ExpressionIdentity, ExpressionIdentityHash> expressions;
    };

    VnaError invalidObservation(std::size_t index, const char *description)
    {
        return measurementError(
            VnaErrorCode::InvalidPlan, "observations[" + std::to_string(index) + "]: " + description);
    }

    bool validWave(VnaWaveKey wave) noexcept
    {
        const bool knownKind = (wave.kind == WaveKind::Incident) || (wave.kind == WaveKind::Outgoing);
        return knownKind && (wave.port != 0);
    }

    bool sameWave(VnaWaveKey left, VnaWaveKey right) noexcept
    {
        return (left.kind == right.kind) && (left.port == right.port);
    }

    bool waveLess(VnaWaveKey left, VnaWaveKey right) noexcept
    {
        if (left.kind != right.kind) {
            return left.kind == WaveKind::Incident;
        }
        return left.port < right.port;
    }

    VnaError inspectTemplateOccurrence(
        const VnaExcitationPlan &excitation, std::size_t pointIndex, std::size_t excitationIndex,
        InspectionState &state)
    {
        if (!excitation.requiredWaves.empty()) {
            std::string path = "points[" + std::to_string(pointIndex) + "].excitations[" +
                               std::to_string(excitationIndex) + "].requiredWaves must be empty";
            return measurementError(VnaErrorCode::InvalidPlan, std::move(path));
        }
        if (!checkedMeasurementAccumulate(excitation.sources.size(), state.shape.sourceCount)) {
            return measurementError(VnaErrorCode::UnsupportedPlan, "measurement source count overflows");
        }
        auto &record = state.excitations[excitation.excitationId];
        if (!checkedMeasurementAccumulate(1, record.occurrenceCount)) {
            return measurementError(VnaErrorCode::UnsupportedPlan, "excitation occurrence count overflows");
        }
        return {};
    }

    Result<bool> inspectSweepTemplate(const MeasurementRequest &request, InspectionState &state)
    {
        if (request.sweepTemplate.points.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<bool>::failure(
                measurementError(VnaErrorCode::UnsupportedPlan, "measurement point count exceeds uint32 indices"));
        }
        state.shape.pointCount = request.sweepTemplate.points.size();
        for (std::size_t pointIndex = 0; pointIndex < request.sweepTemplate.points.size(); ++pointIndex) {
            const auto &point = request.sweepTemplate.points[pointIndex];
            if (!checkedMeasurementAccumulate(point.excitations.size(), state.shape.excitationOccurrenceCount)) {
                return Result<bool>::failure(
                    measurementError(VnaErrorCode::UnsupportedPlan, "excitation occurrence count overflows"));
            }
            for (std::size_t excitationIndex = 0; excitationIndex < point.excitations.size(); ++excitationIndex) {
                VnaError error =
                    inspectTemplateOccurrence(point.excitations[excitationIndex], pointIndex, excitationIndex, state);
                if (error.code != VnaErrorCode::None) {
                    return Result<bool>::failure(std::move(error));
                }
            }
        }
        state.shape.excitationCount = state.excitations.size();
        state.shape.pointIndexCount = state.shape.excitationOccurrenceCount;
        return Result<bool>::success(true);
    }

    Result<ExpressionIdentity> inspectExpression(const ObservationDefinition &definition, std::size_t index)
    {
        ExpressionIdentity identity;
        identity.excitationId = definition.excitationId;
        if (const auto *direct = std::get_if<DirectWaveExpression>(&definition.expression)) {
            if (!validWave(direct->wave)) {
                return Result<ExpressionIdentity>::failure(invalidObservation(index, "direct wave is invalid"));
            }
            identity.numerator = direct->wave;
            return Result<ExpressionIdentity>::success(identity);
        }
        const auto *ratio = std::get_if<WaveRatioExpression>(&definition.expression);
        if (ratio == nullptr) {
            return Result<ExpressionIdentity>::failure(
                measurementError(VnaErrorCode::InternalError, "observation expression is valueless"));
        }
        if (!validWave(ratio->numerator) || !validWave(ratio->denominator)) {
            return Result<ExpressionIdentity>::failure(invalidObservation(index, "ratio wave is invalid"));
        }
        identity.kind = ExpressionKind::Ratio;
        identity.numerator = ratio->numerator;
        identity.denominator = ratio->denominator;
        return Result<ExpressionIdentity>::success(identity);
    }

    void appendExpressionWaves(const ExpressionIdentity &identity, ObservedExcitation &excitation)
    {
        excitation.waves.push_back(identity.numerator);
        if (identity.kind == ExpressionKind::Ratio) {
            excitation.waves.push_back(identity.denominator);
        }
    }

    VnaError inspectObservation(const ObservationDefinition &definition, std::size_t index, InspectionState &state)
    {
        if (definition.id == 0) {
            return invalidObservation(index, "ObservationId must be nonzero");
        }
        if (!state.observationIds.insert(definition.id).second) {
            return invalidObservation(index, "ObservationId is duplicated");
        }
        const auto excitation = state.excitations.find(definition.excitationId);
        if (excitation == state.excitations.end()) {
            return invalidObservation(index, "ExcitationId does not occur in the sweep template");
        }
        auto identity = inspectExpression(definition, index);
        if (!identity.hasValue()) {
            return identity.error();
        }
        if (!state.expressions.insert(identity.value()).second) {
            return invalidObservation(index, "excitation and expression are duplicated");
        }
        if (!checkedMeasurementAccumulate(1, excitation->second.observationCount)) {
            return measurementError(VnaErrorCode::UnsupportedPlan, "measurement observation count overflows");
        }
        state.hasRatio = state.hasRatio || (identity.value().kind == ExpressionKind::Ratio);
        appendExpressionWaves(identity.value(), excitation->second);
        return {};
    }

    Result<bool> inspectObservations(const MeasurementRequest &request, InspectionState &state)
    {
        if (request.observations.empty()) {
            return Result<bool>::failure(
                measurementError(VnaErrorCode::InvalidPlan, "measurement requires at least one observation"));
        }
        state.shape.observationCount = request.observations.size();
        state.observationIds.reserve(request.observations.size());
        state.expressions.reserve(request.observations.size());
        for (std::size_t index = 0; index < request.observations.size(); ++index) {
            VnaError error = inspectObservation(request.observations[index], index, state);
            if (error.code != VnaErrorCode::None) {
                return Result<bool>::failure(std::move(error));
            }
        }
        return Result<bool>::success(true);
    }

    VnaError addExcitationToModel(
        ExcitationId id, ObservedExcitation &source, CompilationModel &model, std::size_t &sampleCount)
    {
        if (source.observationCount == 0) {
            return measurementError(VnaErrorCode::InvalidPlan, "sweep template contains an unobserved ExcitationId");
        }
        std::sort(source.waves.begin(), source.waves.end(), waveLess);
        source.waves.erase(std::unique(source.waves.begin(), source.waves.end(), sameWave), source.waves.end());
        std::size_t excitationSamples = 0;
        if (!checkedMeasurementAccumulate(source.waves.size(), model.shape.dependencyCount) ||
            !checkedMeasurementMultiply(source.occurrenceCount, source.observationCount, excitationSamples) ||
            !checkedMeasurementAccumulate(excitationSamples, sampleCount)) {
            return measurementError(VnaErrorCode::UnsupportedPlan, "measurement shape size overflows");
        }
        model.excitations.push_back(ExcitationRequirements{id, source.occurrenceCount, std::move(source.waves)});
        return {};
    }

    Result<CompilationModel> finalizeInspection(InspectionState state, MeasurementLimits limits)
    {
        CompilationModel model;
        model.shape = state.shape;
        model.hasRatio = state.hasRatio;
        model.excitations.reserve(state.excitations.size());
        std::size_t sampleCount = 0;
        for (auto &entry : state.excitations) {
            VnaError error = addExcitationToModel(entry.first, entry.second, model, sampleCount);
            if (error.code != VnaErrorCode::None) {
                return Result<CompilationModel>::failure(std::move(error));
            }
        }
        model.shape.observationSampleCount = sampleCount;
        std::sort(model.excitations.begin(), model.excitations.end(), [](const auto &left, const auto &right) {
            return left.excitationId < right.excitationId;
        });
        auto payload = inspectMeasurementPayload(model.shape, limits);
        if (!payload.hasValue()) {
            return Result<CompilationModel>::failure(payload.error());
        }
        return Result<CompilationModel>::success(std::move(model));
    }

    Result<CompilationModel> inspectRequest(const MeasurementRequest &request, MeasurementLimits limits)
    {
        auto capacity = preflightMeasurementRequestCapacity(request, limits);
        if (!capacity.hasValue()) {
            return Result<CompilationModel>::failure(capacity.error());
        }
        InspectionState state;
        auto inspectedTemplate = inspectSweepTemplate(request, state);
        if (!inspectedTemplate.hasValue()) {
            return Result<CompilationModel>::failure(inspectedTemplate.error());
        }
        auto inspectedObservations = inspectObservations(request, state);
        if (!inspectedObservations.hasValue()) {
            return Result<CompilationModel>::failure(inspectedObservations.error());
        }
        return finalizeInspection(std::move(state), limits);
    }

    Result<CompilationModel> inspectionInternalFailure() noexcept
    {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        return Result<CompilationModel>::failure(std::move(error));
    }

}  // namespace

Result<CompilationModel> inspectMeasurementRequest(const MeasurementRequest &request, MeasurementLimits limits)
{
    try {
        return inspectRequest(request, limits);
    } catch (const std::bad_alloc &) {
        return inspectionInternalFailure();
    } catch (const std::length_error &) {
        return inspectionInternalFailure();
    } catch (...) {
        return inspectionInternalFailure();
    }
}

}  // namespace vna::detail
