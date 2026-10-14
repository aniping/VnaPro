/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "server_config_loader.h"

namespace {
void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    Expect(path.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()), "测试路径过长");
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    Expect(length > 0, "测试路径不是合法 UTF-8");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    Expect(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) == length,
        "测试路径转换失败");
    return result;
}
#endif

int RemoveConfig(const std::string &path)
{
#ifdef _WIN32
    return _wremove(WindowsPath(path).c_str());
#else
    return std::remove(path.c_str());
#endif
}

void CreateDirectory(const std::string &directory)
{
#ifdef _WIN32
    const int result = _wmkdir(WindowsPath(directory).c_str());
#else
    const int result = mkdir(directory.c_str(), 0700);
#endif
    Expect(result == 0 || errno == EEXIST, "无法创建测试隔离目录");
}

void WriteConfig(const std::string &path, const std::string &text)
{
#ifdef _WIN32
    std::ofstream file(WindowsPath(path).c_str(), std::ios::binary | std::ios::trunc);
#else
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
#endif
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    Expect(static_cast<bool>(file), "写入测试服务配置失败");
}

void ExpectInvalid(const std::string &path, const std::string &text, const std::string &hint)
{
    WriteConfig(path, text);
    bool rejected = false;
    try {
        VnaServer::LoadServerConfig(path);
    } catch (const std::invalid_argument &error) {
        rejected = true;
        Expect(std::string(error.what()).find(hint) != std::string::npos,
            "配置错误缺少预期定位信息：" + hint + "，实际错误：" + error.what());
    }
    Expect(rejected, "非法服务配置未被拒绝：" + text);
}

void TestMapping(const std::string &path, const std::string &directory)
{
    WriteConfig(path,
        "\xef\xbb\xbf# 中文注释\r\nlog:\r\n  level: DEBUG\r\n  console: {enabled: false}\n"
        "  file:\n    enabled: true\n    path: output/service.log\n    maxSizeBytes: 8192\n    maxFiles: 3\n"
        "    maxRecordBytes: 1024\n    rotateOnOpen: true\n  flush: {level: ERROR}\n"
        "  pattern: '%Y [%K] %n - value=%v'\n"
        "instrument:\n  portCount: 2\n  frequency: {minHz: 10000000, maxHz: 8000000000}\n"
        "  sweep: {defaultStartHz: 20000000, defaultStopHz: 6000000000, defaultPoints: 401, defaultIfBandwidthHz: "
        "100, defaultPowerRaw: 18446744073709551615, defaultSweepMode: HOLD}\n");
    const auto config = VnaServer::LoadServerConfig(path);
    Expect(config.log.level == VnaLog::Level::DEBUG && config.log.flushLevel == VnaLog::Level::ERROR_LEVEL,
        "级别映射错误");
    Expect(!config.log.consoleEnabled && config.log.fileEnabled && config.log.rotateOnOpen, "布尔值映射错误");
    Expect(config.log.maxFileBytes == 8192U && config.log.maxFiles == 3U && config.log.maxRecordBytes == 1024U,
        "滚动日志数值映射错误");
    Expect(config.log.filePath == directory + "/output/service.log", "相对路径未使用配置文件目录");
    Expect(config.log.pattern == "%Y [%K] %n - value=%v", "格式中的等号未被保留");
    const auto &profile = config.instrument;
    Expect(profile.portCount == 2U && profile.minFrequencyHz == 1e7 && profile.maxFrequencyHz == 8e9,
        "二端口能力映射错误");
    Expect(profile.defaultStartHz == 2e7 && profile.defaultStopHz == 6e9 && profile.defaultPoints == 401U &&
               profile.defaultIfBandwidthHz == 100.0 &&
               profile.defaultPowerRaw == std::numeric_limits<std::uint64_t>::max() &&
               config.defaultSweepMode == VnaService::SweepMode::HOLD,
        "扫描默认配置映射错误");
}

void TestDefaults(const std::string &path, const std::string &directory)
{
    WriteConfig(path, "# 仅使用默认配置\n\n");
    const auto config = VnaServer::LoadServerConfig(path);
    Expect(config.log.level == VnaLog::Level::INFO && config.log.flushLevel == VnaLog::Level::INFO, "默认级别错误");
    Expect(config.log.consoleEnabled && config.log.fileEnabled && !config.log.rotateOnOpen, "默认输出开关错误");
    Expect(config.log.maxFileBytes == 10485760U && config.log.maxFiles == 5U && config.log.maxRecordBytes == 65536U,
        "默认滚动上限错误");
    Expect(config.log.filePath == directory + "/logs/vnapro.log", "默认文件路径未使用配置文件目录");
    const auto &profile = config.instrument;
    Expect(profile.portCount == 4U && profile.minFrequencyHz == 1e7 && profile.maxFrequencyHz == 8e9,
        "默认四端口能力错误");
    Expect(profile.defaultStartHz == profile.minFrequencyHz && profile.defaultStopHz == profile.maxFrequencyHz &&
               profile.defaultPoints == 201U && profile.defaultIfBandwidthHz == 1000.0 &&
               profile.defaultPowerRaw == 0U && config.defaultSweepMode == VnaService::SweepMode::CONTINUOUS,
        "默认扫描未覆盖完整频段");
    Expect(config.server.ip == "127.0.0.1" && config.server.scpiPort == 5025U && config.server.webPort == 8080U &&
               config.server.webEnabled && config.server.scpiIp.empty() && config.server.webIp.empty() &&
               config.server.webRoot == "WebUI",
        "默认监听配置错误");
    Expect(config.driver.type == VnaServer::DriverType::HARDWARE && config.driver.api == VnaCore::DriverApi::TDC1VN10 &&
               config.driver.library.empty() && config.driver.simulator.mode == "golden" &&
               config.driver.simulator.seed == 42U && config.driver.simulator.noiseAmplitude == 0.002,
        "默认驱动配置错误");
    WriteConfig(path, "instrument: {portCount: 2}\nlog: {}\n");
    Expect(VnaServer::LoadServerConfig(path).instrument.portCount == 2U, "部分配置未保留其他默认值");
}

void TestAbsolutePaths(const std::string &path)
{
    const std::string paths[] = {"C:/service/logs/vna.log", "D:\\日志\\vna.log", "/var/log/vna.log",
        "\\\\server\\share\\vna.log", "\\logs\\vna.log"};
    for (const auto &filePath : paths) {
        WriteConfig(path, "log:\n  file: {path: '" + filePath + "'}\n");
        Expect(VnaServer::LoadServerConfig(path).log.filePath == filePath, "绝对文件路径被错误拼接");
    }
}

void TestUnicodeFile(const std::string &directory)
{
    const std::string path = directory + "/服务配置_测量.yml";
    WriteConfig(path, "log:\n  level: DEBUG\n  file: {path: 中文目录/采集日志.log}\n");
    try {
        const auto config = VnaServer::LoadServerConfig(path);
        Expect(config.log.level == VnaLog::Level::DEBUG, "中文名称的实际配置文件未正确读取");
        Expect(config.log.filePath == directory + "/中文目录/采集日志.log", "中文日志文件路径映射错误");
        Expect(RemoveConfig(path) == 0, "清理中文配置文件失败");
    } catch (...) {
        RemoveConfig(path);
        throw;
    }
}

void TestLevels(const std::string &path)
{
    const std::pair<const char *, VnaLog::Level> levels[] = {{"TRACE", VnaLog::Level::TRACE},
        {"DEBUG", VnaLog::Level::DEBUG}, {"INFO", VnaLog::Level::INFO}, {"WARN", VnaLog::Level::WARN},
        {"ERROR", VnaLog::Level::ERROR_LEVEL}, {"CRITICAL", VnaLog::Level::CRITICAL}, {"OFF", VnaLog::Level::OFF}};
    for (const auto &level : levels) {
        WriteConfig(path, std::string("log:\n  level: ") + level.first + "\n  flush: {level: " + level.first + "}\n");
        const auto config = VnaServer::LoadServerConfig(path);
        Expect(config.log.level == level.second && config.log.flushLevel == level.second, "级别名称映射错误");
    }
}

void TestYamlSchema(const std::string &path)
{
    ExpectInvalid(path, "log:\n  level: INFO\n  level: DEBUG\n", "第 3 行，键 'log.level'");
    ExpectInvalid(path, "log: {}\nlog: {}\n", "配置键重复");
    ExpectInvalid(path, "unexpected: true\n", "第 1 行，键 'unexpected'");
    ExpectInvalid(path, "log: {file: {unexpected: true}}\n", "log.file.unexpected");
    ExpectInvalid(path, "instrument: {sweep: {unexpected: true}}\n", "instrument.sweep.unexpected");
    ExpectInvalid(path, "log: [\n", "第");
    ExpectInvalid(path, "log: {}\n---\ninstrument: {}\n", "一个 YAML 文档");
    ExpectInvalid(path, "level=DEBUG\n", "必须为映射");
    ExpectInvalid(path, "log: null\n", "log");
    ExpectInvalid(path, "log: {file: []}\n", "log.file");
    ExpectInvalid(path, "instrument: {frequency: !custom {minHz: 1}}\n", "自定义标签");
    ExpectInvalid(path, "log: {level: debug}\n", "log.level");
    ExpectInvalid(path, "log: {flush: {level: FATAL}}\n", "log.flush.level");
    ExpectInvalid(path, "log: {console: {enabled: TRUE}}\n", "log.console.enabled");
    ExpectInvalid(path, "log: {file: {enabled: 'false'}}\n", "类型不合法");
    ExpectInvalid(path, "log: {file: {path: ''}}\n", "log.file.path");
    ExpectInvalid(path, "log: {file: {maxFiles: -1}}\n", "log.file.maxFiles");
    ExpectInvalid(path, "log: {file: {maxSizeBytes: +1024}}\n", "log.file.maxSizeBytes");
    ExpectInvalid(path, "log: {file: {maxRecordBytes: 1.5}}\n", "log.file.maxRecordBytes");
    ExpectInvalid(path, "log: {file: {maxFiles: null}}\n", "log.file.maxFiles");
    ExpectInvalid(path, "log: {file: {maxSizeBytes: 184467440737095516160}}\n", "整数溢出");
    ExpectInvalid(path, "log: {pattern: \"%v\\0\"}\n", "空字符");
    ExpectInvalid(path, "log: {file: {path: \"\\uD800\"}}\n", "unicode");
    ExpectInvalid(path, "log: {pattern: \"\\U00110000\"}\n", "unicode");
}

void TestEncodingAndSize(const std::string &path)
{
    ExpectInvalid(path, std::string("#\0invalid\n", 10U), "空字符");
    ExpectInvalid(path, "#" + std::string("\xc0\x80", 2U), "UTF-8");
    ExpectInvalid(path, "#" + std::string("\xed\xa0\x80", 3U), "UTF-8");
    ExpectInvalid(path, "#" + std::string("\xf4\x90\x80\x80", 4U), "UTF-8");
    ExpectInvalid(path, std::string(65537U, '\n'), "超过 64 KiB");
    // 达到文件大小上限本身合法，且注释中的无效编码也必须拒绝。
    WriteConfig(path, std::string(65536U, '\n'));
    Expect(VnaServer::LoadServerConfig(path).log.level == VnaLog::Level::INFO, "64 KiB 合法配置被拒绝");
}

void TestLogConstraints(const std::string &path)
{
    ExpectInvalid(path, "log: {file: {maxSizeBytes: 1023}}\n", "log");
    ExpectInvalid(path, "log: {file: {maxSizeBytes: 4294967297}}\n", "log");
    ExpectInvalid(path, "log: {file: {maxFiles: 1001}}\n", "log");
    ExpectInvalid(path, "log: {file: {maxRecordBytes: 31}}\n", "log");
    ExpectInvalid(path, "log: {pattern: ''}\n", "log");
    ExpectInvalid(path, "log: {pattern: '" + std::string(1025U, 'x') + "'}\n", "log");
    ExpectInvalid(path, "log: {pattern: \"%v\\nforged\"}\n", "log");
    WriteConfig(path, "log: {file: {maxSizeBytes: 1024, maxFiles: 0, maxRecordBytes: 32}, pattern: '%v'}\n");
    const auto config = VnaServer::LoadServerConfig(path);
    Expect(config.log.maxFileBytes == 1024U && config.log.maxFiles == 0U && config.log.maxRecordBytes == 32U,
        "合法的滚动日志边界被拒绝");
}

void TestInstrumentConstraints(const std::string &path)
{
    const std::string invalid[] = {"portCount: 0", "portCount: 5", "portCount: '2'", "portCount: 4294967296",
        "frequency: {minHz: 0}", "frequency: {maxHz: .inf}", "frequency: {minHz: 10000000.5}",
        "frequency: {maxHz: 9007199254740992}", "frequency: {minHz: 10000000.0000000001}",
        "frequency: {minHz: 9000000000}", "sweep: {defaultStartHz: 9999999}", "sweep: {defaultStopHz: 8000000001}",
        "sweep: {defaultStartHz: 20000000, defaultStopHz: 10000000}", "sweep: {defaultPoints: 1}",
        "sweep: {defaultPoints: 10002}", "sweep: {defaultIfBandwidthHz: 123}", "sweep: {defaultSweepMode: SING}",
        "sweep: {defaultSweepMode: hold}", "sweep: {defaultSweepMode: null}", "sweep: {defaultSweepMode: []}",
        "sweep: {defaultPowerRaw: -1}", "sweep: {defaultPowerRaw: '0'}", "sweep: {defaultPowerRaw: 1.5}",
        "sweep: {defaultPowerRaw: 18446744073709551616}"};
    for (const auto &entry : invalid) {
        ExpectInvalid(path, "instrument: {" + entry + "}\n", "instrument");
    }
    WriteConfig(path,
        "instrument:\n  portCount: 2\n  frequency: {minHz: 10000000, maxHz: 10000000}\n"
        "  sweep: {defaultStartHz: 10000000, defaultStopHz: 10000000, defaultPoints: 2, defaultIfBandwidthHz: 10}\n");
    const auto config = VnaServer::LoadServerConfig(path);
    Expect(config.instrument.minFrequencyHz == config.instrument.maxFrequencyHz &&
               config.instrument.defaultStartHz == config.instrument.defaultStopHz,
        "合法 CW 能力边界被拒绝");
}

void TestMissingFile(const std::string &path)
{
    Expect(RemoveConfig(path) == 0, "清理测试配置失败");
    bool rejected = false;
    try {
        VnaServer::LoadServerConfig(path);
    } catch (const std::runtime_error &error) {
        rejected = true;
        Expect(std::string(error.what()).find(path) != std::string::npos, "缺失文件错误未给出路径");
    }
    Expect(rejected, "缺失配置文件未被拒绝");
}

void TestStartupMapping(const std::string &path)
{
    WriteConfig(path,
        "server: {ip: 0.0.0.0, scpiIp: 127.0.0.1, webIp: 192.168.1.2, scpiPort: 6000, webPort: 8191, "
        "webEnabled: false, webRoot: '中文目录/前端 资源'}\n"
        "driver:\n  type: simulator\n  board: tdc1vn08\n  library: '测量驱动 08.dll'\n"
        "  simulator: {mode: noise, seed: 18446744073709551615}\n");
    const auto config = VnaServer::LoadServerConfig(path);
    Expect(config.server.ip == "0.0.0.0" && config.server.scpiPort == 6000U && config.server.webPort == 8191U &&
               !config.server.webEnabled && config.server.scpiIp == "127.0.0.1" &&
               config.server.webIp == "192.168.1.2" && config.server.webRoot == "中文目录/前端 资源",
        "监听配置映射错误");
    Expect(config.driver.type == VnaServer::DriverType::SIMULATOR &&
               config.driver.api == VnaCore::DriverApi::TDC1VN08 && config.driver.library == "测量驱动 08.dll",
        "驱动接口及文件名映射错误");
    Expect(config.driver.simulator.mode == "noise" &&
               config.driver.simulator.seed == std::numeric_limits<std::uint64_t>::max(),
        "噪声模式及 64 位种子映射错误");
    WriteConfig(path,
        "server: {ip: '255.255.255.255', scpiPort: 0, webPort: 65535}\n"
        "driver: {type: simulator, board: tdc1vn10, simulator: {mode: golden, seed: 0}}\n");
    const auto boundary = VnaServer::LoadServerConfig(path);
    Expect(boundary.server.scpiPort == 0U && boundary.server.webPort == 65535U, "合法端口边界被拒绝");
    Expect(boundary.driver.api == VnaCore::DriverApi::TDC1VN10 && boundary.driver.library.empty() &&
               boundary.driver.simulator.seed == 0U,
        "合法驱动配置边界被拒绝");
}

void TestListenerConstraints(const std::string &path)
{
    const std::string invalidAddresses[] = {"", "localhost", "::1", "127.1", "127.0.0.256", "127.0.0.1.", "127..0.1",
        "127.0.0.-1", "127.0.0.+1", "127.00.0.1", "127.0.0.001", " 127.0.0.1", "127.0.0.1 ", "127.0.0.1:8080",
        "127.0.0.1/path", "12345678901234567890.0.0.1"};
    for (const auto &address : invalidAddresses) {
        ExpectInvalid(path, "server: {ip: '" + address + "'}\n", "server.ip");
        ExpectInvalid(path, "server: {scpiIp: '" + address + "'}\n", "server.scpiIp");
        ExpectInvalid(path, "server: {webIp: '" + address + "'}\n", "server.webIp");
    }
    const std::string invalid[] = {"scpiPort: 65536", "webPort: 4294967296", "scpiPort: -1", "webPort: +8080",
        "scpiPort: 0x1389", "webPort: '8080'", "webPort: 1.0", "scpiPort: null", "webPort: []", "webEnabled: TRUE",
        "webEnabled: 'false'", "unexpected: true", "ip: null", "ip: []", "scpiIp: null", "webIp: []", "webRoot: ''",
        "webRoot: null", "webRoot: []", "webRoot: \"WebUI\\n\"", "webRoot: \"WebUI\\t\""};
    for (const auto &entry : invalid) {
        ExpectInvalid(path, "server: {" + entry + "}\n", "server");
    }
    ExpectInvalid(path, "server: null\n", "server");
    ExpectInvalid(path, "server: {scpiPort: 5025, scpiPort: 5026}\n", "server.scpiPort");
    const std::string roots[] = {
        "C:/service/WebUI", "/opt/vnapro/WebUI", "D:\\前端资源\\dist", "\\\\server\\share\\WebUI", "../WebUI/dist"};
    for (const auto &root : roots) {
        WriteConfig(path, "server: {webRoot: '" + root + "'}\n");
        Expect(VnaServer::LoadServerConfig(path).server.webRoot == root, "前端目录被配置解析器提前转换");
    }
}

void TestDriverConstraints(const std::string &path)
{
    const std::string invalidFilenames[] = {"", ".", "..", "../driver.dll", "sub/driver.dll", "sub\\driver.dll",
        "/driver.dll", "C:driver.dll", "C:\\driver.dll", "\\\\host\\driver.dll", "driver.dll:stream", "driver.dll.",
        " driver.dll", "driver.dll ", "driver?.dll", "driver*.dll", "driver|name.dll"};
    for (const auto &filename : invalidFilenames) {
        ExpectInvalid(path, "driver: {library: '" + filename + "'}\n", "driver.library");
    }
    const std::string invalid[] = {"board: unknown", "board: TDC1VN10", "board: null", "board: []", "library: null",
        "library: []", "simulator: {mode: invalid}", "simulator: {mode: NOISE}", "simulator: {mode: null}",
        "simulator: {seed: -1}", "simulator: {seed: +42}", "simulator: {seed: 0x2a}", "simulator: {seed: '42'}",
        "simulator: {seed: 1.5}", "simulator: {seed: 18446744073709551616}", "simulator: {seed: null}",
        "simulator: {seed: []}", "simulator: {unexpected: true}", "simulator: null", "unexpected: true"};
    for (const auto &entry : invalid) {
        ExpectInvalid(path, "driver: {type: simulator, " + entry + "}\n", "driver");
    }
    ExpectInvalid(path, "driver: null\n", "driver");
    ExpectInvalid(path, "driver: {library: \"driver\\n.dll\"}\n", "driver.library");
    ExpectInvalid(path, "driver: {type: simulator, simulator: {seed: 1, seed: 2}}\n", "driver.simulator.seed");
    ExpectInvalid(path, "driver: {type: invalid}\n", "driver.type");
    ExpectInvalid(path, "driver: {type: null}\n", "driver.type");
    ExpectInvalid(path, "driver: {type: []}\n", "driver.type");
}

void TestNoiseAmplitude(const std::string &path)
{
    const std::pair<const char *, double> values[] = {{"0", 0.0}, {"0e10", 0.0}, {"1", 1.0}, {"0.002", 0.002},
        {"0.01", 0.01}, {"1e-7", 1e-7}, {"4.9406564584124654e-324", std::numeric_limits<double>::denorm_min()}};
    for (const auto &value : values) {
        WriteConfig(path, std::string("driver: {type: simulator, simulator: {noiseAmplitude: ") + value.first + "}}\n");
        Expect(VnaServer::LoadServerConfig(path).driver.simulator.noiseAmplitude == value.second,
            "噪声幅度边界或科学计数法映射错误");
    }
    const std::string invalid[] = {"-0.001", "1.001", ".nan", ".inf", "-.inf", "1e400", "1e-400", "-1e-400", "1e-324",
        "true", "'0.002'", "null", "[]", "0.1abc", "0x1"};
    for (const auto &value : invalid) {
        ExpectInvalid(path, "driver: {type: simulator, simulator: {noiseAmplitude: " + value + "}}\n",
            "driver.simulator.noiseAmplitude");
    }
    ExpectInvalid(
        path, "driver: {type: simulator, simulator: {noiseAmplitude: 0.1, noiseAmplitude: 0.2}}\n", "配置键重复");
}

void TestHardwareIgnoresSimulatorConfig(const std::string &path)
{
    const char *unused[] = {"null", "[]", "invalid", "{mode: invalid, seed: -1, noiseAmplitude: .nan}",
        "{noiseAmplitude: 1e400}", "{unexpected: true}"};
    for (const char *settings : unused) {
        for (const char *type : {"", "type: hardware, "}) {
            WriteConfig(path, std::string("driver: {") + type +
                                  "board: tdc1vn08, library: actual.dll, simulator: " + settings + "}\n");
            const auto config = VnaServer::LoadServerConfig(path);
            Expect(config.driver.type == VnaServer::DriverType::HARDWARE && config.driver.simulator.mode == "golden" &&
                       config.driver.simulator.seed == 42U && config.driver.simulator.noiseAmplitude == 0.002,
                "真实驱动不得解析或使用模拟信号配置");
        }
    }
}

void RunTests(const std::string &path, const std::string &directory)
{
    TestMapping(path, directory);
    TestDefaults(path, directory);
    TestAbsolutePaths(path);
    TestUnicodeFile(directory);
    TestLevels(path);
    TestYamlSchema(path);
    TestEncodingAndSize(path);
    TestLogConstraints(path);
    TestInstrumentConstraints(path);
    TestStartupMapping(path);
    TestListenerConstraints(path);
    TestDriverConstraints(path);
    TestNoiseAmplitude(path);
    TestHardwareIgnoresSimulatorConfig(path);
    TestMissingFile(path);
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    const std::string directory(argv[1]);
    const std::string path = directory + "/vnapro.yml";
    try {
        CreateDirectory(directory);
        RunTests(path, directory);
        std::cout << "服务 YAML 配置加载验证通过\n";
        return 0;
    } catch (const std::exception &error) {
        RemoveConfig(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
