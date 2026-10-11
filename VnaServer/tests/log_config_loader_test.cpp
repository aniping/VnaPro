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

#include "log_config_loader.h"

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
    Expect(static_cast<bool>(file), "写入测试日志配置失败");
}

void ExpectInvalid(const std::string &path, const std::string &text, const std::string &hint)
{
    WriteConfig(path, text);
    bool rejected = false;
    try {
        VnaServer::LoadLogConfig(path);
    } catch (const std::invalid_argument &error) {
        rejected = true;
        Expect(std::string(error.what()).find(hint) != std::string::npos, "配置错误缺少预期定位信息：" + hint);
    }
    Expect(rejected, "非法日志配置未被拒绝");
}

void TestMapping(const std::string &path, const std::string &directory)
{
    WriteConfig(path,
        "\xef\xbb\xbf# 中文注释\r\n\r\n  level = DEBUG  \r\nconsole.enabled=false\nfile.enabled=true\n"
        "file.path=output/service.log\nfile.max_bytes=8192\nfile.backups=3\nfile.max_record_bytes=1024\n"
        "file.rotate_on_open=true\nflush.level=ERROR\npattern=%Y [%K] %n - value=%v\n");
    const auto config = VnaServer::LoadLogConfig(path);
    Expect(config.level == VnaLog::Level::DEBUG && config.flushLevel == VnaLog::Level::ERROR_LEVEL, "级别映射错误");
    Expect(!config.consoleEnabled && config.fileEnabled && config.rotateOnOpen, "布尔值映射错误");
    Expect(config.maxFileBytes == 8192U && config.maxFiles == 3U && config.maxRecordBytes == 1024U, "数值映射错误");
    Expect(config.filePath == directory + "/output/service.log", "相对路径未使用配置文件目录");
    Expect(config.pattern == "%Y [%K] %n - value=%v", "格式中的等号未被保留");
}

void TestDefaults(const std::string &path, const std::string &directory)
{
    WriteConfig(path, "# 仅使用默认配置\n\n");
    const auto config = VnaServer::LoadLogConfig(path);
    Expect(config.level == VnaLog::Level::INFO && config.flushLevel == VnaLog::Level::INFO, "默认级别错误");
    Expect(config.consoleEnabled && config.fileEnabled && !config.rotateOnOpen, "默认输出开关错误");
    Expect(config.maxFileBytes == 10485760U && config.maxFiles == 5U && config.maxRecordBytes == 65536U,
        "默认滚动上限错误");
    Expect(config.filePath == directory + "/logs/vnapro.log", "默认文件路径未使用配置文件目录");
}

void TestAbsolutePaths(const std::string &path)
{
    const std::string paths[] = {"C:/service/logs/vna.log", "D:\\日志\\vna.log", "/var/log/vna.log",
        "\\\\server\\share\\vna.log", "\\logs\\vna.log"};
    for (const auto &filePath : paths) {
        WriteConfig(path, "file.path=" + filePath + "\n");
        Expect(VnaServer::LoadLogConfig(path).filePath == filePath, "绝对文件路径被错误拼接");
    }
}

void TestUnicodeFile(const std::string &directory)
{
    const std::string path = directory + "/日志配置_测量.properties";
    WriteConfig(path, "file.path=中文目录/采集日志.log\nlevel=DEBUG\n");
    try {
        const auto config = VnaServer::LoadLogConfig(path);
        Expect(config.level == VnaLog::Level::DEBUG, "中文名称的实际配置文件未正确读取");
        Expect(config.filePath == directory + "/中文目录/采集日志.log", "中文日志文件路径映射错误");
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
        WriteConfig(path, std::string("level=") + level.first + "\nflush.level=" + level.first + "\n");
        const auto config = VnaServer::LoadLogConfig(path);
        Expect(config.level == level.second && config.flushLevel == level.second, "级别名称映射错误");
    }
}

void TestMalformedEntries(const std::string &path)
{
    // 语法错误必须给出行号；已解析的字段还必须保留键名。
    ExpectInvalid(path, "\nlevel=INFO\nlevel=DEBUG\n", "第 3 行，键 'level'");
    ExpectInvalid(path, "unexpected=true\n", "第 1 行，键 'unexpected'");
    ExpectInvalid(path, "level DEBUG\n", "第 1 行");
    ExpectInvalid(path, "=DEBUG\n", "配置键不能为空");
    ExpectInvalid(path, "level=debug\n", "键 'level'");
    ExpectInvalid(path, "flush.level=FATAL\n", "键 'flush.level'");
    ExpectInvalid(path, "console.enabled=TRUE\n", "键 'console.enabled'");
    ExpectInvalid(path, "file.enabled=1\n", "键 'file.enabled'");
    ExpectInvalid(path, "file.rotate_on_open=false # 注释\n", "键 'file.rotate_on_open'");
    ExpectInvalid(path, "file.path= \n", "键 'file.path'");
    ExpectInvalid(path, "file.backups=-1\n", "键 'file.backups'");
    ExpectInvalid(path, "file.max_bytes=+1024\n", "键 'file.max_bytes'");
    ExpectInvalid(path, "file.max_record_bytes=1.5\n", "键 'file.max_record_bytes'");
    ExpectInvalid(path, "file.backups=\n", "键 'file.backups'");
    ExpectInvalid(path, "file.max_bytes=184467440737095516160\n", "整数溢出");
    ExpectInvalid(path, std::string("level=INFO\0invalid\n", 19U), "包含空字符");
}

void TestSizeLimits(const std::string &path)
{
    ExpectInvalid(path, "#" + std::string(2048U, 'x') + "\n", "单行超过 2048 字节");
    ExpectInvalid(path, std::string(65537U, '\n'), "超过 64 KiB");
    // 边界大小本身合法，不能把达到上限误判成超过上限。
    WriteConfig(path, std::string(65536U, '\n'));
    Expect(VnaServer::LoadLogConfig(path).level == VnaLog::Level::INFO, "64 KiB 合法配置被拒绝");
    WriteConfig(path, "#" + std::string(2047U, 'x') + "\n");
    Expect(VnaServer::LoadLogConfig(path).maxFiles == 5U, "2048 字节合法行被拒绝");
}

void TestBackendConstraints(const std::string &path)
{
    ExpectInvalid(path, "file.max_bytes=1023\n", "第 1 行，键 'file.max_bytes'");
    ExpectInvalid(path, "file.max_bytes=4294967297\n", "键 'file.max_bytes'");
    ExpectInvalid(path, "file.backups=1001\n", "键 'file.backups'");
    ExpectInvalid(path, "file.max_record_bytes=31\n", "键 'file.max_record_bytes'");
    ExpectInvalid(path, "pattern=\n", "键 'pattern'");
    ExpectInvalid(path, "pattern=" + std::string(1025U, 'x') + "\n", "键 'pattern'");
    ExpectInvalid(path, "pattern=%v\rforged\n", "键 'pattern'");
    WriteConfig(path, "file.max_bytes=1024\nfile.backups=0\nfile.max_record_bytes=32\npattern=%v\n");
    const auto config = VnaServer::LoadLogConfig(path);
    Expect(config.maxFileBytes == 1024U && config.maxFiles == 0U && config.maxRecordBytes == 32U,
        "合法的后端约束边界被拒绝");
}

void TestMissingFile(const std::string &path)
{
    Expect(RemoveConfig(path) == 0, "清理测试配置失败");
    bool rejected = false;
    try {
        VnaServer::LoadLogConfig(path);
    } catch (const std::runtime_error &error) {
        rejected = true;
        Expect(std::string(error.what()).find(path) != std::string::npos, "缺失文件错误未给出路径");
    }
    Expect(rejected, "缺失配置文件未被拒绝");
}

void RunTests(const std::string &path, const std::string &directory)
{
    TestMapping(path, directory);
    TestDefaults(path, directory);
    TestAbsolutePaths(path);
    TestUnicodeFile(directory);
    TestLevels(path);
    TestMalformedEntries(path);
    TestSizeLimits(path);
    TestBackendConstraints(path);
    TestMissingFile(path);
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    const std::string directory(argv[1]);
    const std::string path = directory + "/logging.properties";
    try {
        CreateDirectory(directory);
        RunTests(path, directory);
        std::cout << "日志配置加载验证通过\n";
        return 0;
    } catch (const std::exception &error) {
        RemoveConfig(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
