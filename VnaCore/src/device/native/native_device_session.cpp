#include "device/native/native_device_session.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "device/plan_preparation_error.h"

namespace vna_core {
namespace detail {
namespace {

void check(std::uint32_t status, const char *step)
{
    if (status != 0U) {
        // Preserve the number without interpreting vendor errors as simulator errors.
        throw std::runtime_error(std::string(step) + " returned " + std::to_string(status));
    }
}

struct DownloadFrame {
    std::vector<std::uint32_t> parameters;
    std::vector<double> table;
};

DownloadFrame encode(const SweepPlan &plan, std::uint32_t sourcePort)
{
    DownloadFrame frame;
    frame.parameters.resize(1U + 6U * plan.frequencyRaw.size(), 0U);
    frame.table = plan.tableData;
    for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
        const std::size_t base = 1U + 6U * point;
        frame.parameters[base] = static_cast<std::uint32_t>(plan.frequencyRaw[point] >> 32U);
        frame.parameters[base + 1U] = static_cast<std::uint32_t>(plan.frequencyRaw[point]);
        frame.parameters[base + 2U] = sourcePort - 1U;
        frame.parameters[base + 4U] = plan.ifBandwidthRaw;
    }
    return frame;
}

}  // namespace

NativeDeviceSession::NativeDeviceSession(std::string path) : path_(std::move(path))
{
}

std::string NativeDeviceSession::initialize()
{
    library_.open(path_);
    const auto &functions = library_.functions();
    check(functions.hwInit(), "DrvHwInit");
    initialized_ = true;
    std::array<char, 4096U> idn{};
    check(functions.getIDN(idn.data(), static_cast<int>(idn.size())), "DrvGetIDN");
    idn.back() = '\0';
    return std::string(idn.data());
}

void NativeDeviceSession::applyPlan(const SweepPlan &plan)
{
    // Prepare every owned ABI buffer before the first device mutation.
    std::vector<DownloadFrame> downloads;
    try {
        downloads.reserve(plan.sourcePorts.size());
        for (const auto source : plan.sourcePorts) {
            downloads.push_back(encode(plan, source));
        }
    } catch (const std::exception &) {
        throw PlanPreparationError();
    }
    const auto &functions = library_.functions();
    check(functions.activateRxLink(plan.receiverMask), "DrvActivateRxLink");
    check(functions.setFastSweepMode(0U), "DrvSetFastSweepMode");
    const auto count = static_cast<std::uint32_t>(plan.frequencyRaw.size());
    for (auto &frame : downloads) {
        check(functions.dataFrameDownload(count, frame.parameters.data(),
                  static_cast<std::uint32_t>(frame.parameters.size()), frame.table.data(),
                  static_cast<std::uint32_t>(frame.table.size())),
            "DrvDataFrameDownload");
    }
}

void NativeDeviceSession::enableRf()
{
    check(library_.functions().setRfPowerOn(1U), "DrvSetRfPowerOn(1)");
}

std::vector<double> NativeDeviceSession::acquirePort(const SweepPlan &plan, std::uint32_t sourcePort)
{
    const auto count = static_cast<std::uint32_t>(plan.frequencyRaw.size());
    std::vector<double> iq(16U * plan.frequencyRaw.size());
    const auto &functions = library_.functions();
    check(functions.startSweep(count, sourcePort - 1U), "DrvStartSweep");
    check(functions.getSweepData(count, iq.data()), "DrvGetSweepData");
    return iq;
}

void NativeDeviceSession::shutdown()
{
    std::uint32_t status = 0U;
    if (initialized_) {
        status = library_.functions().setRfPowerOn(0U);
    }
    initialized_ = false;
    library_.close();
    check(status, "DrvSetRfPowerOn(0)");
}

}  // namespace detail
}  // namespace vna_core
