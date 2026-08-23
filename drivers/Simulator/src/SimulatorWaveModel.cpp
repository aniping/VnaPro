#include "SimulatorDevice.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace vna::simulator {
namespace {

    constexpr double pi = 3.14159265358979323846;
    constexpr double inverseSquareRootTwo = 0.70710678118654752440;
    // 归一化接收机白噪声密度；单位波幅对应 0 dB，仅用于仿真，不代表校准后的 dBm。
    constexpr double receiverNoiseAmplitudePerRootHertz = 3.162277660168379e-8;
    // 10 kHz 时独立 a/b 接收通道合成的高电平比值 trace noise 目标为 0.001 dB RMS。
    constexpr double ratioAmplitudeNoiseRmsAtTenKilohertz = 1.151292553589e-4;

    std::uint64_t mixBits(std::uint64_t value) noexcept
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    std::uint64_t sampleSeed(
        VnaSweepIdV1 sweepId, const Point &point, const Excitation &excitation, const Wave &wave) noexcept
    {
        std::uint64_t seed = mixBits(sweepId);
        seed = mixBits(seed ^ (static_cast<std::uint64_t>(point.identifier) << 32U) ^ excitation.identifier);
        seed = mixBits(seed ^ (static_cast<std::uint64_t>(wave.kind) << 32U) ^ wave.port);
        return seed;
    }

    double unitInterval(std::uint64_t bits) noexcept
    {
        constexpr double inverseMantissaRange = 1.0 / 9007199254740992.0;
        return (static_cast<double>(bits >> 11U) + 0.5) * inverseMantissaRange;
    }

    std::complex<double> standardComplexGaussian(std::uint64_t seed)
    {
        const double first = unitInterval(mixBits(seed));
        const double second = unitInterval(mixBits(seed ^ 0xd1b54a32d192ed03ULL));
        const double radius = std::sqrt(-2.0 * std::log(first));
        const double angle = 2.0 * pi * second;
        return std::complex<double>{radius * std::cos(angle), radius * std::sin(angle)};
    }

    std::complex<double> sourceWave(const Source &source)
    {
        // 功率值按 20log10 还原为复振幅，相位由度转换为弧度。
        const double amplitude = std::pow(10.0, source.powerDbm / 20.0);
        const double phase = source.phaseDegrees * pi / 180.0;
        return std::polar(amplitude, phase);
    }

    std::complex<double> openReflection(const Point &point, std::uint16_t port)
    {
        // 内建 DUT 是带短参考面延迟和微小损耗/纹波的独立开路端口，而非完全理想常数 1。
        const double normalizedFrequency = point.frequencyHz / 20.0e9;
        const double rippleDelay = (0.43 + (0.017 * static_cast<double>(port))) * 1.0e-9;
        const double ripple = 0.0008 * std::sin(2.0 * pi * point.frequencyHz * rippleDelay);
        const double magnitude = std::clamp(0.9985 - (0.0035 * std::sqrt(normalizedFrequency)) + ripple, 0.985, 0.9995);
        const double roundTripDelay = (0.18 + (0.025 * static_cast<double>(port))) * 1.0e-9;
        return std::polar(magnitude, -2.0 * pi * point.frequencyHz * roundTripDelay);
    }

    std::complex<double> isolationLeakage(const Point &point, std::uint16_t sourcePort, std::uint16_t wavePort)
    {
        const std::uint16_t lowPort = std::min(sourcePort, wavePort);
        const std::uint16_t highPort = std::max(sourcePort, wavePort);
        const std::uint16_t distance = highPort - lowPort;
        const double delay =
            (0.35 * static_cast<double>(distance) + 0.02 * static_cast<double>(sourcePort + wavePort)) * 1.0e-9;
        const double ripple = 1.0 + (0.25 * std::sin((2.0 * pi * point.frequencyHz * delay) + distance));
        const double magnitude = (1.0e-5 / static_cast<double>(distance)) * ripple;
        return std::polar(magnitude, -2.0 * pi * point.frequencyHz * delay);
    }

    std::complex<double> sourceContribution(const Point &point, const Source &source, const Wave &wave)
    {
        const std::complex<double> incident = sourceWave(source);
        if (wave.kind == VNA_WAVE_INCIDENT) {
            return source.port == wave.port ? incident : std::complex<double>{0.0, 0.0};
        }
        if (source.port == wave.port) {
            return incident * openReflection(point, wave.port);
        }
        return incident * isolationLeakage(point, source.port, wave.port);
    }

    std::complex<double> applyReceiverNoise(
        VnaSweepIdV1 sweepId, const Point &point, const Excitation &excitation, const Wave &wave,
        const std::complex<double> &nominal)
    {
        const double bandwidthScale = std::sqrt(point.ifBandwidthHz);
        const double additiveSigma = receiverNoiseAmplitudePerRootHertz * bandwidthScale * inverseSquareRootTwo;
        const double relativeSigma =
            ratioAmplitudeNoiseRmsAtTenKilohertz * std::sqrt(point.ifBandwidthHz / 10000.0) * inverseSquareRootTwo;
        const std::uint64_t seed = sampleSeed(sweepId, point, excitation, wave);
        const auto additiveNoise = standardComplexGaussian(seed ^ 0xa0761d6478bd642fULL) * additiveSigma;
        // 该项是各 a/b 接收通道独立的残余 trace noise，不冒充会在比值中完全抵消的源共同模抖动。
        const auto relativeNoise = standardComplexGaussian(seed ^ 0xe7037ed1a0b428dbULL) * relativeSigma;
        return (nominal * (std::complex<double>{1.0, 0.0} + relativeNoise)) + additiveNoise;
    }

}  // namespace

VnaComplex64V1 simulateWave(VnaSweepIdV1 sweepId, const Point &point, const Excitation &excitation, const Wave &wave)
{
    std::complex<double> nominal{0.0, 0.0};
    for (const auto &source : excitation.sources) {
        // 多源的确定性信号相干叠加，接收机噪声在合成后按通道只加入一次。
        nominal += sourceContribution(point, source, wave);
    }
    const auto measured = applyReceiverNoise(sweepId, point, excitation, wave, nominal);
    return VnaComplex64V1{measured.real(), measured.imag()};
}

}  // namespace vna::simulator
