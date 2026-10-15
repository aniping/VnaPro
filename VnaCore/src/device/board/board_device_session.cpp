/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/board/board_device_session.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "device/device_session_factory.h"
#include "device/plan_preparation_error.h"
#include "vna_core/frame_layout.h"
#include "vna_log/logger.h"

namespace VnaCore {
namespace Detail {
namespace {

constexpr std::uint32_t NATIVE_SUCCESS = 0U;
constexpr std::uint32_t RF_OFF = 0U;
constexpr std::uint32_t RF_ON = 1U;
constexpr std::size_t READ_POINTS_PER_SEGMENT = 64U;

void Check(std::uint32_t status, const char *step)
{
    if (status != NATIVE_SUCCESS) {
        // 只保留数值状态，不将模拟器错误名推广为真实硬件的错误契约。
        auto message = std::string(step) + " returned " + std::to_string(status);
        if (status > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
            const auto signedStatus = static_cast<std::int64_t>(status) - (std::int64_t{1} << 32U);
            message += " (signed 32-bit: " + std::to_string(signedStatus) + ")";
        }
        throw std::runtime_error(message);
    }
}

void ValidatePowers(const std::vector<double> &powers)
{
    for (const auto power : powers) {
        if (!std::isfinite(power) || power < MIN_POWER_DBM || power > MAX_POWER_DBM) {
            throw PlanPreparationError("VN10 power must be finite dBm within -120..100");
        }
    }
}

}  // 匿名命名空间

std::unique_ptr<BoardDeviceSession::DownloadLists> BoardDeviceSession::PrepareDownload(const SweepPlan &plan)
{
    ValidatePowers(plan.powerDbm);
    std::unique_ptr<DownloadLists> download;
    try {
        download = std::make_unique<DownloadLists>();
        download->frequencies = plan.frequencyRaw;
        download->powers = plan.powerDbm.empty() ? std::vector<double>(plan.frequencyRaw.size(), 0.0) : plan.powerDbm;
        download->bandwidths.assign(plan.frequencyRaw.size(), plan.ifBandwidthRaw);
        download->ports.reserve(plan.sourcePorts.size());
        for (const auto source : plan.sourcePorts) {
            download->ports.push_back(source - 1U);
        }
    } catch (const std::exception &) {
        throw PlanPreparationError();
    }
    return download;
}

namespace {

std::vector<PortFrame> PreparePorts(const SweepPlan &plan)
{
    const std::size_t count = plan.frequencyRaw.size();
    if (count > (std::numeric_limits<std::size_t>::max)() / IQ_VALUES_PER_POINT) {
        throw std::overflow_error("VN10 IQ buffer size overflow");
    }
    std::vector<PortFrame> ports;
    ports.reserve(plan.sourcePorts.size());
    for (const auto source : plan.sourcePorts) {
        PortFrame port;
        port.sourcePort = source;
        port.iq.resize(count * IQ_VALUES_PER_POINT);
        ports.push_back(std::move(port));
    }
    return ports;
}

void CopySegment(
    const std::vector<double> &segment, std::size_t first, std::size_t count, std::vector<PortFrame> &ports)
{
    // 每个源端口独占本次请求段的连续频点块，不与其他源端口交错。
    for (std::size_t source = 0U; source < ports.size(); ++source) {
        const std::size_t input = source * count * IQ_VALUES_PER_POINT;
        const std::size_t output = first * IQ_VALUES_PER_POINT;
        std::copy_n(segment.data() + input, count * IQ_VALUES_PER_POINT, ports[source].iq.data() + output);
    }
}

}  // 匿名命名空间

BoardDeviceSession::BoardDeviceSession(std::string path) : path_(std::move(path))
{
}

BoardDeviceSession::~BoardDeviceSession()
{
    if (keepLibraryResident_) {
        // 供应接口没有释放入口；关端口不停止扫描，析构不能卸载驱动或释放其借用数组。
        library_.KeepLoadedUntilExit();
        static_cast<void>(activeLists_.release());
        static_cast<void>(stagedLists_.release());
        static_cast<void>(readBuffer_.release());
        VnaLog::Info(VnaLog::Module::CORE, "VN10 没有设备释放接口，动态库和驱动缓冲保留至进程退出");
    }
}

void BoardDeviceSession::OpenLibrary()
{
    LogBoardBinding(path_, "begin");
    try {
        library_.Open(path_);
    } catch (const std::exception &error) {
        LogBoardBinding(path_, "failed", error.what());
        throw;
    }
    LogBoardBinding(path_, "ready");
}

std::string BoardDeviceSession::Initialize()
{
    if (sweepInProgress_) {
        throw std::runtime_error("VN10 scan completion is unknown; reinitialization requires a process restart");
    }
    if (initializationAttempted_) {
        Shutdown();
    }
    OpenLibrary();
    initializationAttempted_ = true;
    keepLibraryResident_ = true;
    try {
        LogBoardCallBegin("initialize", 0U, "DrvHwInit");
        Check(LogBoardCallReturned("initialize", 0U, "DrvHwInit", library_.Functions().hwInit()), "DrvHwInit");
        LogBoardCallBegin("initialize", 0U, "DrvSetRfPowerOn(0)");
        Check(LogBoardCallReturned("initialize", 0U, "DrvSetRfPowerOn(0)", library_.Functions().setRfPowerOn(RF_OFF)),
            "DrvSetRfPowerOn(0)");
    } catch (...) {
        // 初始化也可能留下库内资源，必须先尝试射频关闭，不能直接卸载。
        try {
            Shutdown();
        } catch (const std::exception &error) {
            VnaLog::Error(VnaLog::Module::CORE, "VN10 初始化失败后的设备清理报告错误：{}", error.what());
        }
        throw;
    }
    // VN10 接口没有设备身份查询；回报适配器标识，不能冒充驱动提供的型号或模拟身份。
    return "VnaPro,TDC1VN10,4PORT,1.0";
}

void BoardDeviceSession::ApplyPlan(const SweepPlan &plan)
{
    auto download = PrepareDownload(plan);
    stagedLists_ = std::move(download);
    const auto config = ++configurationAttempt_;
    const auto &functions = library_.Functions();
    const auto count = static_cast<std::uint32_t>(stagedLists_->frequencies.size());
    LogBoardCallBegin("configure", config, "DrvSetFreqList");
    functions.setFreqList(stagedLists_->frequencies.data(), count);
    LogBoardSetterReturned(config, "DrvSetFreqList");
    LogBoardCallBegin("configure", config, "DrvSetPowerList");
    functions.setPowerList(stagedLists_->powers.data(), count);
    LogBoardSetterReturned(config, "DrvSetPowerList");
    LogBoardCallBegin("configure", config, "DrvSetIfBwList");
    functions.setIfBwList(stagedLists_->bandwidths.data(), count);
    LogBoardSetterReturned(config, "DrvSetIfBwList");
    LogBoardCallBegin("configure", config, "DrvSetWorkPorts");
    functions.setWorkPorts(stagedLists_->ports.data(), static_cast<std::uint32_t>(stagedLists_->ports.size()));
    LogBoardSetterReturned(config, "DrvSetWorkPorts");
    LogBoardConfiguration(config, *stagedLists_);
    LogBoardCallBegin("configure", config, "DrvDataFrameDownload");
    const auto status =
        LogBoardCallReturned("configure", config, "DrvDataFrameDownload", functions.dataFrameDownload());
    if (status != NATIVE_SUCCESS) {
        LogBoardConfigurationFailure(config, *stagedLists_);
    }
    Check(status, "DrvDataFrameDownload");
    activeLists_ = std::move(stagedLists_);
}

void BoardDeviceSession::EnableRf()
{
    LogBoardCallBegin("enable_rf", configurationAttempt_, "DrvSetRfPowerOn(1)");
    Check(LogBoardCallReturned(
              "enable_rf", configurationAttempt_, "DrvSetRfPowerOn(1)", library_.Functions().setRfPowerOn(RF_ON)),
        "DrvSetRfPowerOn(1)");
}

std::vector<PortFrame> BoardDeviceSession::AcquireSweep(const SweepPlan &plan)
{
    if (sweepInProgress_) {
        throw std::runtime_error("VN10 previous scan completion is unknown");
    }
    auto ports = PreparePorts(plan);
    const std::size_t segmentPoints = (std::min)(READ_POINTS_PER_SEGMENT, plan.frequencyRaw.size());
    const std::size_t stride = plan.sourcePorts.size() * IQ_VALUES_PER_POINT;
    if (stride == 0U || segmentPoints > static_cast<std::size_t>((std::numeric_limits<int>::max)()) / stride) {
        throw std::overflow_error("VN10 segment exceeds native dataLen capacity");
    }
    readBuffer_ = std::make_unique<std::vector<double>>(segmentPoints * stride);
    const auto &functions = library_.Functions();
    sweepInProgress_ = true;
    Check(functions.startSweep(), "DrvStartSweep");
    for (std::size_t first = 0U; first < plan.frequencyRaw.size(); first += segmentPoints) {
        const std::size_t count = (std::min)(segmentPoints, plan.frequencyRaw.size() - first);
        Check(functions.getSweepData(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count),
                  readBuffer_->data(), static_cast<int>(count * stride)),
            "DrvGetSweepData");
        CopySegment(*readBuffer_, first, count, ports);
    }
    sweepInProgress_ = false;
    return ports;
}

void BoardDeviceSession::Shutdown()
{
    std::uint32_t status = NATIVE_SUCCESS;
    if (initializationAttempted_) {
        LogBoardCallBegin("shutdown", configurationAttempt_, "DrvSetRfPowerOn(0)");
        status = LogBoardCallReturned(
            "shutdown", configurationAttempt_, "DrvSetRfPowerOn(0)", library_.Functions().setRfPowerOn(RF_OFF));
    }
    // 未关闭成功时不能证明驱动已经停止使用代码和数组，保留全部状态供恢复或析构兜底。
    Check(status, "DrvSetRfPowerOn(0)");
    initializationAttempted_ = false;
    // RF OFF 只关端口。保持同一加载引用和全部数组，故障恢复不重新加载 DLL。
    if (sweepInProgress_) {
        throw std::runtime_error("VN10 scan completion is unknown; ports are off but scan may still be running");
    }
}

std::unique_ptr<DeviceSession> CreateBoardDevice(const std::string &path)
{
    return std::make_unique<BoardDeviceSession>(path);
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
