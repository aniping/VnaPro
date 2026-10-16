/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include <chrono>
#include <fstream>
#include <iostream>
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
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "calibration_store.h"
#include "calibration_store_test_support.h"
#include "vna_service/calibration_workspace.h"
#include "vna_service/instrument_profile.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace {
using VnaServer::CalibrationStore;
using VnaService::CalibrationSetPtr;
using VnaWeb::Json;

using VnaServerTest::CheckHotSelectionWithoutReads;
using VnaServerTest::MakeDirectory;
using VnaServerTest::Native;
using VnaServerTest::Read;
using VnaServerTest::Remove;
using VnaServerTest::RemoveTestDirectory;
using VnaServerTest::Rename;
using VnaServerTest::Require;
using VnaServerTest::Set;
using VnaServerTest::Write;

void CheckBinaryAuthority(const std::string &directory, const Json &manifest)
{
    Require(manifest.Find("version")->number == 2.0, "New pools must publish version 2 binary authority by default");
    for (const auto &row : manifest.Find("sets")->array) {
        const auto file = row.Find("file")->string;
        Require(file.substr(file.size() - 5U) == ".vcal" && Read(directory + "/" + file).substr(0U, 8U) == "VNBCAL02",
            "Every new coefficient file must be self-contained binary, without JSON authority");
    }
}

void CheckRestoredContents(
    const std::vector<CalibrationSetPtr> &restored, const std::vector<CalibrationSetPtr> &sets, std::uint64_t selected)
{
    const std::uint64_t largeId = 9007199254740993ULL;
    Require(
        restored.size() == 2U && selected == largeId && restored[1U]->id == largeId &&
            restored[1U]->name == sets[1U]->name && restored[1U]->data->fourPortTerms.size() == 2U &&
            restored[1U]->data->fourPortTerms[1U][15U].tracking == sets[1U]->data->fourPortTerms[1U][15U].tracking &&
            restored[0U]->data->onePortTerms.front().reflectionTracking ==
                sets[0U]->data->onePortTerms.front().reflectionTracking,
        "Restart must preserve exact uint64 ids, opaque names and actual schema 2/3 coefficients");
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
    CheckBinaryAuthority(directory, original);
    CalibrationStore restarted(directory);
    Require(restarted.Load(1U, restored, selected, error), error);
    CheckRestoredContents(restored, sets, selected);
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

void AdoptValidatedPointers(const std::string &directory)
{
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(!store.AdoptValidated(2U, {}, error), "Pointer adoption must require a loaded pool");
    Require(store.Load(2U, sets, selected, error), error);
    const auto manifest = Read(directory + "/channel-2.json");
    const std::weak_ptr<const VnaService::CalibrationSet> old = sets.front();
    VnaService::CalibrationWorkspace workspace;
    VnaService::InstrumentProfile profile;
    profile.maxFrequencyHz = 40000000000.0;
    Require(workspace.RestoreSaved(sets, selected, profile, error), error);
    for (unsigned field = 0U; field < 3U; ++field) {
        auto candidates = workspace.SavedSets();
        auto changed = std::make_shared<VnaService::CalibrationSet>(*candidates.back());
        if (field == 0U) {
            changed->name += "changed";
        } else if (field == 1U) {
            changed->powerDbm.back() += 0.125;
        } else {
            auto data = std::make_shared<VnaCalibration::CalibrationData>(*changed->data);
            data->fourPortTerms.back()[15U].tracking += VnaCalibration::Complex{0.01, -0.02};
            changed->data = data;
        }
        candidates.back() = changed;
        Require(!store.AdoptValidated(2U, candidates, error) && Read(directory + "/channel-2.json") == manifest,
            "Changed metadata, power or coefficients must never be trusted by pointer adoption");
    }
    sets.clear();
    Require(!old.expired(), "Failed adoption must leave the entire previous pointer pool unchanged");
    Require(store.AdoptValidated(2U, workspace.SavedSets(), error) && old.expired() &&
                Read(directory + "/channel-2.json") == manifest,
        "Validated adoption must release the duplicate pool without rewriting the manifest");
    Require(store.Save(2U, workspace.SavedSets(), selected, error), error);
    Require(Read(directory + "/channel-2.json") == manifest,
        "The first persistence operation after adoption must reuse every binary file");
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
    const auto orphan = directory + "/channel-3-set-1-1-1-1.vcal";
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
    const auto search = FindFirstFileW(Native(directory + "/channel-1-set-*.vcal").c_str(), &entry);
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

void LegacyJsonIsRejectedWithoutChangingFiles(const std::string &directory)
{
    const auto manifest = directory + "/channel-4.json";
    const auto coefficient = directory + "/channel-4-set-1-1-1-1.json";
    const auto cache = directory + "/compact-cache";
    const auto cached = cache + "/channel-4-set-1-1-1-1.bin";
    const std::string previous = R"({"version":1,"channelId":4,"selectedId":"1","sets":[]})";
    MakeDirectory(cache);
    Write(manifest, previous);
    Write(coefficient, "legacy JSON authority must remain untouched");
    Write(cached, "legacy binary cache must remain untouched");
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets{Set(999U, false)};
    std::uint64_t selected = 999U;
    std::string error;
    Require(!store.Load(4U, sets, selected, error) && error.find("Legacy JSON") != std::string::npos &&
                sets.front()->id == 999U && selected == 999U,
        "Legacy JSON must fail explicitly without replacing caller state");
    Require(!store.Save(4U, {Set(1U, false)}, 1U, error) && error.find("Legacy JSON") != std::string::npos &&
                Read(manifest) == previous && Read(coefficient) == "legacy JSON authority must remain untouched" &&
                Read(cached) == "legacy binary cache must remain untouched",
        "A direct save must not migrate, delete or overwrite any unsupported legacy data");
    Remove(manifest);
    Remove(coefficient);
    Remove(cached);
    Remove(directory + "/channel-4.lock");
    RemoveTestDirectory(cache);
}

void BinaryGenerationChanges(const std::string &directory)
{
    CalibrationStore store(directory);
    std::vector<CalibrationSetPtr> sets;
    std::uint64_t selected = 0U;
    std::string error;
    Require(store.Load(2U, sets, selected, error) && error.empty(), "Binary authority must restore directly");
    const auto replacement = Set(7U, true);
    Require(store.Save(2U, {replacement}, 7U, error) && error.empty(), "Binary saves must persist the complete set");
    Require(store.Load(2U, sets, selected, error) && selected == 7U && sets.size() == 1U, error);
    const auto before = VnaWeb::ParseJson(Read(directory + "/channel-2.json"));
    const auto oldFile = before.Find("sets")->array.front().Find("file")->string;
    auto changed = std::make_shared<VnaService::CalibrationSet>(*replacement);
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*changed->data);
    data->fourPortTerms.front()[15U].tracking += VnaCalibration::Complex{0.125, -0.03125};
    changed->data = data;
    Require(store.Save(2U, {changed}, 7U, error), error);
    const auto updated = VnaWeb::ParseJson(Read(directory + "/channel-2.json"));
    const auto newFile = updated.Find("sets")->array.front().Find("file")->string;
    Require(newFile != oldFile && !std::ifstream(Native(directory + "/" + oldFile).c_str()) &&
                std::ifstream(Native(directory + "/" + newFile).c_str()),
        "Same-ID replacement must publish a new immutable binary generation and retire the old one");
    Require(store.Load(2U, sets, selected, error) &&
                sets.front()->data->fourPortTerms.front()[15U].tracking == data->fourPortTerms.front()[15U].tracking,
        "Binary restart must retain actual changed coefficients");
    CheckHotSelectionWithoutReads(store, directory, changed);
    Require(store.Save(2U, {}, 0U, error) && !std::ifstream(Native(directory + "/" + newFile).c_str()),
        "Deleting a set must retire its binary file");
    Require(store.Save(2U, {replacement}, 7U, error), error);
}

void BinaryStillRequiresSemanticValidation(const std::string &directory)
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
            "even intact binary authority must not bypass full coefficient validation at restore");
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
        }
        Remove(base + ".json");
        Remove(base + ".lock");
    }
    RemoveTestDirectory(directory);
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
        AdoptValidatedPointers(directory);
        FirstCandidateCrashKeepsEmptyBaseline(directory);
        FailedWriteKeepsManifest(directory);
        FailedManifestPublishKeepsOldPool(directory);
        CorruptionIsNotAnEmptyPool(directory);
        LegacyJsonIsRejectedWithoutChangingFiles(directory);
        BinaryGenerationChanges(directory);
        BinaryStillRequiresSemanticValidation(directory);
        Cleanup(directory);
        std::cout << "PASS calibration store restart, immutable files, conflicts, atomic failures and corruption\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << " (test directory: " << directory << ")\n";
        return 1;
    }
}
