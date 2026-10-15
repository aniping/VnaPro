/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "server_config_loader.h"

namespace {

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    Require(path.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()), "测试路径过长");
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    Require(length > 0, "测试路径不是合法 UTF-8");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) == length,
        "测试路径转换失败");
    return result;
}
#endif

void WriteConfig(const std::string &path, const std::string &fields)
{
#ifdef _WIN32
    std::ofstream stream(WindowsPath(path).c_str(), std::ios::binary | std::ios::trunc);
#else
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
#endif
    stream << "instrument: {sweep: {" << fields << "}}\n";
    stream.close();
    Require(static_cast<bool>(stream), "写入功率测试配置失败");
}

void RejectConfig(const std::string &path, const std::string &fields, const std::string &hint)
{
    WriteConfig(path, fields);
    bool rejected = false;
    try {
        VnaServer::LoadServerConfig(path);
    } catch (const std::invalid_argument &error) {
        rejected = true;
        Require(std::string(error.what()).find(hint) != std::string::npos,
            "功率配置错误未定位字段或提供迁移说明：" + std::string(error.what()));
    }
    Require(rejected, "非法功率配置未拒绝：" + fields);
}

void TestPowerMapping(const std::string &path)
{
    const std::pair<const char *, double> accepted[] = {
        {"-120", -120.0}, {"100", 100.0}, {"-17.5", -17.5}, {"0.25", 0.25}, {"-1.75e1", -17.5}, {"0", 0.0}};
    for (const auto &entry : accepted) {
        WriteConfig(path, std::string("defaultPowerDbm: ") + entry.first);
        Require(VnaServer::LoadServerConfig(path).instrument.defaultPowerDbm == entry.second,
            "dBm 配置未保留边界、负数或分数");
    }
    const char *invalid[] = {"-120.01", "100.01", ".nan", ".inf", "-.inf", "1e400", "1e-400", "'0.25'", "true", "null",
        "[]", "{}", "invalid", "0.25dBm"};
    for (const char *value : invalid) {
        RejectConfig(path, std::string("defaultPowerDbm: ") + value, "defaultPowerDbm");
    }
    RejectConfig(path, "defaultPowerDbm: 0, defaultPowerDbm: 1", "配置键重复");
}

void TestLegacyZeroOnly(const std::string &path)
{
    WriteConfig(path, "defaultPowerRaw: 0");
    Require(VnaServer::LoadServerConfig(path).instrument.defaultPowerDbm == 0.0, "旧整数零应兼容为 0 dBm");
    for (const char *value : {"1", "17", "18446744073709551615"}) {
        RejectConfig(path, std::string("defaultPowerRaw: ") + value, "请改用 defaultPowerDbm");
    }
    for (const char *value :
        {"-1", "-0", "+0", "0.0", "0e0", "0x0", "'0'", "true", "null", "[]", "18446744073709551616"}) {
        RejectConfig(path, std::string("defaultPowerRaw: ") + value, "defaultPowerRaw");
    }
    RejectConfig(path, "defaultPowerDbm: 0, defaultPowerRaw: 0", "不能同时配置");
    RejectConfig(path, "defaultPowerRaw: 0, defaultPowerDbm: -17.5", "不能同时配置");
}

void TestProfilePowerValidation()
{
    VnaService::InstrumentProfile profile;
    Require(profile.defaultPowerDbm == 0.0, "缺省功率必须为 0 dBm");
    for (double value : {-120.0, 100.0, -17.5, 0.25}) {
        profile.defaultPowerDbm = value;
        VnaService::ValidateInstrumentProfile(profile);
    }
    for (double value : {-120.01, 100.01, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        profile.defaultPowerDbm = value;
        bool rejected = false;
        try {
            VnaService::ValidateInstrumentProfile(profile);
        } catch (const std::invalid_argument &error) {
            rejected = true;
            Require(std::string(error.what()).find("defaultPowerDbm") != std::string::npos,
                "非法 profile 功率必须定位 dBm 字段");
        }
        Require(rejected, "程序化 profile 必须拒绝非有限或越界 dBm");
    }
}

}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    try {
        const std::string path(argv[1]);
        TestPowerMapping(path);
        TestLegacyZeroOnly(path);
        TestProfilePowerValidation();
#ifdef _WIN32
        Require(_wremove(WindowsPath(path).c_str()) == 0, "功率测试配置清理失败");
#else
        Require(std::remove(path.c_str()) == 0, "功率测试配置清理失败");
#endif
        std::cout << "PASS dBm 默认功率配置与旧零值兼容\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
