/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "calibration_cache.h"
#include "calibration_store.h"
#include "vna_service/calibration_workspace.h"
#include "vna_service/instrument_profile.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace {
using VnaServer::CalibrationStore;
using VnaService::CalibrationSetPtr;
using VnaWeb::Json;

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

void RoundtripAndIncremental(const std::string &directory)
{
    const std::uint64_t largeId = 9007199254740993ULL;
    std::string error;
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> restored;
    std::uint64_t selected = 99U;
    Require(store.Load(1U, restored, selected, error) && restored.empty() && selected == 0U, error);
    const std::vector<CalibrationSetPtr> sets{Set(1U, false), Set(largeId, true)};
    Require(store.Save(1U, sets, largeId, error), error);
    const auto original = VnaWeb::ParseJson(Read(directory + "/channel-1.json"));
    CalibrationStore restarted(directory);
    Require(restarted.Load(1U, restored, selected, error), error);
    Require(
        restored.size() == 2U && selected == largeId && restored[1U]->id == largeId &&
            restored[1U]->name == sets[1U]->name && restored[1U]->data->fourPortTerms.size() == 2U &&
            restored[1U]->data->fourPortTerms[1U][15U].tracking == sets[1U]->data->fourPortTerms[1U][15U].tracking &&
            restored[0U]->data->onePortTerms.front().reflectionTracking ==
                sets[0U]->data->onePortTerms.front().reflectionTracking,
        "Restart must preserve exact uint64 ids, opaque names and actual schema 2/3 coefficients");
    // 模拟 workspace 恢复时深冻结；结构相同但 shared_ptr 不同也复用已有文件。
    auto frozen = std::make_shared<VnaService::CalibrationSet>(*restored[1U]);
    frozen->data = std::make_shared<const VnaCalibration::CalibrationData>(*frozen->data);
    restored[1U] = frozen;
    Require(restarted.Save(1U, restored, 1U, error), error);
    const auto updated = VnaWeb::ParseJson(Read(directory + "/channel-1.json"));
    Require(
        original.Find("sets")->array[0U].Find("file")->string == updated.Find("sets")->array[0U].Find("file")->string &&
            original.Find("sets")->array[1U].Find("file")->string ==
                updated.Find("sets")->array[1U].Find("file")->string,
        "Selection and unchanged restored coefficients must not rewrite every pool file");
    Require(!store.Save(1U, sets, largeId, error) && error.find("changed outside") != std::string::npos,
        "Another store's committed manifest must reject stale overwrite");
    Require(restarted.Save(2U, sets, 1U, error), error);
    Require(restarted.Load(1U, restored, selected, error) && selected == 1U,
        "Another channel must not delete committed files");
}

void FirstCandidateCrashKeepsEmptyBaseline(const std::string &directory)
{
    CalibrationStore store(directory);
    std::string error;
    auto invalid = std::make_shared<VnaService::CalibrationSet>(*Set(1U, false));
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*invalid->data);
    data->onePortTerms.front().directivity = {(std::numeric_limits<double>::quiet_NaN)(), 0.0};
    invalid->data = data;
    Require(!store.Save(3U, {invalid}, 1U, error) && !error.empty(),
        "A failed first candidate must not report its empty baseline as a committed candidate");
    const auto path = directory + "/channel-3.json";
    const auto baseline = VnaWeb::ParseJson(Read(path));
    Require(baseline.Find("sets")->array.empty() && baseline.Find("selectedId")->string == "0",
        "The first failed candidate must retain a durable empty baseline");
    const auto orphan = directory + "/channel-3-set-1-1-1-1.json";
    Write(orphan, "uncommitted candidate left before manifest publication");
    CalibrationStore restarted(directory);
    std::vector<CalibrationSetPtr> sets{Set(999U, false)};
    std::uint64_t selected = 999U;
    Require(restarted.Load(3U, sets, selected, error) && sets.empty() && selected == 0U, error);
    Require(!std::ifstream(Native(orphan).c_str(), std::ios::binary),
        "Restart must discard the unpublished candidate while preserving the empty baseline");
    Require(store.Save(3U, {Set(1U, false)}, 1U, error),
        "The original store must retry against its updated baseline fingerprint: " + error);
    Require(restarted.Load(3U, sets, selected, error) && sets.size() == 1U && selected == 1U, error);
    const auto committed = Read(path);
    Remove(path);
    Require(!restarted.Load(3U, sets, selected, error) && sets.size() == 1U && selected == 1U,
        "Deleting a committed manifest must still fail closed without replacing caller state");
    Write(path, committed);
}

void FailedWriteKeepsManifest(const std::string &directory)
{
    CalibrationStore store(directory);
    std::string error;
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    Require(store.Load(1U, sets, selected, error), error);
    const auto before = Read(directory + "/channel-1.json");
    sets.push_back(Set(3U, false));
    Rename(directory, directory + "-saved");
    Write(directory, "not a directory");
    Require(!store.Save(1U, sets, 3U, error) && !error.empty(), "Write failure must be explicit");
    Remove(directory);
    Rename(directory + "-saved", directory);
    Require(
        Read(directory + "/channel-1.json") == before, "Failed Save must leave old manifest byte-for-byte unchanged");
    Require(store.Save(1U, sets, 3U, error), error);
    CalibrationStore restarted(directory);
    Require(restarted.Load(1U, sets, selected, error) && sets.size() == 3U && selected == 3U,
        "Retry after a failed save must publish the correct new generation");
}

void CorruptionIsNotAnEmptyPool(const std::string &directory)
{
    const auto manifest = directory + "/channel-1.json";
    const auto text = Read(manifest);
    CalibrationStore store(directory);
    std::string error;
    std::vector<CalibrationSetPtr> sets{Set(999U, false)};
    std::uint64_t selected = 999U;
    Write(manifest, "{broken");
    Require(!store.Load(1U, sets, selected, error) && sets.front()->id == 999U && selected == 999U,
        "Corrupt manifest must fail without replacing caller state");
    Write(manifest, text);
    auto document = VnaWeb::ParseJson(text);
    document["sets"].array[0U]["file"] = Json("../outside.json");
    Write(manifest, VnaWeb::StringifyJson(document));
    Require(!store.Load(1U, sets, selected, error), "Manifest path traversal must be rejected");
    Write(manifest, text);
    document = VnaWeb::ParseJson(text);
    const auto file = directory + "/" + document.Find("sets")->array[0U].Find("file")->string;
    const auto contents = Read(file);
    Write(file, contents + " ");
    Require(!store.Load(1U, sets, selected, error), "Checksum must detect even parseable file damage");
    Write(file, contents);
    Remove(manifest);
    Require(!store.Load(1U, sets, selected, error) && !error.empty(),
        "Missing manifest with existing sets is not an empty pool");
    Write(manifest, text);
    Remove(file);
    Require(!store.Load(1U, sets, selected, error), "A missing referenced coefficient file must fail startup");
    Write(file, contents);
    Require(store.Load(1U, sets, selected, error) && selected == 3U, error);
    std::vector<CalibrationSetPtr> overflow;
    for (std::size_t index = 0U; index <= VnaService::MAX_CALIBRATION_SETS; ++index) {
        overflow.push_back(Set(index + 1U, false));
    }
    Require(!store.Save(1U, overflow, 1U, error), "Storage set count must be bounded");
    auto oversized = std::make_shared<VnaService::CalibrationSet>(*sets.front());
    auto oversizedData = std::make_shared<VnaCalibration::CalibrationData>(*oversized->data);
    oversizedData->frequencyHz.resize(VnaService::MAX_SWEEP_POINTS + 1U, 1e9);
    oversizedData->onePortTerms.resize(VnaService::MAX_SWEEP_POINTS + 1U);
    oversized->powerDbm.resize(VnaService::MAX_SWEEP_POINTS + 1U, 0.0);
    oversized->data = oversizedData;
    Require(!store.Save(1U, {oversized}, oversized->id, error),
        "Invalid oversized typed input must reject before allocating its JSON tree");
    Require(Read(manifest) == text, "Rejected oversized pool must not modify the manifest");
}

void FailedManifestPublishKeepsOldPool(const std::string &directory)
{
#ifdef _WIN32
    CalibrationStore store(directory);
    std::string error;
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    Require(store.Load(1U, sets, selected, error), error);
    const auto manifest = directory + "/channel-1.json";
    const auto before = Read(manifest);
    const auto oldCoefficient = sets[0U]->data->onePortTerms.front().directivity;
    auto changed = std::make_shared<VnaService::CalibrationSet>(*sets[0U]);
    auto coefficients = std::make_shared<VnaCalibration::CalibrationData>(*changed->data);
    coefficients->onePortTerms.front().directivity += VnaCalibration::Complex{0.125, -0.03125};
    changed->data = coefficients;
    sets[0U] = changed;
    // 允许读写但不共享 DELETE，确保新集及临时 manifest 可写，最终替换才失败。
    const auto handle = CreateFileW(Native(manifest).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(handle != INVALID_HANDLE_VALUE, "Cannot hold the manifest publication fault gate");
    const bool saved = store.Save(1U, sets, selected, error);
    CloseHandle(handle);
    Require(!saved && error.find("replace calibration manifest") != std::string::npos && Read(manifest) == before,
        "Publish failure after writing candidates must preserve the old manifest");
    CalibrationStore restarted(directory);
    std::vector<CalibrationSetPtr> old;
    std::uint64_t restoredSelection = 0U;
    Require(restarted.Load(1U, old, restoredSelection, error) && restoredSelection == selected &&
                old[0U]->data->onePortTerms.front().directivity == oldCoefficient,
        "An uncommitted candidate must never be recovered as the current coefficient set");
    WIN32_FIND_DATAW entry{};
    const auto search = FindFirstFileW(Native(directory + "/channel-1-set-*.json").c_str(), &entry);
    Require(search != INVALID_HANDLE_VALUE, "Committed set files are missing");
    std::size_t files = 0U;
    do {
        ++files;
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    Require(files == old.size(), "Failed manifest publication left an adopted or unbounded candidate generation");
    Require(store.Save(1U, sets, selected, error), "Retry after closing the publication gate failed: " + error);
    Require(restarted.Load(1U, old, restoredSelection, error) &&
                old[0U]->data->onePortTerms.front().directivity == coefficients->onePortTerms.front().directivity,
        "Same-id retry must persist the actual new non-unit coefficients");
#else
    static_cast<void>(directory);
#endif
}

std::string CachePath(const std::string &directory, const std::string &file)
{
    return directory + "/compact-cache/" + file.substr(0U, file.size() - 5U) + ".bin";
}

void CompactCacheFallbackAndAuthority(const std::string &directory)
{
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Load(1U, sets, selected, error), error);
    const auto manifest = Read(directory + "/channel-1.json");
    const auto rows = VnaWeb::ParseJson(manifest).Find("sets")->array;
    const auto file = rows.front().Find("file")->string;
    const auto path = CachePath(directory, file);
    const auto valid = Read(path);
    const auto source = Read(directory + "/" + file);
    const auto expected = sets.front()->data->onePortTerms.front().directivity;
    std::string version = valid;
    version[8U] ^= 1;
    std::string binding = valid;
    binding[24U] ^= 1;
    std::string checksum = valid;
    checksum.back() ^= 1;
    for (const auto &bad : {version, binding, checksum, valid.substr(0U, valid.size() / 2U), valid + "x"}) {
        Write(path, bad);
        CalibrationStore restarted(directory);
        Require(restarted.Load(1U, sets, selected, error) && error.empty() && Read(path) == valid &&
                    sets.front()->data->onePortTerms.front().directivity == expected,
            "invalid or obsolete caches must rebuild losslessly from the original JSON without failing restore");
    }
    Remove(path);
    Require(store.Load(1U, sets, selected, error) && Read(path) == valid, "missing cache must rebuild at startup");
    Remove(path);
    Require(store.Save(1U, sets, selected, error) && !std::ifstream(Native(path).c_str(), std::ios::binary) &&
                Read(directory + "/channel-1.json") == manifest && Read(directory + "/" + file) == source,
        "selection-only Save must neither read nor rebuild even a missing cache or rewrite source coefficients");
    CalibrationStore cold(directory);
    Require(cold.Save(1U, sets, selected, error) && error.empty() &&
                !std::ifstream(Native(path).c_str(), std::ios::binary) &&
                Read(directory + "/channel-1.json") == manifest && Read(directory + "/" + file) == source,
        "selection-only cold Save must not rebuild derived cache before the authoritative commit");
    Require(store.Load(1U, sets, selected, error), error);
    const auto retained = sets.front();
    const auto retainedSelection = selected;
    std::string damaged = source;
    damaged[damaged.size() / 2U] ^= 1;
    Write(directory + "/" + file, damaged);
    Require(!store.Load(1U, sets, selected, error) && sets.front() == retained && selected == retainedSelection &&
                Read(path) == valid,
        "a valid cache cannot conceal same-length damage to the authoritative JSON or mutate caller outputs");
    Write(directory + "/" + file, source);
    Remove(directory + "/" + file);
    Require(!store.Load(1U, sets, selected, error) && sets.front() == retained && selected == retainedSelection,
        "a valid cache cannot replace a missing authoritative JSON");
    Write(directory + "/" + file, source);
    Write(directory + "/compact-cache/channel-1-cache-1-1-1.tmp", "abandoned local temporary");
    const auto foreign = directory + "/compact-cache/channel-2-cache-1-1-1.tmp";
    Write(foreign, "another channel may still be writing");
    Require(store.Load(1U, sets, selected, error) && Read(foreign) == "another channel may still be writing" &&
                !std::ifstream(Native(directory + "/compact-cache/channel-1-cache-1-1-1.tmp").c_str()),
        "cache cleanup must remove only this channel's temporaries");
    Remove(foreign);
}

void CacheWriteFailureAndGenerationChanges(const std::string &directory)
{
    const auto folder = directory + "/compact-cache";
    Rename(folder, folder + "-saved");
    Write(folder, "cache unavailable; authoritative directory remains writable");
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Load(2U, sets, selected, error) && error.empty(),
        "unreadable/unwritable cache directory must not fail a valid JSON restore");
    const auto replacement = Set(7U, true);
    Require(store.Save(2U, {replacement}, 7U, error) && error.empty(),
        "cache creation failure must not fail an authoritative new-generation commit");
    Require(store.Load(2U, sets, selected, error) && selected == 7U && sets.size() == 1U, error);
    Remove(folder);
    Rename(folder + "-saved", folder);
    Require(store.Load(2U, sets, selected, error), error);
    const auto before = VnaWeb::ParseJson(Read(directory + "/channel-2.json"));
    const auto oldFile = before.Find("sets")->array.front().Find("file")->string;
    auto changed = std::make_shared<VnaService::CalibrationSet>(*replacement);
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*changed->data);
    data->fourPortTerms.front()[15U].tracking += VnaCalibration::Complex{0.125, -0.03125};
    changed->data = data;
    Require(store.Save(2U, {changed}, 7U, error), error);
    const auto updated = VnaWeb::ParseJson(Read(directory + "/channel-2.json"));
    const auto newFile = updated.Find("sets")->array.front().Find("file")->string;
    Require(newFile != oldFile && !std::ifstream(Native(CachePath(directory, oldFile)).c_str()) &&
                std::ifstream(Native(CachePath(directory, newFile)).c_str()),
        "same-id coefficient replacement must create a matching new cache and retire the old generation");
    Require(store.Load(2U, sets, selected, error) &&
                sets.front()->data->fourPortTerms.front()[15U].tracking == data->fourPortTerms.front()[15U].tracking,
        "new-generation cache must retain the changed actual coefficients");
    Require(store.Save(2U, {}, 0U, error) && !std::ifstream(Native(CachePath(directory, newFile)).c_str()),
        "deleting a set must retire its derived cache");
    Require(store.Save(2U, {replacement}, 7U, error), error);
}

void CachedDataStillRequiresSemanticValidation(const std::string &directory)
{
    CalibrationStore store(directory);
    auto invalid = std::make_shared<VnaService::CalibrationSet>(*Set(1U, false));
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*invalid->data);
    data->onePortTerms.front().reflectionTracking = {};
    invalid->data = data;
    std::string error;
    // Store accepts structurally valid data; the service owns profile and correction validation.
    Require(store.Save(3U, {invalid}, 1U, error), error);
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        CalibrationStore restarted(directory);
        std::vector<CalibrationSetPtr> restored;
        std::uint64_t selected = 0U;
        Require(restarted.Load(3U, restored, selected, error), error);
        VnaService::CalibrationWorkspace workspace;
        VnaService::InstrumentProfile profile;
        profile.maxFrequencyHz = 40000000000.0;
        Require(!workspace.RestoreSaved(restored, selected, profile, error) && workspace.SavedSets().empty(),
            "even a valid matching cache must not bypass the original full coefficient validation at restore");
    }
    Require(store.Save(3U, {Set(1U, false)}, 1U, error), error);
}

void Cleanup(const std::string &directory)
{
    for (unsigned channel : {1U, 2U, 3U}) {
        const auto base = directory + "/channel-" + std::to_string(channel);
        const auto manifest = VnaWeb::ParseJson(Read(base + ".json"));
        for (const auto &entry : manifest.Find("sets")->array) {
            const auto file = entry.Find("file")->string;
            Remove(directory + "/" + file);
            Remove(directory + "/compact-cache/" + file.substr(0U, file.size() - 5U) + ".bin");
        }
        Remove(base + ".json");
        Remove(base + ".lock");
    }
#ifdef _WIN32
    Require(RemoveDirectoryW(Native(directory + "/compact-cache").c_str()) != 0, "Cannot remove cache directory");
    Require(RemoveDirectoryW(Native(directory).c_str()) != 0, "Cannot remove test directory");
#else
    Require(rmdir((directory + "/compact-cache").c_str()) == 0, "Cannot remove cache directory");
    Require(rmdir(directory.c_str()) == 0, "Cannot remove test directory");
#endif
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string directory = std::string(argv[1]) + "/校准库-" + std::to_string(suffix);
    try {
        RoundtripAndIncremental(directory);
        FirstCandidateCrashKeepsEmptyBaseline(directory);
        FailedWriteKeepsManifest(directory);
        FailedManifestPublishKeepsOldPool(directory);
        CorruptionIsNotAnEmptyPool(directory);
        CompactCacheFallbackAndAuthority(directory);
        CacheWriteFailureAndGenerationChanges(directory);
        CachedDataStillRequiresSemanticValidation(directory);
        Cleanup(directory);
        std::cout << "PASS calibration store restart, immutable files, conflicts, atomic failures and corruption\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << " (test directory: " << directory << ")\n";
        return 1;
    }
}
