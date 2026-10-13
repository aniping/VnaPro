/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "startup_paths.h"
#include "web/static_file.h"

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
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    Expect(length > 0, "测试路径不是合法 UTF-8");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    Expect(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) == length,
        "测试路径转换失败");
    return result;
}
#endif

void MakeDirectory(const std::string &path)
{
#ifdef _WIN32
    const int result = _wmkdir(WindowsPath(path).c_str());
#else
    const int result = mkdir(path.c_str(), 0700);
#endif
    Expect(result == 0 || errno == EEXIST, "无法创建隔离测试目录");
}

void WriteFile(const std::string &path, const std::string &content)
{
#ifdef _WIN32
    std::ofstream output(WindowsPath(path).c_str(), std::ios::binary | std::ios::trunc);
#else
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
#endif
    output << content;
    output.close();
    Expect(static_cast<bool>(output), "无法写入测试资源");
}

void RemoveFile(const std::string &path)
{
#ifdef _WIN32
    static_cast<void>(_wremove(WindowsPath(path).c_str()));
#else
    static_cast<void>(std::remove(path.c_str()));
#endif
}

void RemoveDirectory(const std::string &path)
{
#ifdef _WIN32
    static_cast<void>(_wrmdir(WindowsPath(path).c_str()));
#else
    static_cast<void>(rmdir(path.c_str()));
#endif
}

std::string TestDirectory()
{
    const auto directory = VnaServer::ExecutableDirectory();
    Expect(!directory.empty(), "无法解析测试程序目录");
#ifdef _WIN32
    Expect(directory.front() == '/' || directory.find(":/") != std::string::npos, "程序目录不是绝对路径");
    const auto processId = GetCurrentProcessId();
#else
    Expect(directory.front() == '/', "程序目录不是绝对路径");
    const auto processId = getpid();
#endif
    return directory + "/发布目录 中文 " + std::to_string(processId);
}

void Cleanup(const std::string &directory)
{
    // 仅删除本测试明确创建的固定文件和空目录，不递归删除任何外部目录。
    RemoveFile(directory + "/vnapro.yml");
    RemoveFile(directory + "/driver08.dll");
    RemoveFile(directory + "/driver10.dll");
    RemoveFile(directory + "/.vnapro-release-files");
    RemoveFile(directory + "/WebUI/index.html");
    RemoveDirectory(directory + "/WebUI");
    RemoveDirectory(directory);
}

void TestResourcePreference(const std::string &directory)
{
    MakeDirectory(directory);
    MakeDirectory(directory + "/WebUI");
    WriteFile(directory + "/vnapro.yml", "instrument: {portCount: 2}\n");
    WriteFile(directory + "/driver08.dll", "test driver 08");
    WriteFile(directory + "/driver10.dll", "test driver 10");
    WriteFile(directory + "/WebUI/index.html", "<html>release WebUI</html>");
    const std::string fallback = "/unavailable development path";
    for (const std::string &name : {"vnapro.yml", "driver08.dll", "driver10.dll"}) {
        Expect(VnaServer::DefaultStartupPath(directory, name, fallback) == directory + "/" + name,
            "程序旁的发布资源未优先使用：" + name);
    }
    const auto webRoot =
        VnaServer::DefaultStartupPath(directory, "WebUI", fallback, VnaServer::StartupPathKind::DIRECTORY);
    Expect(webRoot == directory + "/WebUI", "中文 WebUI 目录未优先使用");
    Expect(VnaServer::DefaultStartupPath(directory, "missing.yml", fallback) == fallback,
        "缺少发布文件时未保留开发回退路径");
    Expect(VnaServer::DefaultStartupPath(directory, "WebUI", fallback) == fallback, "目录被错误识别为文件");
    Expect(VnaServer::DefaultStartupPath(directory, "vnapro.yml", fallback, VnaServer::StartupPathKind::DIRECTORY) ==
               fallback,
        "文件被错误识别为目录");
}

void TestUnicodePage(const std::string &webRoot)
{
    VnaWeb::StaticFile page(webRoot, "/");
    std::string content;
    while (!page.Advance(content)) {
    }
    Expect(content == "<html>release WebUI</html>", "中文发布目录中的静态网页不可读");
    page.Close();
    bool rejected = false;
    try {
        page.Open(webRoot, "/../vnapro.yml");
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    Expect(rejected, "中文路径支持改变了目录逃逸防护");
}

void TestReleaseIsolation(const std::string &directory)
{
    const std::string fallback = "/unavailable development path";
    WriteFile(directory + "/.vnapro-release-files", "vnapro.yml\ndriver08.dll\ndriver10.dll\nWebUI/index.html\n");
    Expect(VnaServer::DefaultStartupPath(directory, "vnapro.yml", fallback) == directory + "/vnapro.yml",
        "完整发布配置未从程序旁读取");
    RemoveFile(directory + "/vnapro.yml");
    RemoveFile(directory + "/driver10.dll");
    RemoveFile(directory + "/WebUI/index.html");
    RemoveDirectory(directory + "/WebUI");
    Expect(VnaServer::DefaultStartupPath(directory, "driver10.dll", fallback) == directory + "/driver10.dll",
        "发布包缺少驱动时偷偷使用了开发驱动");
    Expect(VnaServer::DefaultStartupPath(directory, "WebUI", fallback, VnaServer::StartupPathKind::DIRECTORY) ==
               directory + "/WebUI",
        "发布包缺少前端时偷偷使用了源码目录");
    Expect(VnaServer::DefaultStartupPath(directory, "vnapro.yml", fallback) == directory + "/vnapro.yml",
        "发布包缺少配置时偷偷使用了开发配置");
}
}  // 匿名命名空间

int main()
{
    std::string directory;
    try {
        directory = TestDirectory();
        TestResourcePreference(directory);
        TestUnicodePage(directory + "/WebUI");
        TestReleaseIsolation(directory);
        Cleanup(directory);
        std::cout << "启动资源路径和中文静态目录验收通过\n";
        return 0;
    } catch (const std::exception &error) {
        if (!directory.empty()) {
            Cleanup(directory);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
