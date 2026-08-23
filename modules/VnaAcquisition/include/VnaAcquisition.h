#ifndef VNA_ACQUISITION_VNA_ACQUISITION_H
#define VNA_ACQUISITION_VNA_ACQUISITION_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "Result.h"
#include "VnaHardware.h"

namespace vna {

using AcquisitionRunId = std::uint64_t;

struct AcquisitionLimits {
    std::size_t maximumRawFramePayloadBytes{0};
    std::size_t maximumPreparedMappingPayloadBytes{0};
};

enum class AcquisitionRunMode : std::uint8_t { Finite, Continuous };

struct AcquisitionRunRequest {
    AcquisitionRunMode mode{AcquisitionRunMode::Finite};
    std::uint32_t sweepCount{1};
};

enum class AcquisitionState : std::uint8_t { Unprepared, Preparing, Ready, Running, Fault };

enum class AcquisitionRunTermination : std::uint8_t { Completed, Stopped, Aborted, Failed };

struct AcquisitionRunCompletion {
    AcquisitionRunId runId{0};
    AcquisitionRunTermination termination{AcquisitionRunTermination::Completed};
    std::uint64_t completedSweepCount{0};
    std::optional<VnaError> error;
};

class PreparedAcquisition;
class RawSweepFrame;

namespace detail {
    class VnaAcquisitionEngine;
    struct PreparedMapping;
    struct RawFrameAssembly;

    Result<std::shared_ptr<const PreparedAcquisition>> compilePreparedAcquisition(
        std::shared_ptr<const PreparedSweep> hardwarePrepared, const AcquisitionLimits &limits);
    VnaError copyBlockIntoFrame(
        const PreparedAcquisition &prepared, const VnaWaveBlock &block, RawFrameAssembly &assembly);
    bool rawFrameAssemblyComplete(const PreparedAcquisition &prepared, const RawFrameAssembly &assembly) noexcept;
    Result<std::shared_ptr<const RawSweepFrame>> freezeRawSweepFrame(
        AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
        std::shared_ptr<const PreparedAcquisition> prepared, RawFrameAssembly assembly);
}  // namespace detail

class PreparedAcquisition final {
public:
    ~PreparedAcquisition();
    PreparedAcquisition(const PreparedAcquisition &) = delete;
    PreparedAcquisition &operator=(const PreparedAcquisition &) = delete;
    PreparedAcquisition(PreparedAcquisition &&) = delete;
    PreparedAcquisition &operator=(PreparedAcquisition &&) = delete;

    // 返回借用引用，有效期不超过本 PreparedAcquisition。
    const PreparedSweep &hardwarePrepared() const noexcept;
    std::size_t pointExcitationRecordCount() const noexcept;
    std::size_t waveSampleCountPerSweep() const noexcept;

private:
    friend class RawSweepFrame;
    friend class detail::VnaAcquisitionEngine;
    friend Result<std::shared_ptr<const PreparedAcquisition>> detail::compilePreparedAcquisition(
        std::shared_ptr<const PreparedSweep> hardwarePrepared, const AcquisitionLimits &limits);
    friend VnaError detail::copyBlockIntoFrame(
        const PreparedAcquisition &prepared, const VnaWaveBlock &block, detail::RawFrameAssembly &assembly);
    friend bool detail::rawFrameAssemblyComplete(
        const PreparedAcquisition &prepared, const detail::RawFrameAssembly &assembly) noexcept;

    PreparedAcquisition(
        std::shared_ptr<const PreparedSweep> hardwarePrepared, std::size_t recordCount, std::size_t sampleCount,
        std::unique_ptr<const detail::PreparedMapping> mapping);

    std::shared_ptr<const PreparedSweep> hardwarePrepared_;
    std::size_t pointExcitationRecordCount_{0};
    std::size_t waveSampleCountPerSweep_{0};
    std::unique_ptr<const detail::PreparedMapping> mapping_;
};

struct RawWaveSeriesView {
    // 三个借用区间长度相同；其有效期不得超过所属 RawSweepFrame。
    const std::uint32_t *pointIndices{nullptr};
    const std::complex<double> *values{nullptr};
    const std::uint32_t *quality{nullptr};
    std::size_t size{0};
};

class RawSweepFrame final {
public:
    ~RawSweepFrame();
    RawSweepFrame(const RawSweepFrame &) = delete;
    RawSweepFrame &operator=(const RawSweepFrame &) = delete;
    RawSweepFrame(RawSweepFrame &&) = delete;
    RawSweepFrame &operator=(RawSweepFrame &&) = delete;

    AcquisitionRunId runId() const noexcept;
    std::uint64_t sweepIndex() const noexcept;
    SweepId hardwareSweepId() const noexcept;
    // 返回借用引用，有效期不超过本 RawSweepFrame。
    const PreparedAcquisition &prepared() const noexcept;
    std::optional<RawWaveSeriesView> findWave(ExcitationId excitationId, VnaWaveKey wave) const noexcept;

private:
    friend class detail::VnaAcquisitionEngine;
    friend Result<std::shared_ptr<const RawSweepFrame>> detail::freezeRawSweepFrame(
        AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
        std::shared_ptr<const PreparedAcquisition> prepared, detail::RawFrameAssembly assembly);

    RawSweepFrame(
        AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
        std::shared_ptr<const PreparedAcquisition> prepared, detail::RawFrameAssembly assembly);

    AcquisitionRunId runId_{0};
    std::uint64_t sweepIndex_{0};
    SweepId hardwareSweepId_{0};
    std::shared_ptr<const PreparedAcquisition> prepared_;
    std::vector<std::complex<double>> values_;
    std::vector<std::uint32_t> quality_;
};

class IVnaAcquisitionSink {
public:
    IVnaAcquisitionSink() = default;
    virtual ~IVnaAcquisitionSink();
    IVnaAcquisitionSink(const IVnaAcquisitionSink &) = delete;
    IVnaAcquisitionSink &operator=(const IVnaAcquisitionSink &) = delete;
    IVnaAcquisitionSink(IVnaAcquisitionSink &&) = delete;
    IVnaAcquisitionSink &operator=(IVnaAcquisitionSink &&) = delete;

    // 回调在硬件事件线程串行执行；Sink 必须比 Engine 存活更久，且回调内不得销毁 Engine。
    // Prepared 成功结果和 frame 均保证非空、不可变；Sink 可按需保留其 shared_ptr 共享所有权。
    virtual void onPrepared(CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome) = 0;
    virtual void onSweepReady(std::shared_ptr<const RawSweepFrame> frame) = 0;
    // completion 是仅在本次回调期间有效的借用；跨回调保存时必须复制。
    virtual void onRunCompleted(const AcquisitionRunCompletion &completion) = 0;
};

class IVnaAcquisitionEngine {
public:
    IVnaAcquisitionEngine() = default;
    virtual ~IVnaAcquisitionEngine();
    IVnaAcquisitionEngine(const IVnaAcquisitionEngine &) = delete;
    IVnaAcquisitionEngine &operator=(const IVnaAcquisitionEngine &) = delete;
    IVnaAcquisitionEngine(IVnaAcquisitionEngine &&) = delete;
    IVnaAcquisitionEngine &operator=(IVnaAcquisitionEngine &&) = delete;

    // 公开方法线程安全；终态回调交付期间，非回调线程的调用会等待。
    virtual Result<CommandId> prepareAsync(VnaSweepPlan plan) = 0;
    virtual Result<AcquisitionRunId> start(AcquisitionRunRequest request) = 0;
    virtual VnaError requestStop(AcquisitionRunId runId) = 0;
    virtual VnaError requestAbort(AcquisitionRunId runId) = 0;
    virtual AcquisitionState state() const = 0;
};

// hardware 必须非空，Engine 在自身生命周期内持有其共享所有权；成功结果保证包含非空唯一所有权。
Result<std::unique_ptr<IVnaAcquisitionEngine>> createAcquisitionEngine(
    std::shared_ptr<IVnaHardware> hardware, IVnaAcquisitionSink &sink, AcquisitionLimits limits);

}  // namespace vna

#endif
