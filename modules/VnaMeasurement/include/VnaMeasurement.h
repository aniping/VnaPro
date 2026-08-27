#ifndef VNA_MEASUREMENT_VNA_MEASUREMENT_H
#define VNA_MEASUREMENT_VNA_MEASUREMENT_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "VnaAcquisition.h"

namespace vna {

using ObservationId = std::uint32_t;

struct DirectWaveExpression {
    VnaWaveKey wave{};
};

struct WaveRatioExpression {
    VnaWaveKey numerator{};
    VnaWaveKey denominator{};
};

using ObservationExpression = std::variant<DirectWaveExpression, WaveRatioExpression>;

struct ObservationDefinition {
    ObservationId id{0};
    ExcitationId excitationId{0};
    ObservationExpression expression{DirectWaveExpression{}};
};

struct MeasurementRequest {
    VnaSweepPlan sweepTemplate{};
    std::vector<ObservationDefinition> observations{};
};

struct MeasurementLimits {
    std::size_t maximumPreparedPayloadBytes{0};
    std::size_t maximumBuildPayloadBytes{0};
};

enum class CalculationQuality : std::uint32_t {
    Valid = 0,
    InputUnavailable = 1U << 0,
    ValueNonFinite = 1U << 1,
    DenominatorNonFinite = 1U << 2,
    DenominatorZero = 1U << 3,
    ResultNonFinite = 1U << 4
};

struct ObservationSampleStatus {
    std::uint32_t numeratorWaveQuality{0};
    std::uint32_t denominatorWaveQuality{0};
    std::uint32_t calculationQuality{0};
};

struct ObservationSeriesView {
    // 三个数组均借用所属 ObservationFrame 的不可变存储、长度同为 size，仅在该 Frame 存活期间有效。
    const std::uint32_t *pointIndices{nullptr};
    const std::complex<double> *values{nullptr};
    const ObservationSampleStatus *status{nullptr};
    std::size_t size{0};
};

namespace detail {
    struct CompiledMeasurementData;
    struct ObservationFrameData;
    struct PreparedMeasurementData;
}  // namespace detail

class CompiledMeasurement;
class ObservationFrame;
class PreparedMeasurement;

struct MeasurementCompilation {
    VnaSweepPlan sweepPlan{};
    std::shared_ptr<const CompiledMeasurement> compiled{};
};

Result<MeasurementCompilation> compileMeasurement(MeasurementRequest &&request, MeasurementLimits limits);
Result<std::shared_ptr<const PreparedMeasurement>> bindMeasurement(
    std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition);
Result<std::shared_ptr<const ObservationFrame>> buildObservationFrame(
    std::shared_ptr<const PreparedMeasurement> prepared, const RawSweepFrame &rawFrame);

class CompiledMeasurement final {
public:
    ~CompiledMeasurement();
    CompiledMeasurement(const CompiledMeasurement &) = delete;
    CompiledMeasurement &operator=(const CompiledMeasurement &) = delete;
    CompiledMeasurement(CompiledMeasurement &&) = delete;
    CompiledMeasurement &operator=(CompiledMeasurement &&) = delete;

    std::size_t observationCount() const noexcept;
    const ObservationDefinition &observation(std::size_t index) const;

private:
    friend Result<MeasurementCompilation> compileMeasurement(MeasurementRequest &&request, MeasurementLimits limits);
    friend Result<std::shared_ptr<const PreparedMeasurement>> bindMeasurement(
        std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition);

    explicit CompiledMeasurement(std::unique_ptr<const detail::CompiledMeasurementData> data);

    std::unique_ptr<const detail::CompiledMeasurementData> data_;
};

class PreparedMeasurement final {
public:
    ~PreparedMeasurement();
    PreparedMeasurement(const PreparedMeasurement &) = delete;
    PreparedMeasurement &operator=(const PreparedMeasurement &) = delete;
    PreparedMeasurement(PreparedMeasurement &&) = delete;
    PreparedMeasurement &operator=(PreparedMeasurement &&) = delete;

    const CompiledMeasurement &compiled() const noexcept;
    const PreparedAcquisition &acquisition() const noexcept;
    std::size_t observationSampleCount() const noexcept;

private:
    friend class ObservationFrame;
    friend Result<std::shared_ptr<const PreparedMeasurement>> bindMeasurement(
        std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition);
    friend Result<std::shared_ptr<const ObservationFrame>> buildObservationFrame(
        std::shared_ptr<const PreparedMeasurement> prepared, const RawSweepFrame &rawFrame);

    PreparedMeasurement(
        std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition,
        std::unique_ptr<const detail::PreparedMeasurementData> data);

    std::shared_ptr<const CompiledMeasurement> compiled_;
    std::shared_ptr<const PreparedAcquisition> acquisition_;
    std::unique_ptr<const detail::PreparedMeasurementData> data_;
};

class ObservationFrame final {
public:
    ~ObservationFrame();
    ObservationFrame(const ObservationFrame &) = delete;
    ObservationFrame &operator=(const ObservationFrame &) = delete;
    ObservationFrame(ObservationFrame &&) = delete;
    ObservationFrame &operator=(ObservationFrame &&) = delete;

    AcquisitionRunId runId() const noexcept;
    std::uint64_t sweepIndex() const noexcept;
    SweepId hardwareSweepId() const noexcept;
    const PreparedMeasurement &prepared() const noexcept;
    std::optional<ObservationSeriesView> findObservation(ObservationId id) const noexcept;

private:
    friend Result<std::shared_ptr<const ObservationFrame>> buildObservationFrame(
        std::shared_ptr<const PreparedMeasurement> prepared, const RawSweepFrame &rawFrame);

    ObservationFrame(
        std::shared_ptr<const PreparedMeasurement> prepared, std::unique_ptr<const detail::ObservationFrameData> data);

    std::shared_ptr<const PreparedMeasurement> prepared_;
    std::unique_ptr<const detail::ObservationFrameData> data_;
};

}  // namespace vna

#endif
