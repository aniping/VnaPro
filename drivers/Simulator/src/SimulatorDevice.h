#ifndef VNA_DRIVER_SIMULATOR_SIMULATOR_DEVICE_H
#define VNA_DRIVER_SIMULATOR_SIMULATOR_DEVICE_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

#include "VnaDriverSdk/VnaDriverApi.h"

namespace vna::simulator {

// 能力表与执行器共用批大小，避免声明值和实际提交策略漂移。
inline constexpr std::uint32_t simulatorBatchPoints = 2;

struct Source {
    std::uint16_t port{0};
    double powerDbm{0.0};
    double phaseDegrees{0.0};
};

struct Wave {
    std::uint32_t kind{VNA_WAVE_INCIDENT};
    std::uint16_t port{0};
};

struct Excitation {
    // identifier 来自计划；layoutId 在 Prepare 时按跨频点稳定波布局分配。
    std::uint16_t identifier{0};
    std::uint32_t layoutId{0};
    std::vector<Source> sources;
    std::vector<Wave> waves;
};

struct Point {
    // 这些都是从短生命周期 ABI 视图深拷贝出的驱动自有数据。
    std::uint32_t identifier{0};
    double frequencyHz{0.0};
    double ifBandwidthHz{0.0};
    std::int64_t dwellNanoseconds{0};
    std::vector<Excitation> excitations;
};

// 仿真设备也严格走公开 C ABI，用它验证宿主不会依赖某个真实仪器的私有行为。
class SimulatorDevice final {
public:
    explicit SimulatorDevice(VnaDriverHostApiV1 hostApi);
    VnaStatusV1 connect();
    VnaStatusV1 disconnect();
    VnaStatusV1 capabilities(VnaCapabilitiesViewV1 *view) const;
    VnaStatusV1 prepare(const VnaSweepPlanViewV1 *plan, VnaPrepareResultViewV1 *result);
    VnaStatusV1 execute(VnaSweepIdV1 sweepId);
    VnaStatusV1 requestAbort(VnaSweepIdV1 sweepId);

private:
    // 以移动方式暂存旧配置；任何提前返回或异常都会自动回滚，成功时显式提交。
    class PrepareTransaction final {
    public:
        explicit PrepareTransaction(SimulatorDevice &device);
        ~PrepareTransaction();
        PrepareTransaction(const PrepareTransaction &) = delete;
        PrepareTransaction &operator=(const PrepareTransaction &) = delete;
        void commit();

    private:
        SimulatorDevice &device_;
        bool wasPrepared_{false};
        bool committed_{false};
        std::vector<Point> points_;
        std::vector<std::vector<VnaWaveKeyV1>> layoutWaves_;
        std::vector<std::uint16_t> layoutExcitations_;
        std::vector<VnaWaveLayoutViewV1> layoutViews_;
    };

    bool canPrepare() const;
    bool beginSweep(VnaSweepIdV1 sweepId);
    VnaStatusV1 finishExecution(VnaStatusV1 status);
    VnaStatusV1 copyPlan(const VnaSweepPlanViewV1 &plan);
    VnaStatusV1 copyPoint(const VnaPointPlanViewV1 &source, Point &destination);
    VnaStatusV1 copyExcitation(const VnaExcitationPlanViewV1 &source, Excitation &destination);
    VnaStatusV1 rebuildLayouts(VnaPrepareResultViewV1 &result);
    VnaStatusV1 executeLayout(
        VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::chrono::steady_clock::time_point &measurementDeadline);
    VnaStatusV1 emitBatch(
        VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount,
        bool finalBatch);
    VnaStatusV1 fillBatch(
        VnaSweepIdV1 sweepId, std::size_t layoutIndex, std::uint32_t firstPoint, std::uint32_t pointCount,
        VnaMutableWaveBlockViewV1 &view) const;
    const Excitation *findExcitation(const Point &point, std::uint32_t layoutId) const;
    bool cancellationRequested(VnaSweepIdV1 sweepId) const;
    bool waitUntilMeasurementReady(VnaSweepIdV1 sweepId, std::chrono::steady_clock::time_point deadline);
    bool closeAbortAdmissionForFinalCommit(VnaSweepIdV1 sweepId);

    // hostApi_ 按值保存函数表；函数指针由宿主保证在驱动模块生命周期内有效。
    VnaDriverHostApiV1 hostApi_{};
    bool connected_{false};
    bool prepared_{false};
    // points_ 拥有执行计划，后三个布局容器按相同索引维护波数组、激励 ID 和公开视图。
    std::vector<Point> points_;
    std::vector<std::vector<VnaWaveKeyV1>> layoutWaves_;
    std::vector<std::uint16_t> layoutExcitations_;
    std::vector<VnaWaveLayoutViewV1> layoutViews_;
    // execute 与 requestAbort 位于不同线程；sweepMutex_ 使“接受中止”与“执行收尾”互斥。
    // 原子量供执行热路径和取消回调无锁读取，互斥量则保证终态判定不丢请求。
    std::mutex sweepMutex_;
    std::condition_variable sweepChanged_;
    bool abortAdmissionOpen_{false};
    std::atomic<bool> abortRequested_{false};
    std::atomic<VnaSweepIdV1> activeSweep_{0};
};

VnaStatusV1 simulatorStatus(std::uint32_t code, const char *message);
VnaComplex64V1 simulateWave(VnaSweepIdV1 sweepId, const Point &point, const Excitation &excitation, const Wave &wave);

}  // namespace vna::simulator

#endif
