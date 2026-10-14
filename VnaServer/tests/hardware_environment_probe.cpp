/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cstdlib>
#include <cstring>

#include "vna_native/native_api_contract.h"

namespace {

// 只用于验证服务启动的 ABI 探针，不模拟真实板卡或生成测量数据。
constexpr std::uint32_t SUCCESS = 0U;
constexpr std::uint32_t ENVIRONMENT_CHANGED = 91U;
constexpr std::uint32_t INITIALIZATION_FAILURE = 17U;
constexpr std::uint32_t UNSUPPORTED_OPERATION = 18U;
const char g_identity[] = "Huawei,HardwareEnvironmentProbe,4PORT,TEST";
bool g_initialized = false;

bool EnvironmentEquals(const char *name, const char *expected)
{
    const char *value = std::getenv(name);
    return value != nullptr && std::strcmp(value, expected) == 0;
}

}  // 匿名命名空间

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit()
{
    // 三个值刻意不符合模拟参数语法，硬件路径必须原样保留进程启动环境。
    if (!EnvironmentEquals("VNAPRO_SIM_MODE", "hardware-probe-mode-unchanged") ||
        !EnvironmentEquals("VNAPRO_SIM_SEED", "hardware-probe-seed-unchanged") ||
        !EnvironmentEquals("VNAPRO_SIM_NOISE_AMPLITUDE", "hardware-probe-amplitude-unchanged")) {
        return ENVIRONMENT_CHANGED;
    }
    if (EnvironmentEquals("VNAPRO_TEST_HW_INIT_FAILURE", "1")) {
        return INITIALIZATION_FAILURE;
    }
    g_initialized = true;
    return SUCCESS;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char *buffer, int capacity)
{
    if (!g_initialized || buffer == nullptr || capacity < static_cast<int>(sizeof(g_identity))) {
        return UNSUPPORTED_OPERATION;
    }
    std::memcpy(buffer, g_identity, sizeof(g_identity));
    return SUCCESS;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t)
{
    return g_initialized ? SUCCESS : UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t)
{
    return g_initialized ? SUCCESS : UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(
    std::uint32_t, std::uint32_t *, std::uint32_t, double *, std::uint32_t)
{
    // 允许服务建立 HOLD 状态的初始扫描计划；不解释厂商私有下载内容。
    return g_initialized ? SUCCESS : UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled)
{
    return g_initialized && enabled == 0U ? SUCCESS : UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(std::uint32_t, std::uint32_t)
{
    // 此探针只验启动隔离，意外触发采集应立即暴露测试配置错误。
    return UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t, double *)
{
    return UNSUPPORTED_OPERATION;
}

extern "C" VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t, std::uint32_t *, std::uint16_t *, std::uint32_t)
{
    return UNSUPPORTED_OPERATION;
}
