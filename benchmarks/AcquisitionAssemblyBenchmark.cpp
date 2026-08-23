#include "BenchmarkModel.h"

#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "PreparedMapping.h"
#include "VnaBufferPool.h"
#include "VnaDriverSdk/VnaDriverAbi.h"

namespace vna::benchmark {
namespace {

    using BenchmarkClock = std::chrono::steady_clock;
    constexpr std::size_t assemblyWarmupFrames = 20;
    constexpr std::size_t assemblyMeasuredFrames = 400;

    VnaStatusV1 okStatus()
    {
        VnaStatusV1 status{};
        status.code = VNA_STATUS_OK;
        return status;
    }

    void requireStatus(const char *operation, const VnaStatusV1 &status)
    {
        if (status.code != VNA_STATUS_OK) {
            throw std::runtime_error(std::string{operation} + " failed with status " + std::to_string(status.code));
        }
    }

    void requireNoError(const char *operation, const VnaError &error)
    {
        if (error.code != VnaErrorCode::None) {
            throw std::runtime_error(std::string{operation} + ": " + error.description);
        }
    }

    std::shared_ptr<const PreparedAcquisition> compileTargetAcquisition()
    {
        auto hardwarePrepared = std::make_shared<PreparedSweep>(makeTargetPreparedSweep(10.0e6));
        const AcquisitionLimits limits{16U * 1024U * 1024U, 16U * 1024U * 1024U};
        auto compiled = detail::compilePreparedAcquisition(std::move(hardwarePrepared), limits);
        if (!compiled.hasValue() || !compiled.value()) {
            const std::string description =
                compiled.hasValue() ? "compiled acquisition is empty" : compiled.error().description;
            throw std::runtime_error("target acquisition compilation failed: " + description);
        }
        return compiled.takeValue();
    }

    struct IterationDurations {
        double copyOnlySeconds{0.0};
        double completeFrameSeconds{0.0};
    };

    class AssemblyFixture final {
    public:
        AssemblyFixture() : prepared_(compileTargetAcquisition())
        {
            blocks_.reserve(targetExcitationCount);
            pool_ = detail::VnaBufferPool::create(
                prepared_->hardwarePrepared().layouts, targetPointCount, targetExcitationCount,
                [this](VnaWaveBlock block) {
                    blocks_.push_back(std::move(block));
                    return okStatus();
                });
            buildSourceBlocks();
        }

        ~AssemblyFixture() = default;
        AssemblyFixture(const AssemblyFixture &) = delete;
        AssemblyFixture &operator=(const AssemblyFixture &) = delete;
        AssemblyFixture(AssemblyFixture &&) = delete;
        AssemblyFixture &operator=(AssemblyFixture &&) = delete;

        IterationDurations measureFrame(std::uint64_t sweepIndex)
        {
            const auto frameStartedAt = BenchmarkClock::now();
            auto assembly = allocateAssembly();
            const auto copyStartedAt = BenchmarkClock::now();
            for (const auto &block : blocks_) {
                requireNoError("raw block copy", detail::copyBlockIntoFrame(*prepared_, block, assembly));
            }
            const auto copyCompletedAt = BenchmarkClock::now();
            const auto frame = freezeFrame(sweepIndex, std::move(assembly));
            const auto frameCompletedAt = BenchmarkClock::now();
            consumeFrame(*frame);
            return IterationDurations{
                std::chrono::duration<double>(copyCompletedAt - copyStartedAt).count(),
                std::chrono::duration<double>(frameCompletedAt - frameStartedAt).count()};
        }

        double checksum() const
        {
            return checksum_;
        }

    private:
        detail::RawFrameAssembly allocateAssembly() const
        {
            auto allocated = detail::allocateRawFrameAssembly(*prepared_);
            if (!allocated.hasValue()) {
                throw std::runtime_error("raw frame allocation failed: " + allocated.error().description);
            }
            return allocated.takeValue();
        }

        std::shared_ptr<const RawSweepFrame> freezeFrame(
            std::uint64_t sweepIndex, detail::RawFrameAssembly assembly) const
        {
            auto frame = detail::freezeRawSweepFrame(1, sweepIndex, 1, prepared_, std::move(assembly));
            if (!frame.hasValue() || !frame.value()) {
                const std::string description = frame.hasValue() ? "raw frame is empty" : frame.error().description;
                throw std::runtime_error("raw frame freeze failed: " + description);
            }
            return frame.takeValue();
        }

        void buildSourceBlocks()
        {
            for (std::size_t index = 0; index < targetExcitationCount; ++index) {
                buildSourceBlock(index);
            }
            if (blocks_.size() != targetExcitationCount) {
                throw std::runtime_error("source block callback count is invalid");
            }
        }

        void buildSourceBlock(std::size_t index)
        {
            VnaBufferHandle handle = nullptr;
            VnaMutableWaveBlockViewV1 view{};
            view.structSize = static_cast<std::uint32_t>(sizeof(view));
            const auto layoutId = static_cast<LayoutId>(index + 1);
            requireStatus(
                "source block acquire",
                pool_->acquire(1, layoutId, static_cast<std::uint32_t>(targetPointCount), &handle, &view));
            try {
                fillSourceView(layoutId, view);
                const auto status = pool_->commit(handle, 0, static_cast<std::uint32_t>(targetPointCount), index);
                requireStatus("source block commit", status);
            } catch (...) {
                pool_->discard(handle);
                throw;
            }
        }

        static void fillSourceView(LayoutId layoutId, VnaMutableWaveBlockViewV1 &view)
        {
            if ((view.waveCount != targetWaveCount) || (view.pointCapacity < targetPointCount)) {
                throw std::runtime_error("source block view shape is invalid");
            }
            for (std::size_t wave = 0; wave < targetWaveCount; ++wave) {
                for (std::size_t point = 0; point < targetPointCount; ++point) {
                    const std::size_t offset = (wave * view.pointCapacity) + point;
                    const double value = static_cast<double>((layoutId * 100) + wave) + (point * 1.0e-6);
                    view.values[offset] = VnaComplex64V1{value, -value};
                    view.qualityFlags[offset] = VNA_QUALITY_VALID;
                }
            }
        }

        void consumeFrame(const RawSweepFrame &frame)
        {
            const auto view = frame.findWave(1, VnaWaveKey{WaveKind::Incident, 1});
            if (!view || (view->size != targetPointCount)) {
                throw std::runtime_error("frozen target wave is missing");
            }
            checksum_ += view->values[0].real() + view->values[view->size - 1].imag();
        }

        std::shared_ptr<const PreparedAcquisition> prepared_;
        std::shared_ptr<detail::VnaBufferPool> pool_;
        std::vector<VnaWaveBlock> blocks_;
        double checksum_{0.0};
    };

}  // namespace

AssemblyResult runAcquisitionAssemblyBenchmark()
{
    AssemblyFixture fixture;
    for (std::size_t index = 0; index < assemblyWarmupFrames; ++index) {
        (void)fixture.measureFrame(index);
    }
    std::vector<double> copyDurations;
    std::vector<double> frameDurations;
    copyDurations.reserve(assemblyMeasuredFrames);
    frameDurations.reserve(assemblyMeasuredFrames);
    for (std::size_t index = 0; index < assemblyMeasuredFrames; ++index) {
        const auto durations = fixture.measureFrame(assemblyWarmupFrames + index);
        copyDurations.push_back(durations.copyOnlySeconds);
        frameDurations.push_back(durations.completeFrameSeconds);
    }
    AssemblyResult result;
    result.warmupFrames = assemblyWarmupFrames;
    result.measuredFrames = assemblyMeasuredFrames;
    result.copyOnly = summarizeDurations(std::move(copyDurations));
    result.completeFrame = summarizeDurations(std::move(frameDurations));
    result.checksum = fixture.checksum();
    return result;
}

}  // namespace vna::benchmark
