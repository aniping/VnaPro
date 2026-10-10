/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/native/native_device_session.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "device/native/download_frame.h"
#include "device/plan_preparation_error.h"
#include "vna_core/frame_layout.h"
#include "vna_native/native_api_contract.h"

namespace vna_core {
namespace detail {
namespace {

constexpr std::size_t PARAM_HEADER_VALUES = 1U;
constexpr unsigned FREQUENCY_WORD_BITS = 32U;
constexpr std::size_t IDENTITY_BUFFER_SIZE = 4096U;
constexpr std::uint32_t NATIVE_SUCCESS = 0U;
constexpr std::uint32_t NORMAL_SWEEP_MODE = 0U;
constexpr std::uint32_t FIRST_PHYSICAL_PORT = 1U;

// These fields occupy the six-word normal-sweep record in the frozen native ABI.
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
        // Preserve the number without interpreting vendor errors as simulator errors.
        throw std::runtime_error(std::string(step) + " returned " + std::to_string(status));
    }
}

DownloadFrame Encode(const SweepPlan &plan, std::uint32_t sourcePort)
{
    DownloadFrame frame;
    // Zeroed header and reserved dynamic-link fields select the normal-sweep format.
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

}  // namespace

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
    // Prepare every owned ABI buffer before the first device mutation.
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

std::vector<double> NativeDeviceSession::AcquirePort(const SweepPlan &plan, std::uint32_t sourcePort)
{
    const auto count = static_cast<std::uint32_t>(plan.frequencyRaw.size());
    std::vector<double> iq(IQ_VALUES_PER_POINT * plan.frequencyRaw.size());
    const auto &functions = library_.Functions();
    Check(functions.startSweep(count, sourcePort - FIRST_PHYSICAL_PORT), "DrvStartSweep");
    Check(functions.getSweepData(count, iq.data()), "DrvGetSweepData");
    return iq;
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

}  // namespace detail
}  // namespace vna_core
