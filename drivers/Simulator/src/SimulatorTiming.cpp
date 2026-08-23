#include "SimulatorTiming.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vna::simulator {
namespace {

    using Nanoseconds = std::chrono::nanoseconds;

    bool measurementWindow(double ifBandwidthHz, Nanoseconds &duration) noexcept
    {
        constexpr long double nanosecondsPerSecond = 1000000000.0L;
        if (!std::isfinite(ifBandwidthHz) || (ifBandwidthHz <= 0.0)) {
            return false;
        }
        const long double rounded = std::ceil(nanosecondsPerSecond / static_cast<long double>(ifBandwidthHz));
        const long double maximum = static_cast<long double>(std::numeric_limits<Nanoseconds::rep>::max());
        // 部分平台的 long double 只有 53 位尾数，int64 最大值会被舍入成 2^63；
        // 用保守的 >= 拒绝边界，避免随后浮点转整数触发未定义行为。
        if ((rounded < 1.0L) || (rounded >= maximum)) {
            return false;
        }
        duration = Nanoseconds{static_cast<Nanoseconds::rep>(rounded)};
        return true;
    }

    bool checkedAdd(Nanoseconds value, Nanoseconds &total) noexcept
    {
        if ((value.count() < 0) || (total.count() > (Nanoseconds::max().count() - value.count()))) {
            return false;
        }
        total += value;
        return true;
    }

    bool pointMeasurementTiming(const Point &point, std::uint32_t layoutId, Nanoseconds &duration) noexcept
    {
        std::uint32_t minimumLayoutId = std::numeric_limits<std::uint32_t>::max();
        std::size_t matchingExcitations = 0;
        for (const auto &excitation : point.excitations) {
            if (excitation.layoutId == 0) {
                return false;
            }
            minimumLayoutId = std::min(minimumLayoutId, excitation.layoutId);
            if (excitation.layoutId == layoutId) {
                ++matchingExcitations;
            }
        }
        if ((matchingExcitations != 1) || (point.dwellNanoseconds < 0)) {
            return false;
        }
        if (!measurementWindow(point.ifBandwidthHz, duration)) {
            return false;
        }
        if (layoutId == minimumLayoutId) {
            return checkedAdd(Nanoseconds{point.dwellNanoseconds}, duration);
        }
        return true;
    }

    bool pointSweepTiming(const Point &point, Nanoseconds &duration) noexcept
    {
        if (point.excitations.empty() || (point.dwellNanoseconds < 0)) {
            return false;
        }
        Nanoseconds window{0};
        if (!measurementWindow(point.ifBandwidthHz, window)) {
            return false;
        }
        duration = Nanoseconds{point.dwellNanoseconds};
        for (std::size_t index = 0; index < point.excitations.size(); ++index) {
            if (!checkedAdd(window, duration)) {
                return false;
            }
        }
        return true;
    }

}  // namespace

BatchMeasurementTiming calculateBatchMeasurementTiming(
    const std::vector<Point> &points, std::size_t firstPoint, std::uint32_t pointCount, std::uint32_t layoutId) noexcept
{
    const std::size_t count = pointCount;
    if ((layoutId == 0) || (count == 0) || (firstPoint >= points.size()) || (count > (points.size() - firstPoint))) {
        return {};
    }
    Nanoseconds total{0};
    for (std::size_t offset = 0; offset < count; ++offset) {
        Nanoseconds pointDuration{0};
        if (!pointMeasurementTiming(points[firstPoint + offset], layoutId, pointDuration) ||
            !checkedAdd(pointDuration, total)) {
            return {};
        }
    }
    return BatchMeasurementTiming{true, total};
}

BatchMeasurementTiming calculateSweepMeasurementTiming(const std::vector<Point> &points) noexcept
{
    if (points.empty()) {
        return {};
    }
    Nanoseconds total{0};
    for (const auto &point : points) {
        Nanoseconds pointDuration{0};
        if (!pointSweepTiming(point, pointDuration) || !checkedAdd(pointDuration, total) ||
            (total > simulatorMaximumSweepDuration)) {
            return {};
        }
    }
    return BatchMeasurementTiming{true, total};
}

}  // namespace vna::simulator
