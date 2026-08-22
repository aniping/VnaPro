#include "SimulatorDevice.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vna::simulator {
namespace {

    constexpr double pi = 3.14159265358979323846;

    std::complex<double> sourceWave(const Source &source)
    {
        // 功率值按 20log10 还原为复振幅，相位由度转换为弧度。
        const double amplitude = std::pow(10.0, source.powerDbm / 20.0);
        const double phase = source.phaseDegrees * pi / 180.0;
        return std::polar(amplitude, phase);
    }

    std::complex<double> sourceContribution(const Point &point, const Source &source, const Wave &wave)
    {
        const std::complex<double> incident = sourceWave(source);
        // 入射波只出现在源端口；其他端口的入射分量定义为零。
        if (wave.kind == VNA_WAVE_INCIDENT) {
            return source.port == wave.port ? incident : std::complex<double>{0.0, 0.0};
        }
        const std::uint16_t lowPort = std::min(source.port, wave.port);
        const std::uint16_t highPort = std::max(source.port, wave.port);
        const std::uint16_t distance = highPort - lowPort;
        // 出射模型使用确定性距离衰减和传播延迟，不模拟随机噪声，便于精确断言。
        const double gain = distance == 0 ? 0.1 : 0.5 / static_cast<double>(distance);
        const double delaySeconds = static_cast<double>(distance) * 1.0e-10;
        const double phase = -2.0 * pi * point.frequencyHz * delaySeconds;
        return incident * std::polar(gain, phase);
    }

}  // namespace

VnaComplex64V1 simulateWave(const Point &point, const Excitation &excitation, const Wave &wave)
{
    std::complex<double> result{0.0, 0.0};
    for (const auto &source : excitation.sources) {
        // 多源相干叠加，保证同一输入计划在每次测试中得到相同复数波值。
        result += sourceContribution(point, source, wave);
    }
    return VnaComplex64V1{result.real(), result.imag()};
}

}  // namespace vna::simulator
