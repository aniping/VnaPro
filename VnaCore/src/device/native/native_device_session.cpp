/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/native/native_device_session.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "device/device_session_factory.h"
#include "device/native/download_frame.h"
#include "device/plan_preparation_error.h"
#include "vna_core/frame_layout.h"
#include "vna_native/native_api_contract.h"

namespace VnaCore {
namespace Detail {
namespace {

constexpr std::size_t PARAM_HEADER_VALUES = 1U;
constexpr unsigned FREQUENCY_WORD_BITS = 32U;
constexpr std::size_t IDENTITY_BUFFER_SIZE = 4096U;
constexpr std::uint32_t NATIVE_SUCCESS = 0U;
constexpr std::uint32_t NORMAL_SWEEP_MODE = 0U;
constexpr std::uint32_t FIRST_PHYSICAL_PORT = 1U;

// 这些字段对应已固定原生 ABI 中的六字普通扫描记录。
enum ParameterOffset : std::size_t {
    FREQUENCY_HIGH_OFFSET = 0U,
    FREQUENCY_LOW_OFFSET = 1U,
    SOURCE_PORT_OFFSET = 2U,
    IF_BANDWIDTH_OFFSET = 4U
};

enum RfPowerState : std::uint32_t {
    RF_OFF = 0U,
    RF_ON = 1U
};

void Check(std::uint32_t status, const char *step)
{
    if (status != NATIVE_SUCCESS) {
        // 保留错误编号，不将厂商错误解释为模拟器错误。
        throw std::runtime_error(std::string(step) + " returned " + std::to_string(status));
    }
}

DownloadFrame Encode(const SweepPlan &plan, std::uint32_t sourcePort)
{
    DownloadFrame frame;
    // 将帧头及预留动态链路字段置零，以选择普通扫描格式。
    frame.parameters.resize(PARAM_HEADER_VALUES + VnaNative::PARAM_VALUES_PER_POINT * plan.frequencyRaw.size(), 0U);
    frame.table = plan.tableData;
    for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
        const std::size_t base = PARAM_HEADER_VALUES + VnaNative::PARAM_VALUES_PER_POINT * point;
        frame.parameters[base + FREQUENCY_HIGH_OFFSET] =
            static_cast<std::uint32_t>(plan.frequencyRaw[point] >> FREQUENCY_WORD_BITS);
        frame.parameters[base + FREQUENCY_LOW_OFFSET] = static_cast<std::uint32_t>(plan.frequencyRaw[point]);
        frame.parameters[base + SOURCE_PORT_OFFSET] = sourcePort - FIRST_PHYSICAL_PORT;
        frame.parameters[base + IF_BANDWIDTH_OFFSET] = plan.ifBandwidthRaw;
    }
    return frame;
}

}  // 匿名命名空间

NativeDeviceSession::NativeDeviceSession(std::string path) : path_(std::move(path))
{
}

std::string NativeDeviceSession::Initialize()
{
    library_.Open(path_);
    const auto &functions = library_.Functions();
    Check(functions.hwInit(), "DrvHwInit");
    initialized_ = true;
    std::array<char, IDENTITY_BUFFER_SIZE> idn{};
    Check(functions.getIdn(idn.data(), static_cast<int>(idn.size())), "DrvGetIDN");
    idn.back() = '\0';
    return std::string(idn.data());
}

void NativeDeviceSession::ApplyPlan(const SweepPlan &plan)
{
    if (plan.tableData.size() != plan.frequencyRaw.size() * TABLE_VALUES_PER_POINT) {
        throw PlanPreparationError("VN08 opaque table must contain exactly 9N values");
    }
    // 在首次变更设备状态前，准备好全部独立持有的 ABI 缓冲区。
    std::vector<DownloadFrame> downloads;
    try {
        downloads.reserve(plan.sourcePorts.size());
        for (const auto source : plan.sourcePorts) {
            downloads.push_back(Encode(plan, source));
        }
    } catch (const std::exception &) {
        throw PlanPreparationError();
    }
    const auto &functions = library_.Functions();
    Check(functions.activateRxLink(plan.receiverMask), "DrvActivateRxLink");
    Check(functions.setFastSweepMode(NORMAL_SWEEP_MODE), "DrvSetFastSweepMode");
    const auto count = static_cast<std::uint32_t>(plan.frequencyRaw.size());
    for (auto &frame : downloads) {
        Check(functions.dataFrameDownload(count, frame.parameters.data(),
                  static_cast<std::uint32_t>(frame.parameters.size()), frame.table.data(),
                  static_cast<std::uint32_t>(frame.table.size())),
            "DrvDataFrameDownload");
    }
}

void NativeDeviceSession::EnableRf()
{
    Check(library_.Functions().setRfPowerOn(RF_ON), "DrvSetRfPowerOn(1)");
}

std::vector<PortFrame> NativeDeviceSession::AcquireSweep(const SweepPlan &plan)
{
    const auto count = static_cast<std::uint32_t>(plan.frequencyRaw.size());
    std::vector<PortFrame> ports;
    ports.reserve(plan.sourcePorts.size());
    for (const auto source : plan.sourcePorts) {
        PortFrame port;
        port.sourcePort = source;
        port.iq.resize(IQ_VALUES_PER_POINT * plan.frequencyRaw.size());
        ports.push_back(std::move(port));
    }
    const auto &functions = library_.Functions();
    for (auto &port : ports) {
        Check(functions.startSweep(count, port.sourcePort - FIRST_PHYSICAL_PORT), "DrvStartSweep");
        Check(functions.getSweepData(count, port.iq.data()), "DrvGetSweepData");
    }
    return ports;
}

void NativeDeviceSession::Shutdown()
{
    std::uint32_t status = NATIVE_SUCCESS;
    if (initialized_) {
        status = library_.Functions().setRfPowerOn(RF_OFF);
    }
    initialized_ = false;
    library_.Close();
    Check(status, "DrvSetRfPowerOn(0)");
}

std::unique_ptr<DeviceSession> CreateNativeDevice(const std::string &path)
{
    return std::make_unique<NativeDeviceSession>(path);
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
