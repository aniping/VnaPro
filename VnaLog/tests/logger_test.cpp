/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_log/logger.h"

#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string Read(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool Exists(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

std::string Archive(const std::string &path, unsigned index)
{
    const auto extension = path.rfind('.');
    return path.substr(0, extension) + "." + std::to_string(index) + path.substr(extension);
}

void Clean(const std::string &path)
{
    VnaLog::Logger::Instance().Shutdown();
    std::remove(path.c_str());
    for (unsigned index = 1U; index <= 6U; ++index) {
        std::remove(Archive(path, index).c_str());
    }
}

VnaLog::LogConfig FileConfig(const std::string &path)
{
    VnaLog::LogConfig config;
    config.filePath = path;
    config.consoleEnabled = false;
    return config;
}

void CheckFilteringAndFormat(const std::string &path)
{
    Clean(path);
    auto &logger = VnaLog::Logger::Instance();
    Require(!logger.Enabled(VnaLog::Level::INFO), "配置前应静默");
    logger.Configure(FileConfig(path));
    VnaLog::Debug(VnaLog::Module::CORE, "不应出现");
    VnaLog::Info(VnaLog::Module::CORE, "消息\n下一行\t{}", std::string(1U, '\1'));
    // INFO 自动刷新，读取前无需调用 Flush。
    const std::string first = Read(path);
    Require(first.find("消息\\n下一行\\t\\x01") != std::string::npos, "控制字符必须可见转义");
    const std::regex prefix(
        "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} "
        "\\[INFO\\] \\[[0-9]+\\] VnaCore - ");
    Require(std::regex_search(first, prefix), "默认格式应包含时间、级别、线程和模块");
    VnaLog::Error(VnaLog::Module::SERVICE, "error={}", 7);
    logger.Write(VnaLog::Level::OFF, VnaLog::Module::CORE, "关闭等级不输出");
    logger.Shutdown();
    const std::string output = Read(path);
    Require(output.find("不应出现") == std::string::npos, "DEBUG 过滤失败");
    Require(output.find("[ERROR]") != std::string::npos, "错误级别应使用大写");
    Require(std::count(output.begin(), output.end(), '\n') == 2, "换行不得伪造额外记录");
    Require(!logger.Enabled(VnaLog::Level::INFO), "退出后不得继续输出");
}

void CheckRotation(const std::string &path, std::size_t backups)
{
    Clean(path);
    auto config = FileConfig(path);
    config.maxFileBytes = 1024U;
    config.maxRecordBytes = 200U;
    config.maxFiles = backups;
    config.pattern = "%v";
    VnaLog::Logger::Instance().Configure(config);
    for (unsigned index = 0U; index < 50U; ++index) {
        VnaLog::Info(VnaLog::Module::CORE, "record={} {}", index, std::string(150U, 'x'));
    }
    VnaLog::Logger::Instance().Shutdown();
    Require(Read(path).size() <= config.maxFileBytes, "活动文件超过上限");
    Require(Read(path).find("record=49") != std::string::npos, "最新记录应保留");
    for (unsigned index = 1U; index <= backups; ++index) {
        Require(Exists(Archive(path, index)), "备份文件未生成");
        Require(Read(Archive(path, index)).size() <= config.maxFileBytes, "备份文件超过上限");
    }
    Require(!Exists(Archive(path, static_cast<unsigned>(backups + 1U))), "备份淘汰失败");
}

void CheckLongUtf8(const std::string &path)
{
    Clean(path);
    auto config = FileConfig(path);
    config.maxFileBytes = 1024U;
    config.maxRecordBytes = 127U;
    config.pattern = "%n %v";
    VnaLog::Logger::Instance().Configure(config);
    std::string message;
    for (unsigned index = 0U; index < 1000U; ++index) {
        message += "采集";
    }
    VnaLog::Info(VnaLog::Module::CORE, "{}", message);
    VnaLog::Logger::Instance().Shutdown();
    const std::string output = Read(path);
    Require(output.size() <= config.maxRecordBytes, "整行输出超过记录上限");
    Require(output.find("...[truncated]\n") != std::string::npos, "长消息缺少截断标记");
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> utf8;
    Require(!utf8.from_bytes(output).empty(), "截断不得破坏 UTF-8");
    // 记录上限大于文件上限时，仍须约束整个格式化记录。
    config.maxRecordBytes = 65536U;
    VnaLog::Logger::Instance().Configure(config);
    VnaLog::Info(VnaLog::Module::CORE, "{}", message);
    VnaLog::Logger::Instance().Shutdown();
    Require(Read(path).size() <= config.maxFileBytes, "超长单条记录突破文件上限");
}

void CheckRestart(const std::string &path)
{
    Clean(path);
    auto config = FileConfig(path);
    config.pattern = "%v";
    VnaLog::Logger::Instance().Configure(config);
    VnaLog::Info(VnaLog::Module::CORE, "before-restart");
    VnaLog::Logger::Instance().Shutdown();
    config.rotateOnOpen = true;
    VnaLog::Logger::Instance().Configure(config);
    VnaLog::Info(VnaLog::Module::CORE, "after-restart");
    VnaLog::Logger::Instance().Shutdown();
    Require(Read(path) == "after-restart\n", "启动轮转后活动文件应从新记录开始");
    Require(Read(Archive(path, 1U)) == "before-restart\n", "启动轮转应保留之前的记录");
}

void CheckBufferedReconfiguration(const std::string &path)
{
    Clean(path);
    auto config = FileConfig(path);
    config.maxFileBytes = 1024U;
    config.maxRecordBytes = 800U;
    config.flushLevel = VnaLog::Level::OFF;
    config.pattern = "%v";
    auto &logger = VnaLog::Logger::Instance();
    logger.Configure(config);
    VnaLog::Info(VnaLog::Module::CORE, "{}", std::string(700U, 'a'));
    // 重配同一目标前必须刷新旧缓冲，否则新 sink 会低估活动文件大小。
    logger.Configure(config);
    VnaLog::Info(VnaLog::Module::CORE, "{}", std::string(700U, 'b'));
    logger.Shutdown();
    Require(Read(path).size() == 701U, "重配后活动文件不应累积超过大小上限");
    Require(Read(Archive(path, 1U)).size() == 701U, "重配不得遗失旧缓冲中的记录");
}

void CheckConcurrentModules(const std::string &path)
{
    Clean(path);
    auto config = FileConfig(path);
    config.pattern = "%n %v";
    VnaLog::Logger::Instance().Configure(config);
    std::vector<std::thread> writers;
    for (unsigned thread = 0U; thread < 8U; ++thread) {
        writers.emplace_back([thread] {
            const auto module = static_cast<VnaLog::Module>(thread % static_cast<unsigned>(VnaLog::Module::COUNT));
            for (unsigned index = 0U; index < 100U; ++index) {
                VnaLog::Info(module, "writer={} record={}", thread, index);
            }
        });
    }
    for (auto &writer : writers) {
        writer.join();
    }
    VnaLog::Logger::Instance().Shutdown();
    std::istringstream records(Read(path));
    std::set<std::string> unique;
    std::string line;
    const std::regex record("^Vna(Core|Service|Server|Scpi|Web) writer=[0-7] record=[0-9]{1,2}$");
    unsigned count = 0U;
    while (std::getline(records, line)) {
        Require(std::regex_match(line, record), "多模块并发写入出现半条或拼接记录");
        Require(unique.insert(line).second, "并发记录重复");
        ++count;
    }
    Require(count == 800U, "多线程记录丢失");
}

void ExpectRejected(const VnaLog::LogConfig &config)
{
    bool rejected = false;
    try {
        VnaLog::Logger::Instance().Configure(config);
    } catch (const std::exception &) {
        rejected = true;
    }
    Require(rejected, "无效日志配置应拒绝");
}

void CheckFailureHandling(const std::string &path)
{
    Clean(path);
    auto config = FileConfig(path);
    VnaLog::Logger::Instance().Configure(config);
    auto invalid = config;
    invalid.maxFileBytes = 0U;
    ExpectRejected(invalid);
    invalid = config;
    invalid.maxFiles = 1001U;
    ExpectRejected(invalid);
    invalid = config;
    invalid.pattern = "伪造\n换行";
    ExpectRejected(invalid);
    invalid = config;
    invalid.filePath = path.substr(0U, path.find_last_of('/'));
    ExpectRejected(invalid);
    Require(VnaLog::Logger::Instance().Enabled(VnaLog::Level::INFO), "初始化失败不得替换原配置");
    // 运行时格式异常必须由日志层吞掉，随后业务仍能写入。
    VnaLog::Info(VnaLog::Module::CORE, std::string("{"));
    VnaLog::Info(VnaLog::Module::CORE, "格式异常后仍可继续");
    VnaLog::Logger::Instance().Shutdown();
    Require(Read(path).find("格式异常后仍可继续") != std::string::npos, "日志异常影响后续调用");
    config.fileEnabled = false;
    VnaLog::Logger::Instance().Configure(config);
    Require(!VnaLog::Logger::Instance().Enabled(VnaLog::Level::INFO), "全部输出关闭时应跳过格式化");
}

}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "需要隔离的测试目录\n";
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/logger-test.log";
    try {
        CheckFilteringAndFormat(path);
        CheckRotation(path, 2U);
        CheckRotation(path, 0U);
        CheckLongUtf8(path);
        CheckRestart(path);
        CheckBufferedReconfiguration(path);
        CheckConcurrentModules(path);
        CheckFailureHandling(path);
        Clean(path);
        std::cout << "日志格式、过滤、轮转、边界和并发验证通过\n";
        return 0;
    } catch (const std::exception &error) {
        VnaLog::Logger::Instance().Shutdown();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
