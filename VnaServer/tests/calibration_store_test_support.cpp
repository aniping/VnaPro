/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_store_test_support.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "calibration_store.h"
#include "web/json_parser.h"

namespace VnaServerTest {
using VnaService::CalibrationSetPtr;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

#ifdef _WIN32
std::wstring Native(const std::string &path)
{
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    Require(count > 0, "Invalid test path");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, &result[0], count) == count,
        "Cannot convert test path");
    result.pop_back();
    return result;
}
#else
const std::string &Native(const std::string &path)
{
    return path;
}
#endif

std::string Read(const std::string &path)
{
    std::ifstream stream(Native(path).c_str(), std::ios::binary);
    Require(static_cast<bool>(stream), "Cannot read test file: " + path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void Write(const std::string &path, const std::string &text)
{
    std::ofstream stream(Native(path).c_str(), std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    Require(static_cast<bool>(stream), "Cannot write test file: " + path);
}

void Remove(const std::string &path)
{
#ifdef _WIN32
    Require(DeleteFileW(Native(path).c_str()) != 0, "Cannot remove test file");
#else
    Require(std::remove(path.c_str()) == 0, "Cannot remove test file");
#endif
}

void Rename(const std::string &from, const std::string &to)
{
#ifdef _WIN32
    Require(MoveFileW(Native(from).c_str(), Native(to).c_str()) != 0, "Cannot rename test directory");
#else
    Require(std::rename(from.c_str(), to.c_str()) == 0, "Cannot rename test directory");
#endif
}

void MakeDirectory(const std::string &directory)
{
#ifdef _WIN32
    Require(CreateDirectoryW(Native(directory).c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS,
        "Cannot create test directory");
#else
    Require(mkdir(directory.c_str(), 0700) == 0 || errno == EEXIST, "Cannot create test directory");
#endif
}

void RemoveTestDirectory(const std::string &directory)
{
#ifdef _WIN32
    Require(RemoveDirectoryW(Native(directory).c_str()) != 0, "Cannot remove test directory");
#else
    Require(rmdir(directory.c_str()) == 0, "Cannot remove test directory");
#endif
}

CalibrationSetPtr Set(std::uint64_t id, bool four)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = {50000000.0, 40000000000.0};
    data->port1 = 1U;
    if (four) {
        data->method = VnaCalibration::CalibrationMethod::FOUR_PORT;
        data->port2 = 4U;
        VnaCalibration::FourPortTerms row;
        for (std::size_t cell = 0U; cell < row.size(); ++cell) {
            row[cell] = {{0.001 * (cell + 1U), 0.002}, {0.02, -0.01}, {0.8 + cell * 0.01, 0.03}};
        }
        data->fourPortTerms.assign(2U, row);
    } else {
        data->onePortTerms.assign(2U, {{0.02, 0.01}, {-0.1, 0.03}, {0.92, -0.08}});
    }
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->id = id;
    set->name = "真实校准 200\\300;\"" + std::to_string(id);
    set->deviceIdentity = "VNB,Explicit storage fixture,4PORT,1.0";
    set->includeIsolation = four;
    set->ifBandwidthHz = 1000.0;
    set->powerDbm = {-1.5, -1.5};
    set->data = std::move(data);
    return set;
}

#ifdef _WIN32
namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

Handle DenyCoefficientReads(const std::string &coefficient)
{
    const auto locked = CreateFileW(Native(coefficient).c_str(), DELETE, FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "Cannot gate binary coefficient reads");
    Handle gate(locked, &CloseHandle);
    const auto forbidden = CreateFileW(Native(coefficient).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto denied = GetLastError();
    const Handle rejected(forbidden == INVALID_HANDLE_VALUE ? nullptr : forbidden, &CloseHandle);
    Require(!rejected && denied == ERROR_SHARING_VIOLATION,
        "The no-read proof must actually deny new coefficient data opens");
    return gate;
}
}  // 匿名命名空间
#endif

void CheckHotSelectionWithoutReads(
    VnaServer::CalibrationStore &store, const std::string &directory, const CalibrationSetPtr &changed)
{
#ifdef _WIN32
    const auto other = Set(8U, false);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Save(2U, {changed, other}, 7U, error) && store.Load(2U, sets, selected, error), error);
    const auto rows = VnaWeb::ParseJson(Read(directory + "/channel-2.json")).Find("sets")->array;
    std::vector<Handle> gates;
    for (const auto &row : rows) {
        gates.push_back(DenyCoefficientReads(directory + "/" + row.Find("file")->string));
    }
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        selected = attempt % 2U == 0U ? 8U : 7U;
        Require(store.Save(2U, sets, selected, error),
            "Hot selection persistence must succeed while all coefficient reads are denied: " + error);
        const auto committed = VnaWeb::ParseJson(Read(directory + "/channel-2.json"));
        Require(committed.Find("selectedId")->string == std::to_string(selected),
            "A gated selection change must still publish the actual durable selection");
    }
#else
    static_cast<void>(store);
    static_cast<void>(directory);
    static_cast<void>(changed);
#endif
}
}  // 命名空间 VnaServerTest
