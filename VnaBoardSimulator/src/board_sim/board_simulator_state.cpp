/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "board_simulator_state.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <string>

#include "board_signal_model.h"
#include "vna_board/board_api_contract.h"

namespace VnaBoard {
namespace {

enum ProductionTiming : std::size_t {
    MAX_PRODUCTION_BATCHES = 32U,
    PRODUCTION_DELAY_MS = 1U
};

bool ReadSettings(SignalSettings &settings)
{
    const char *mode = std::getenv("VNAPRO_SIM_MODE");
    if (mode != nullptr) {
        const std::string value(mode);
        if (value == "noise") {
            settings.mode = SignalMode::NOISE;
        } else if (!value.empty() && value != "golden") {
            return false;
        }
    }
    if (settings.mode != SignalMode::NOISE) {
        return true;
    }
    const char *seed = std::getenv("VNAPRO_SIM_SEED");
    if (seed == nullptr || *seed == '\0') {
        return true;
    }
    std::uint64_t parsed = 0U;
    for (const char *digit = seed; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        const auto value = static_cast<std::uint64_t>(*digit - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - value) / 10U) {
            return false;
        }
        parsed = parsed * 10U + value;
    }
    settings.seed = parsed;
    return true;
}

std::uint32_t ValidatePlan(SweepPlan &plan)
{
    const std::size_t points = plan.frequencies.size();
    if (points == 0U || plan.powers.size() != points || plan.bandwidths.size() != points || plan.workPorts.empty() ||
        plan.workPorts.size() > BOARD_PORT_COUNT) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (std::find(plan.frequencies.begin(), plan.frequencies.end(), 0U) != plan.frequencies.end() ||
        std::find(plan.bandwidths.begin(), plan.bandwidths.end(), 0U) != plan.bandwidths.end()) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::uint32_t seen = 0U;
    for (const auto port : plan.workPorts) {
        if (port >= BOARD_PORT_COUNT || (seen & (1U << port)) != 0U) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
        seen |= 1U << port;
    }
    plan.valuesPerPoint = plan.workPorts.size() * IQ_VALUES_PER_SOURCE_POINT;
    const std::size_t maxValues =
        std::min(MAX_SWEEP_BYTES / sizeof(double), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    if (points > maxValues / plan.valuesPerPoint) {
        return DRV_SIM_SIZE_OVERFLOW;
    }
    plan.totalValues = points * plan.valuesPerPoint;
    return DRV_SIM_OK;
}

void CopyRange(const SweepResult &result, std::size_t start, std::size_t count, double *output)
{
    // 后台按频点发布完整结果，原生读取则按工作端口分块返回本次请求的频点。
    for (std::size_t source = 0U; source < result.plan->workPorts.size(); ++source) {
        for (std::size_t point = 0U; point < count; ++point) {
            const std::size_t from =
                (start + point) * result.plan->valuesPerPoint + source * IQ_VALUES_PER_SOURCE_POINT;
            const std::size_t to = (source * count + point) * IQ_VALUES_PER_SOURCE_POINT;
            std::copy_n(result.values.data() + from, IQ_VALUES_PER_SOURCE_POINT, output + to);
        }
    }
}

void GenerateBatch(SweepResult &result, const BoardSignalModel &model, std::size_t start, std::size_t end)
{
    const auto &plan = *result.plan;
    for (std::size_t point = start; point < end; ++point) {
        for (std::size_t source = 0U; source < plan.workPorts.size(); ++source) {
            const std::size_t offset = point * plan.valuesPerPoint + source * IQ_VALUES_PER_SOURCE_POINT;
            model.GenerateSourcePoint(plan, point, plan.workPorts[source], result.values.data() + offset);
        }
    }
}

}  // 匿名命名空间

BoardSimulatorState::BoardSimulatorState(BatchObserver beforePublish, WaitObserver beforeWait)
    : beforePublish_(std::move(beforePublish)), beforeWait_(std::move(beforeWait))
{
}

BoardSimulatorState::~BoardSimulatorState()
{
    // 普通对象析构可兜底回收；DLL 卸载前必须通过 RF_OFF 在 loader lock 外完成回收。
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    StopWorker();
}

void BoardSimulatorState::StopWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result_) {
            result_->cancelled = true;
        }
        changed_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    scanning_ = false;
}

std::uint32_t BoardSimulatorState::Initialize()
{
    SignalSettings next;
    if (!ReadSettings(next)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    StopWorker();
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = next;
    staged_ = {};
    committed_.reset();
    result_.reset();
    sweepNumber_ = 0U;
    rfEnabled_ = false;
    initialized_ = true;
    return DRV_SIM_OK;
}

void BoardSimulatorState::SetList(ListKind kind, const std::uint64_t *values, std::uint32_t length) noexcept
{
    std::vector<std::uint64_t> copy;
    bool valid = values != nullptr && length > 0U && length <= MAX_POINT_COUNT;
    try {
        if (valid) {
            copy.assign(values, values + length);
        }
    } catch (...) {
        valid = false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    valid = valid && initialized_;
    switch (kind) {
        case ListKind::FREQUENCIES:
            staged_.plan.frequencies = std::move(copy);
            staged_.frequenciesValid = valid;
            break;
        case ListKind::POWERS:
            staged_.plan.powers = std::move(copy);
            staged_.powersValid = valid;
            break;
        case ListKind::BANDWIDTHS:
            staged_.plan.bandwidths = std::move(copy);
            staged_.bandwidthsValid = valid;
            break;
        default:
            break;
    }
}

void BoardSimulatorState::SetFrequencies(const std::uint64_t *values, std::uint32_t length) noexcept
{
    SetList(ListKind::FREQUENCIES, values, length);
}

void BoardSimulatorState::SetPowers(const std::uint64_t *values, std::uint32_t length) noexcept
{
    SetList(ListKind::POWERS, values, length);
}

void BoardSimulatorState::SetBandwidths(const std::uint64_t *values, std::uint32_t length) noexcept
{
    SetList(ListKind::BANDWIDTHS, values, length);
}

void BoardSimulatorState::SetWorkPorts(const std::uint32_t *values, std::uint32_t length) noexcept
{
    std::vector<std::uint32_t> copy;
    bool valid = values != nullptr && length > 0U && length <= BOARD_PORT_COUNT;
    try {
        if (valid) {
            copy.assign(values, values + length);
        }
    } catch (...) {
        valid = false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    staged_.plan.workPorts = std::move(copy);
    staged_.portsValid = valid && initialized_;
}

std::uint32_t BoardSimulatorState::Download()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return DRV_SIM_NOT_INITIALIZED;
    }
    if (scanning_) {
        return DRV_SIM_BUSY;
    }
    if (!staged_.frequenciesValid || !staged_.powersValid || !staged_.bandwidthsValid || !staged_.portsValid) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    auto next = std::make_shared<SweepPlan>(staged_.plan);
    const auto status = ValidatePlan(*next);
    if (status != DRV_SIM_OK) {
        return status;
    }
    committed_ = std::move(next);
    return DRV_SIM_OK;
}

std::uint32_t BoardSimulatorState::SetRfPower(std::uint32_t enabled)
{
    if (enabled > 1U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return DRV_SIM_NOT_INITIALIZED;
        }
        rfEnabled_ = enabled == 1U;
    }
    if (enabled == 0U) {
        StopWorker();
    }
    return DRV_SIM_OK;
}

std::uint32_t BoardSimulatorState::Start()
{
    // 并发启动直接拒绝，避免把供应头要求的串行调用偷偷排队成多次扫描。
    std::unique_lock<std::mutex> lifecycle(lifecycleMutex_, std::try_to_lock);
    if (!lifecycle.owns_lock()) {
        return DRV_SIM_BUSY;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scanning_) {
            return DRV_SIM_BUSY;
        }
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return LaunchSweep();
}

std::uint32_t BoardSimulatorState::LaunchSweep()
{
    if (!initialized_) {
        return DRV_SIM_NOT_INITIALIZED;
    }
    if (!rfEnabled_) {
        return DRV_SIM_RF_DISABLED;
    }
    if (!committed_) {
        return DRV_SIM_INVALID_STATE;
    }
    if (sweepNumber_ == std::numeric_limits<std::uint64_t>::max()) {
        return DRV_SIM_SIZE_OVERFLOW;
    }
    auto next = std::make_shared<SweepResult>();
    next->plan = committed_;
    next->values.resize(committed_->totalValues);
    result_ = next;
    scanning_ = true;
    try {
        worker_ = std::thread(&BoardSimulatorState::Produce, this, next, settings_, sweepNumber_ + 1U);
    } catch (...) {
        scanning_ = false;
        next->error = DRV_SIM_INTERNAL_ERROR;
        changed_.notify_all();
        throw;
    }
    ++sweepNumber_;
    return DRV_SIM_OK;
}

void BoardSimulatorState::Produce(
    std::shared_ptr<SweepResult> result, SignalSettings settings, std::uint64_t sweepNumber) noexcept
{
    try {
        const auto &plan = *result->plan;
        const std::size_t points = plan.frequencies.size();
        const std::size_t batchSize = (points + MAX_PRODUCTION_BATCHES - 1U) / MAX_PRODUCTION_BATCHES;
        BoardSignalModel model(settings, sweepNumber);
        for (std::size_t start = 0U; start < points; start += batchSize) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 最多 32 次短等待；采样窗不随频点数线性增长，取消可以立即打断等待。
                if (changed_.wait_for(lock, std::chrono::milliseconds(PRODUCTION_DELAY_MS), [&result] {
                        return result->cancelled;
                    })) {
                    break;
                }
            }
            const std::size_t end = std::min(points, start + batchSize);
            GenerateBatch(*result, model, start, end);
            if (beforePublish_) {
                beforePublish_(end);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (result->cancelled) {
                break;
            }
            // 仅整频点的全部工作端口计算完成后发布，读者不会看到半个频点。
            result->availablePoints = end;
            if (end == points) {
                scanning_ = false;
            }
            changed_.notify_all();
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        result->error = DRV_SIM_INTERNAL_ERROR;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    scanning_ = false;
    changed_.notify_all();
}

std::uint32_t BoardSimulatorState::Get(std::uint32_t start, std::uint32_t count, double *output, int capacity)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!initialized_) {
        return DRV_SIM_NOT_INITIALIZED;
    }
    const auto selected = result_;
    if (!selected) {
        return DRV_SIM_INVALID_STATE;
    }
    const std::size_t points = selected->plan->frequencies.size();
    if (count == 0U || start >= points || count > points - start || output == nullptr || capacity < 0) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    const std::size_t required = static_cast<std::size_t>(count) * selected->plan->valuesPerPoint;
    if (static_cast<std::size_t>(capacity) < required) {
        return DRV_SIM_BUFFER_TOO_SMALL;
    }
    const std::size_t end = static_cast<std::size_t>(start) + count;
    // 内部测试观察器只能记录已绑定结果的等待，不得重入模拟器。
    if (beforeWait_ && selected->availablePoints < end && !selected->cancelled) {
        beforeWait_();
    }
    changed_.wait(lock, [&selected, end] {
        return selected->cancelled || selected->error != DRV_SIM_OK || selected->availablePoints >= end;
    });
    if (selected->cancelled) {
        return DRV_SIM_CANCELLED;
    }
    if (selected->error != DRV_SIM_OK) {
        return selected->error;
    }
    CopyRange(*selected, start, count, output);
    return DRV_SIM_OK;
}

}  // 命名空间 VnaBoard
