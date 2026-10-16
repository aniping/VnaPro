/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "calibration_store.h"
#include "calibration_store_test_support.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace {
using VnaServer::CalibrationStore;
using VnaServerTest::Native;
using VnaServerTest::Read;
using VnaServerTest::Remove;
using VnaServerTest::RemoveTestDirectory;
using VnaServerTest::Rename;
using VnaServerTest::Require;
using VnaServerTest::Set;
using VnaServerTest::Write;
using VnaService::CalibrationSetPtr;
using VnaWeb::Json;

CalibrationSetPtr NamedSet(std::uint64_t id, const std::string &name)
{
    auto set = std::make_shared<VnaService::CalibrationSet>(*Set(id, true));
    set->name = name;
    return set;
}

Json NamedPool(const std::string &directory, const std::vector<CalibrationSetPtr> &sets)
{
    CalibrationStore store(directory);
    std::string error;
    Require(store.Save(1U, sets, sets.front()->id, error), error);
    auto manifest = VnaWeb::ParseJson(Read(directory + "/channel-1.json"));
    for (std::size_t index = 0U; index < sets.size(); ++index) {
        auto &row = manifest["sets"].array[index];
        const auto named = sets[index]->name + ".vcal";
        Rename(directory + "/" + row["file"].string, directory + "/" + named);
        row["file"] = Json(named);
    }
    Write(directory + "/channel-1.json", VnaWeb::StringifyJson(manifest));
    return manifest;
}

bool Exists(const std::string &path)
{
    return static_cast<bool>(std::ifstream(Native(path).c_str(), std::ios::binary));
}

#ifdef _WIN32
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

Handle BlockReads(const std::string &path)
{
    const auto locked = CreateFileW(Native(path).c_str(), DELETE, FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "Cannot hold the named coefficient read gate");
    Handle gate(locked, &CloseHandle);
    const auto opened = CreateFileW(Native(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto error = GetLastError();
    const Handle rejected(opened == INVALID_HANDLE_VALUE ? nullptr : opened, &CloseHandle);
    Require(!rejected && error == ERROR_SHARING_VIOLATION, "Named coefficient gate must actually deny reads");
    return gate;
}
#endif

void SelectionReusesNamedFiles(const std::string &directory, const Json &original)
{
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Load(1U, sets, selected, error) && sets.size() == 2U && selected == 1U, error);
    Require(sets.front()->name == "S5P1_S11P1-S19P4_S13P1" && sets.back()->data->fourPortTerms[1U][15U].tracking ==
                                                                  Set(2U, true)->data->fourPortTerms[1U][15U].tracking,
        "Named pool restart must preserve its names, ids and actual four-port coefficients");
#ifdef _WIN32
    std::vector<Handle> gates;
    for (const auto &row : original.Find("sets")->array) {
        gates.push_back(BlockReads(directory + "/" + row.Find("file")->string));
    }
#endif
    Require(store.Save(1U, sets, 2U, error), "Named pool selection must not read coefficient files: " + error);
    const auto current = VnaWeb::ParseJson(Read(directory + "/channel-1.json"));
    Require(current.Find("selectedId")->string == "2" &&
                VnaWeb::StringifyJson(*current.Find("sets")) == VnaWeb::StringifyJson(*original.Find("sets")),
        "Selection must retain every named filename and checksum without rewriting coefficients");
}

void RejectsUnsafeAndMismatchedNames(const std::string &directory, const Json &original)
{
    const std::vector<std::string> invalid{"../outside.vcal", "..\\outside.vcal", "C:outside.vcal", "safe.vcal:stream",
        "CON.vcal", "nul.any.vcal", "Lpt1.vcal", "COM\xc2\xb9.vcal", "con .vcal", "one?.vcal", "one*.vcal", "one|.vcal",
        "one<.vcal", "one>.vcal", "one\".vcal", "safe .vcal", "safe..vcal", ".vcal", std::string(129U, 'x') + ".vcal",
        "channel-other.vcal", "CHANNEL-other.vcal", std::string("a\0b.vcal", 8U)};
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets{Set(999U, false)};
    std::uint64_t selected = 999U;
    std::string error;
    for (const auto &file : invalid) {
        auto manifest = original;
        manifest["sets"].array.front()["file"] = Json(file);
        Write(directory + "/channel-1.json", VnaWeb::StringifyJson(manifest));
        Require(!store.Load(1U, sets, selected, error) && error.find("internal filename") != std::string::npos &&
                    sets.front()->id == 999U && selected == 999U,
            "Unsafe names must be rejected before coefficient I/O, preserving caller state: " + file);
    }
    auto manifest = original;
    const auto source = directory + "/" + manifest["sets"].array.front()["file"].string;
    Write(directory + "/renamed.vcal", Read(source));
    manifest["sets"].array.front()["file"] = Json("renamed.vcal");
    Write(directory + "/channel-1.json", VnaWeb::StringifyJson(manifest));
    Require(!store.Load(1U, sets, selected, error) && error.find("document name differ") != std::string::npos,
        "A safe basename must exactly match the decoded calibration name, even with a valid digest");
    Remove(directory + "/renamed.vcal");
    Write(directory + "/channel-1.json", VnaWeb::StringifyJson(original));
}

void CleanupPreservesNamedFiles(const std::string &directory, const Json &original)
{
    const auto first = directory + "/" + original.Find("sets")->array.front().Find("file")->string;
    const auto bytes = Read(first);
    const auto orphan = directory + "/unreferenced-route.vcal";
    const auto internal = directory + "/channel-1-set-99-1-1-1.vcal";
    Write(orphan, "user-owned named file");
    Write(internal, "uncommitted internal generation");
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Load(1U, sets, selected, error) && !Exists(internal) && Exists(orphan),
        "Startup GC must remove internal generations and preserve unrelated named files");
    auto changed = std::make_shared<VnaService::CalibrationSet>(*sets.front());
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*changed->data);
    data->fourPortTerms.front()[15U].tracking += VnaCalibration::Complex{0.125, -0.03125};
    changed->data = data;
    sets.front() = changed;
    Require(store.Save(1U, sets, selected, error) && Read(first) == bytes && Exists(orphan), error);
    const auto updated = VnaWeb::ParseJson(Read(directory + "/channel-1.json"));
    const auto replacement = directory + "/" + updated.Find("sets")->array.front().Find("file")->string;
    Require(replacement != first && Exists(replacement), "Changed named coefficients must use a new internal file");
    Require(store.Save(1U, {}, 0U, error) && !Exists(replacement) && Read(first) == bytes && Exists(orphan),
        "Deleting sets must clean internal generations without deleting externally named files");
}

void MissingManifestRejectsNamedPool(const std::string &directory, const Json &original)
{
    const auto first = directory + "/" + original.Find("sets")->array.front().Find("file")->string;
    const auto bytes = Read(first);
    Remove(directory + "/channel-1.json");
    CalibrationStore missing(directory);
    std::vector<CalibrationSetPtr> sets{Set(999U, false)};
    std::uint64_t selected = 999U;
    std::string error;
    Require(!missing.Load(1U, sets, selected, error) && error.find("manifest is missing") != std::string::npos &&
                sets.front()->id == 999U && selected == 999U,
        "Named coefficient files without a manifest must fail closed and preserve caller state");
    Require(!missing.Save(1U, {}, 0U, error) && !Exists(directory + "/channel-1.json") && Read(first) == bytes,
        "Saving an empty pool must not replace a named pool whose manifest is missing");
    Remove(directory + "/unreferenced-route.vcal");
    for (const auto &row : original.Find("sets")->array) {
        Remove(directory + "/" + row.Find("file")->string);
    }
    Remove(directory + "/channel-1.lock");
    RemoveTestDirectory(directory);
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string directory = std::string(argv[1]) + "/named-pool-" + std::to_string(suffix);
    try {
        const auto original =
            NamedPool(directory, {NamedSet(1U, "S5P1_S11P1-S19P4_S13P1"), NamedSet(2U, "中文校准面")});
        SelectionReusesNamedFiles(directory, original);
        RejectsUnsafeAndMismatchedNames(directory, original);
        CleanupPreservesNamedFiles(directory, original);
        MissingManifestRejectsNamedPool(directory, original);
        std::cout << "PASS named calibration pools, safe basenames, hot selection and bounded internal cleanup\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << " (test directory: " << directory << ")\n";
        return 1;
    }
}
