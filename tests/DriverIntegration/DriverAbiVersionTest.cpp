#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <dlfcn.h>

#include "DriverPluginLoader.h"
#include "HostServices.h"

namespace {

class SimulatorAbiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        library_ = dlopen(VNA_TEST_DRIVER_MODULE, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(library_, nullptr) << dlerror();
        getApi_ = reinterpret_cast<VnaDriverGetApiFunctionV1>(dlsym(library_, "VnaDriverGetApi"));
        ASSERT_NE(getApi_, nullptr);
        api_.structSize = static_cast<std::uint32_t>(sizeof(api_));
        ASSERT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1, &vna::detail::HostServices::api(), &api_).code, VNA_STATUS_OK);
    }

    void TearDown() override
    {
        if (library_ != nullptr) {
            dlclose(library_);
        }
    }

    void *library_{nullptr};
    VnaDriverGetApiFunctionV1 getApi_{nullptr};
    VnaDriverApiV1 api_{};
};

class DriverHandles final {
public:
    explicit DriverHandles(const VnaDriverApiV1 &api) : api_(&api)
    {
    }

    ~DriverHandles()
    {
        if (device != nullptr) {
            api_->Disconnect(device);
            api_->DestroyDevice(device);
        }
        if (driver != nullptr) {
            api_->Destroy(driver);
        }
    }

    VnaDriverHandle driver{nullptr};
    VnaDeviceHandle device{nullptr};

private:
    const VnaDriverApiV1 *api_{nullptr};
};

TEST_F(SimulatorAbiTest, HostRejectsReturnedTableVersionAndLayoutMismatches)
{
    EXPECT_TRUE(vna::detail::driverApiIsValidForHost(api_));
    auto invalid = api_;
    ++invalid.abiMajor;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
    invalid = api_;
    ++invalid.abiMinor;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
    invalid = api_;
    --invalid.structSize;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
    invalid = api_;
    invalid.reserved = 1;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
    invalid = api_;
    --invalid.metadata.structSize;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
    invalid = api_;
    invalid.metadata.reserved = 1;
    EXPECT_FALSE(vna::detail::driverApiIsValidForHost(invalid));
}

TEST_F(SimulatorAbiTest, DriverEntryRejectsHostVersionSizeAndReservedMismatches)
{
    auto invalidHost = vna::detail::HostServices::api();
    VnaDriverApiV1 output{};
    output.structSize = static_cast<std::uint32_t>(sizeof(output));
    EXPECT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1 + 1, &invalidHost, &output).code, VNA_STATUS_INVALID_ARGUMENT);

    --invalidHost.structSize;
    EXPECT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1, &invalidHost, &output).code, VNA_STATUS_INVALID_ARGUMENT);
    invalidHost = vna::detail::HostServices::api();
    ++invalidHost.abiMajor;
    EXPECT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1, &invalidHost, &output).code, VNA_STATUS_INVALID_ARGUMENT);
    invalidHost = vna::detail::HostServices::api();
    ++invalidHost.abiMinor;
    EXPECT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1, &invalidHost, &output).code, VNA_STATUS_INVALID_ARGUMENT);
    invalidHost = vna::detail::HostServices::api();
    invalidHost.reserved = 1;
    EXPECT_EQ(getApi_(VNA_DRIVER_ABI_VERSION_V1, &invalidHost, &output).code, VNA_STATUS_INVALID_ARGUMENT);

    output = {};
    output.structSize = static_cast<std::uint32_t>(sizeof(output) - 1);
    EXPECT_EQ(
        getApi_(VNA_DRIVER_ABI_VERSION_V1, &vna::detail::HostServices::api(), &output).code,
        VNA_STATUS_INVALID_ARGUMENT);
    output = {};
    output.structSize = static_cast<std::uint32_t>(sizeof(output));
    output.reserved = 1;
    EXPECT_EQ(
        getApi_(VNA_DRIVER_ABI_VERSION_V1, &vna::detail::HostServices::api(), &output).code,
        VNA_STATUS_INVALID_ARGUMENT);
}

TEST_F(SimulatorAbiTest, DirectPrepareRejectsUnsupportedAndInvalidPlanValues)
{
    DriverHandles handles(api_);
    VnaDriverCreateInfoV1 createInfo{};
    createInfo.structSize = static_cast<std::uint32_t>(sizeof(createInfo));
    ASSERT_EQ(api_.Create(&createInfo, &handles.driver).code, VNA_STATUS_OK);
    const char identifier[] = "simulator-0";
    const VnaStringViewV1 deviceId{identifier, std::strlen(identifier)};
    ASSERT_EQ(api_.CreateDevice(handles.driver, deviceId, &handles.device).code, VNA_STATUS_OK);
    ASSERT_EQ(api_.Connect(handles.device).code, VNA_STATUS_OK);

    const VnaSourcePlanV1 source{1, 0, 0.0, 0.0};
    VnaWaveKeyV1 wave{VNA_WAVE_INCIDENT, 1, 0};
    const VnaExcitationPlanViewV1 excitation{1, 0, 1, &source, 1, &wave};
    VnaPointPlanViewV1 points[2]{};
    for (std::size_t index = 0; index < 2; ++index) {
        points[index] = VnaPointPlanViewV1{static_cast<std::uint32_t>(index + 1), 0, 1.0e9,      1000.0,
                                           static_cast<std::int64_t>(index),      1, &excitation};
    }
    VnaSweepPlanViewV1 plan{};
    plan.structSize = static_cast<std::uint32_t>(sizeof(plan));
    plan.triggerMode = VNA_TRIGGER_INTERNAL;
    plan.pointCount = 2;
    plan.points = points;
    VnaPrepareResultViewV1 result{};
    result.structSize = static_cast<std::uint32_t>(sizeof(result));
    EXPECT_EQ(api_.Prepare(handles.device, &plan, &result).code, VNA_STATUS_INVALID_ARGUMENT);

    // 直接 ABI 调用不经宿主计划校验，驱动仍必须独立拒绝不支持的外触发。
    points[1].dwellNanoseconds = points[0].dwellNanoseconds;
    plan.triggerMode = VNA_TRIGGER_EXTERNAL;
    EXPECT_EQ(api_.Prepare(handles.device, &plan, &result).code, VNA_STATUS_INVALID_ARGUMENT);

    // 整数 ABI 字段可携带任意值，未知触发和波类型都不能被默认映射。
    plan.triggerMode = UINT32_C(0xffffffff);
    EXPECT_EQ(api_.Prepare(handles.device, &plan, &result).code, VNA_STATUS_INVALID_ARGUMENT);
    plan.triggerMode = VNA_TRIGGER_INTERNAL;
    wave.kind = UINT32_C(0xffffffff);
    EXPECT_EQ(api_.Prepare(handles.device, &plan, &result).code, VNA_STATUS_INVALID_ARGUMENT);
}

}  // namespace
