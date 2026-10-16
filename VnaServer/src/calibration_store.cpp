/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_store.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "calibration_store_data.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "vna_log/logger.h"
#include "web/calibration_binary.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace VnaServer {
namespace {
using CalibrationStoreData::CheckPool;
using CalibrationStoreData::Same;
using VnaWeb::Json;
constexpr std::size_t SET_LIMIT = VnaService::MAX_CALIBRATION_SETS;
constexpr std::size_t COEFFICIENT_BYTE_LIMIT = VnaService::MAX_CALIBRATION_STORAGE_BYTES;
constexpr std::size_t DISK_BYTE_LIMIT = COEFFICIENT_BYTE_LIMIT * 4U;
constexpr std::size_t MANIFEST_BYTE_LIMIT = 64U * 1024U;
constexpr unsigned MANIFEST_NODE_LIMIT = 8U * SET_LIMIT + 32U;

[[noreturn]] void IoError(const std::string &action, const std::string &path)
{
    throw std::runtime_error(action + ": " + path + " (errno=" + std::to_string(errno) + ")");
}

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    VnaWeb::ValidateJsonString(path);
    if (path.empty() || path.find('\0') != std::string::npos || path.size() > 32760U) {
        throw std::invalid_argument("Invalid calibration store path");
    }
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (count == 0) {
        throw std::invalid_argument("Calibration store path must be UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), &result[0], count) != count) {
        throw std::runtime_error("Cannot convert calibration store path");
    }
    return result;
}

std::string Utf8(const wchar_t *text)
{
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) {
        throw std::runtime_error("Cannot decode calibration store filename");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, &result[0], count, nullptr, nullptr) != count) {
        throw std::runtime_error("Cannot decode calibration store filename");
    }
    result.pop_back();
    return result;
}
#endif

bool Directory(const std::string &path)
{
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(WindowsPath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
    struct stat info {};

    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool FileSizeMatches(const std::string &path, std::size_t bytes)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info{};
    return GetFileAttributesExW(WindowsPath(path).c_str(), GetFileExInfoStandard, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
           ((static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow) == bytes;
#else
    struct stat info {};

    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
           static_cast<std::uint64_t>(info.st_size) == bytes;
#endif
}

void EnsureDirectory(const std::string &path)
{
    if (Directory(path)) {
        return;
    }
    const auto separator = path.find_last_of("/\\");
    if (separator != std::string::npos && separator != 0U) {
        const auto parent = path.substr(0U, separator);
        if (!(parent.size() == 2U && parent[1U] == ':')) {
            EnsureDirectory(parent);
        }
    }
#ifdef _WIN32
    if (!CreateDirectoryW(WindowsPath(path).c_str(), nullptr) && !Directory(path)) {
        throw std::runtime_error(
            "Cannot create calibration directory: " + path + " (Win32=" + std::to_string(GetLastError()) + ")");
    }
#else
    if (mkdir(path.c_str(), 0700) != 0 && !Directory(path)) {
        IoError("Cannot create calibration directory", path);
    }
#endif
}

class StoreLock final {
public:
    explicit StoreLock(const std::string &path)
    {
#ifdef _WIN32
        handle_ = CreateFileW(WindowsPath(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error(
                "Calibration store is busy or unwritable: " + path + " (Win32=" + std::to_string(GetLastError()) + ")");
        }
#else
        descriptor_ = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (descriptor_ < 0) {
            IoError("Cannot lock calibration store", path);
        }
        if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            close(descriptor_);
            IoError("Calibration store is busy", path);
        }
#endif
    }

    ~StoreLock()
    {
#ifdef _WIN32
        CloseHandle(handle_);
#else
        close(descriptor_);
#endif
    }

    StoreLock(const StoreLock &) = delete;
    StoreLock &operator=(const StoreLock &) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

std::string Prefix(unsigned channelId)
{
    if (channelId == 0U) {
        throw std::invalid_argument("Calibration store channel must be positive");
    }
    return "channel-" + std::to_string(channelId);
}

std::string Unique()
{
    static std::atomic<std::uint64_t> sequence{0U};
    const auto time = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    return std::to_string(process) + "-" + std::to_string(time) + "-" + std::to_string(++sequence);
}

bool ReadFile(const std::string &path, std::size_t limit, std::string &text)
{
#ifdef _WIN32
    std::FILE *file = _wfopen(WindowsPath(path).c_str(), L"rb");
#else
    std::FILE *file = std::fopen(path.c_str(), "rb");
#endif
    if (file == nullptr) {
        if (errno == ENOENT) {
            return false;
        }
        IoError("Cannot read calibration store file", path);
    }
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> owned(file, &std::fclose);
    std::array<char, 16384U> buffer{};
    text.clear();
    for (;;) {
        const auto count = std::fread(buffer.data(), 1U, buffer.size(), file);
        if (count > limit - text.size()) {
            throw std::runtime_error("Calibration store file exceeds its byte budget: " + path);
        }
        text.append(buffer.data(), count);
        if (count != buffer.size()) {
            if (std::ferror(file)) {
                IoError("Cannot finish reading calibration store file", path);
            }
            break;
        }
    }
    return true;
}

void Remove(const std::string &path)
{
#ifdef _WIN32
    if (!DeleteFileW(WindowsPath(path).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        throw std::runtime_error(
            "Cannot remove orphan calibration file: " + path + " (Win32=" + std::to_string(GetLastError()) + ")");
    }
#else
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
        IoError("Cannot remove orphan calibration file", path);
    }
#endif
}

void WriteNew(const std::string &path, const std::string &text, bool durable = true)
{
#ifdef _WIN32
    const int descriptor =
        _wopen(WindowsPath(path).c_str(), _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
    std::FILE *file = descriptor < 0 ? nullptr : _fdopen(descriptor, "wb");
#else
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    std::FILE *file = descriptor < 0 ? nullptr : fdopen(descriptor, "wb");
#endif
    if (file == nullptr) {
        if (descriptor >= 0) {
#ifdef _WIN32
            _close(descriptor);
#else
            close(descriptor);
#endif
        }
        IoError("Cannot create immutable calibration file", path);
    }
    std::unique_ptr<std::FILE, decltype(&std::fclose)> owned(file, &std::fclose);
    if (std::fwrite(text.data(), 1U, text.size(), file) != text.size() || std::fflush(file) != 0) {
        IoError("Cannot write calibration file", path);
    }
    if (durable) {
#ifdef _WIN32
        const int flushed = _commit(descriptor);
#else
        const int flushed = fsync(descriptor);
#endif
        if (flushed != 0) {
            IoError("Cannot flush calibration file", path);
        }
    }
    owned.release();
    if (std::fclose(file) != 0) {
        IoError("Cannot close calibration file", path);
    }
}

void Publish(const std::string &temporary, const std::string &path, const std::string &directory, bool &published,
    std::string &warning)
{
#ifdef _WIN32
    static_cast<void>(directory);
    static_cast<void>(warning);
    if (!MoveFileExW(WindowsPath(temporary).c_str(), WindowsPath(path).c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error(
            "Cannot replace calibration manifest: " + path + " (Win32=" + std::to_string(GetLastError()) + ")");
    }
    published = true;
#else
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        IoError("Cannot open calibration directory for synchronization", directory);
    }
    const bool ready = fsync(descriptor) == 0;
    if (!ready || std::rename(temporary.c_str(), path.c_str()) != 0) {
        close(descriptor);
        IoError("Cannot replace calibration manifest", path);
    }
    published = true;
    const int flushed = fsync(descriptor);
    close(descriptor);
    if (flushed != 0) {
        warning = "Calibration manifest committed; directory synchronization failed, durability is uncertain";
    }
#endif
}

std::vector<std::string> Files(const std::string &directory)
{
    std::vector<std::string> files;
#ifdef _WIN32
    WIN32_FIND_DATAW entry{};
    const auto handle = FindFirstFileW(WindowsPath(directory + "/*").c_str(), &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot enumerate calibration directory: " + directory);
    }
    const std::unique_ptr<void, decltype(&FindClose)> owned(handle, &FindClose);
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            files.push_back(Utf8(entry.cFileName));
        }
    } while (FindNextFileW(handle, &entry));
    const auto error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        throw std::runtime_error("Cannot finish enumerating calibration directory: " + directory);
    }
#else
    const std::unique_ptr<DIR, decltype(&closedir)> handle(opendir(directory.c_str()), &closedir);
    if (handle == nullptr) {
        IoError("Cannot enumerate calibration directory", directory);
    }
    for (;;) {
        errno = 0;
        const auto *entry = readdir(handle.get());
        if (entry == nullptr) {
            if (errno != 0) {
                IoError("Cannot finish enumerating calibration directory", directory);
            }
            break;
        }
        files.emplace_back(entry->d_name);
    }
#endif
    return files;
}

bool InternalFile(const std::string &name, const std::string &prefix, const std::string &ending)
{
    if (name.size() <= prefix.size() + ending.size() || name.size() > 160U ||
        name.compare(0U, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - ending.size(), ending.size(), ending) != 0) {
        return false;
    }
    const auto body = name.substr(prefix.size(), name.size() - prefix.size() - ending.size());
    return body.find_first_not_of("0123456789-") == std::string::npos;
}

void Clean(const std::string &directory, unsigned channelId, const std::set<std::string> &keep)
{
    const auto prefix = Prefix(channelId);
    for (const auto &file : Files(directory)) {
        if (keep.count(file) == 0U &&
            (InternalFile(file, prefix + "-set-", ".vcal") || InternalFile(file, prefix + "-manifest-", ".tmp"))) {
            Remove(directory + "/" + file);
        }
    }
}

std::uint64_t Identifier(const Json &value, bool zero = false)
{
    if (value.type != Json::Type::STRING || value.string.empty() ||
        (value.string.size() > 1U && value.string.front() == '0')) {
        throw std::invalid_argument("Calibration store identifier must be a decimal string");
    }
    std::uint64_t result = 0U;
    for (const char digit : value.string) {
        if (digit < '0' || digit > '9' ||
            result > ((std::numeric_limits<std::uint64_t>::max)() - static_cast<unsigned>(digit - '0')) / 10U) {
            throw std::invalid_argument("Calibration store identifier is out of range");
        }
        result = result * 10U + static_cast<unsigned>(digit - '0');
    }
    if (!zero && result == 0U) {
        throw std::invalid_argument("Calibration store identifier must be positive");
    }
    return result;
}

const Json &Field(const Json &value, const std::string &name, Json::Type type)
{
    const auto *field = value.Find(name);
    if (field == nullptr || field->type != type) {
        throw std::invalid_argument("Invalid calibration store field: " + name);
    }
    return *field;
}

std::string Digest(const std::string &text)
{
    // FNV-1a 检测意外损坏，不作为不受信任用户的认证机制。
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) {
        hash = (hash ^ byte) * 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

bool HasCoefficients(const std::string &directory, const std::string &prefix)
{
    for (const auto &file : Files(directory)) {
        if (InternalFile(file, prefix + "-set-", ".json") || InternalFile(file, prefix + "-set-", ".vcal")) {
            return true;
        }
    }
    return false;
}

Json ParseManifest(const std::string &text, unsigned channelId)
{
    auto manifest = VnaWeb::ParseJson(text, MANIFEST_NODE_LIMIT);
    const auto version = Field(manifest, "version", Json::Type::NUMBER).number;
    if (version == 1.0) {
        throw std::invalid_argument("Legacy JSON calibration stores are not supported; original files were preserved");
    }
    if (manifest.type != Json::Type::OBJECT || manifest.object.size() != 4U || version != 2.0 ||
        Field(manifest, "channelId", Json::Type::NUMBER).number != channelId) {
        throw std::invalid_argument("Invalid calibration manifest version or channel");
    }
    if (Field(manifest, "sets", Json::Type::ARRAY).array.size() > SET_LIMIT) {
        throw std::invalid_argument("Calibration manifest exceeds 256 sets");
    }
    return manifest;
}

Json EmptyManifest(unsigned channelId, std::uint64_t selectedId)
{
    Json manifest = Json::MakeObject();
    manifest["version"] = Json(2U);
    manifest["channelId"] = Json(channelId);
    manifest["selectedId"] = Json(std::to_string(selectedId));
    manifest["sets"] = Json::MakeArray();
    return manifest;
}

Json ManifestRow(std::uint64_t id, const std::string &file, const std::string &digest)
{
    Json row = Json::MakeObject();
    row["id"] = Json(std::to_string(id));
    row["file"] = Json(file);
    row["digest"] = Json(digest);
    return row;
}

}  // 匿名命名空间

struct CalibrationStore::SaveState final {
    Entries entries;
    std::set<std::string> committed;
    std::set<std::string> keep;
    std::vector<std::string> created;
    Json rows = Json::MakeArray();
    std::size_t diskBytes = 0U;
    bool changedFiles = false;
    bool cleaned = false;
    bool published = false;
};

CalibrationStore::CalibrationStore(std::string directory) : directory_(std::move(directory))
{
    if (directory_.empty() || directory_.find('\0') != std::string::npos) {
        throw std::invalid_argument("Calibration store directory is empty or contains NUL");
    }
    VnaWeb::ValidateJsonString(directory_);
    while (directory_.size() > 1U && directory_.back() == '/') {
        directory_.pop_back();
    }
}

CalibrationStore::Entry CalibrationStore::ReadEntry(
    const Json &row, const std::string &prefix, std::size_t &diskBytes, std::string &text) const
{
    const auto id = Identifier(Field(row, "id", Json::Type::STRING));
    const auto file = Field(row, "file", Json::Type::STRING).string;
    const auto digest = Field(row, "digest", Json::Type::STRING).string;
    if (row.object.size() != 3U || !InternalFile(file, prefix + "-set-" + std::to_string(id) + "-", ".vcal")) {
        throw std::invalid_argument("Invalid calibration manifest identity or internal filename");
    }
    if (!ReadFile(directory_ + "/" + file, VnaWeb::CalibrationBinary::BYTE_LIMIT, text) || Digest(text) != digest) {
        throw std::runtime_error("Missing or damaged calibration set file: " + directory_ + "/" + file);
    }
    if (text.size() > DISK_BYTE_LIMIT - diskBytes) {
        throw std::runtime_error("Calibration pool exceeds its serialized disk budget");
    }
    diskBytes += text.size();
    const auto set = VnaWeb::CalibrationBinary::Decode(text);
    if (!set || set->id != id) {
        throw std::invalid_argument("Calibration manifest and document identifiers differ");
    }
    return {set, file, digest, text.size()};
}

CalibrationStore::Entries CalibrationStore::ReadRows(const Json &rows, const std::string &prefix,
    std::vector<VnaService::CalibrationSetPtr> &sets, std::string &text) const
{
    Entries entries;
    std::size_t diskBytes = 0U;
    for (const auto &row : rows.array) {
        const auto id = Identifier(Field(row, "id", Json::Type::STRING));
        if (entries.count(id) != 0U) {
            throw std::invalid_argument("Invalid calibration manifest identity or internal filename");
        }
        auto entry = ReadEntry(row, prefix, diskBytes, text);
        sets.push_back(entry.set);
        entries.emplace(id, std::move(entry));
        CheckPool(sets, 0U);
    }
    return entries;
}

CalibrationStore::Entries CalibrationStore::Read(unsigned channelId, std::vector<VnaService::CalibrationSetPtr> &sets,
    std::uint64_t &selectedId, std::string &manifestDigest)
{
    const auto prefix = Prefix(channelId);
    std::string text;
    if (!ReadFile(directory_ + "/" + prefix + ".json", MANIFEST_BYTE_LIMIT, text)) {
        if (HasCoefficients(directory_, prefix)) {
            throw std::runtime_error("Calibration manifest is missing from a nonempty pool: " + directory_);
        }
        selectedId = 0U;
        manifestDigest.clear();
        return {};
    }
    const auto fingerprint = Digest(text);
    const auto manifest = ParseManifest(text, channelId);
    selectedId = Identifier(Field(manifest, "selectedId", Json::Type::STRING), true);
    const auto &rows = Field(manifest, "sets", Json::Type::ARRAY);
    auto entries = ReadRows(rows, prefix, sets, text);
    CheckPool(sets, selectedId);
    manifestDigest = fingerprint;
    VnaLog::Info(VnaLog::Module::SERVER, "二进制校准文件已加载，channel={}，format=VNBCAL02，sets={}", channelId,
        rows.array.size());
    return entries;
}

bool CalibrationStore::Load(
    unsigned channelId, std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t &selectedId, std::string &error)
{
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto prefix = Prefix(channelId);
        EnsureDirectory(directory_);
        StoreLock lock(directory_ + "/" + prefix + ".lock");
        std::vector<VnaService::CalibrationSetPtr> restored;
        std::uint64_t selected = 0U;
        std::string fingerprint;
        auto entries = Read(channelId, restored, selected, fingerprint);
        std::set<std::string> keep;
        for (const auto &entry : entries) {
            keep.insert(entry.second.file);
        }
        Clean(directory_, channelId, keep);
        cache_[channelId] = std::move(entries);
        manifests_[channelId] = std::move(fingerprint);
        sets = std::move(restored);
        selectedId = selected;
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}

bool CalibrationStore::AdoptValidated(
    unsigned channelId, const std::vector<VnaService::CalibrationSetPtr> &sets, std::string &error)
{
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        CheckPool(sets, 0U);
        const auto found = cache_.find(channelId);
        if (found == cache_.end() || found->second.size() != sets.size()) {
            throw std::invalid_argument("Calibration pointer adoption requires the complete loaded binary pool");
        }
        for (const auto &set : sets) {
            const auto previous = found->second.find(set->id);
            if (previous == found->second.end() || !Same(*set, *previous->second.set)) {
                throw std::invalid_argument("Validated calibration contents differ from the loaded pool");
            }
        }
        // 校验完成前不替换任一项；之后 shared_ptr 赋值无分配、无异常，也释放恢复前的系数副本。
        for (const auto &set : sets) {
            found->second.at(set->id).set = set;
        }
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}

void CalibrationStore::EnsureLoaded(unsigned channelId)
{
    if (cache_.count(channelId) != 0U) {
        return;
    }
    std::vector<VnaService::CalibrationSetPtr> previous;
    std::uint64_t selected = 0U;
    std::string fingerprint;
    auto entries = Read(channelId, previous, selected, fingerprint);
    manifests_[channelId] = std::move(fingerprint);
    cache_[channelId] = std::move(entries);
}

void CalibrationStore::EnsureBaseline(unsigned channelId, SaveState &state, std::string &error)
{
    const auto prefix = Prefix(channelId);
    std::string current;
    const bool exists = ReadFile(directory_ + "/" + prefix + ".json", MANIFEST_BYTE_LIMIT, current);
    if ((exists ? Digest(current) : std::string()) != manifests_.at(channelId)) {
        throw std::runtime_error("Calibration manifest changed outside this store; reload before saving");
    }
    if (exists) {
        return;
    }
    const auto text = VnaWeb::StringifyJson(EmptyManifest(channelId, 0U), MANIFEST_BYTE_LIMIT);
    auto fingerprint = Digest(text);
    state.created.push_back(directory_ + "/" + prefix + "-manifest-" + Unique() + ".tmp");
    WriteNew(state.created.back(), text);
    // 首次候选前发布空基线；它不属于候选提交，后续失败仍返回假并保留这个基线。
    bool baselinePublished = false;
    error.clear();
    Publish(state.created.back(), directory_ + "/" + prefix + ".json", directory_, baselinePublished, error);
    manifests_.at(channelId).swap(fingerprint);
    state.created.pop_back();
}

void CalibrationStore::RemoveUncommitted(unsigned channelId, SaveState &state) const
{
    if (!state.cleaned) {
        Clean(directory_, channelId, state.committed);
        state.cleaned = true;
    }
}

CalibrationStore::Entry CalibrationStore::CandidateEntry(
    unsigned channelId, const VnaService::CalibrationSetPtr &set, SaveState &state) const
{
    const auto &previous = cache_.at(channelId);
    const auto found = previous.find(set->id);
    if (found != previous.end() && Same(*set, *found->second.set)) {
        auto entry = found->second;
        if (!FileSizeMatches(directory_ + "/" + entry.file, entry.bytes)) {
            throw std::runtime_error("Cached calibration file is missing or changed: " + entry.file);
        }
        entry.set = set;
        return entry;
    }
    state.changedFiles = true;
    RemoveUncommitted(channelId, state);
    const auto text = VnaWeb::CalibrationBinary::Encode(*set);
    const auto decoded = VnaWeb::CalibrationBinary::Decode(text);
    if (!decoded || !Same(*set, *decoded)) {
        throw std::runtime_error("Binary calibration roundtrip changed its contents");
    }
    Entry entry{set, Prefix(channelId) + "-set-" + std::to_string(set->id) + "-" + Unique() + ".vcal", Digest(text),
        text.size()};
    state.created.push_back(directory_ + "/" + entry.file);
    WriteNew(state.created.back(), text);
    return entry;
}

void CalibrationStore::PrepareCandidate(
    unsigned channelId, const std::vector<VnaService::CalibrationSetPtr> &sets, SaveState &state) const
{
    const auto &previous = cache_.at(channelId);
    for (const auto &entry : previous) {
        state.committed.insert(entry.second.file);
    }
    state.changedFiles = sets.size() != previous.size();
    if (state.changedFiles) {
        RemoveUncommitted(channelId, state);
    }
    for (const auto &set : sets) {
        auto entry = CandidateEntry(channelId, set, state);
        if (entry.bytes > DISK_BYTE_LIMIT - state.diskBytes) {
            throw std::runtime_error("Calibration pool exceeds its serialized disk budget");
        }
        state.diskBytes += entry.bytes;
        state.rows.array.push_back(ManifestRow(set->id, entry.file, entry.digest));
        state.keep.insert(entry.file);
        state.entries.emplace(set->id, std::move(entry));
    }
}

void CalibrationStore::CleanupCommitted(unsigned channelId, const SaveState &state, std::string &error) const
{
    // 提交后清理不能把已成功持久化的操作重新报告失败；下次 Load 重试清理。
    try {
        if (state.changedFiles) {
            Clean(directory_, channelId, state.keep);
        }
    } catch (const std::exception &) {
        if (error.empty()) {
            error = "Calibration manifest committed; orphan cleanup failed and will be retried";
        }
    }
}

void CalibrationStore::CommitCandidate(
    unsigned channelId, std::uint64_t selectedId, SaveState &state, std::string &error)
{
    const auto prefix = Prefix(channelId);
    auto manifest = EmptyManifest(channelId, selectedId);
    manifest["sets"] = std::move(state.rows);
    const auto text = VnaWeb::StringifyJson(manifest, MANIFEST_BYTE_LIMIT);
    auto fingerprint = Digest(text);
    state.created.push_back(directory_ + "/" + prefix + "-manifest-" + Unique() + ".tmp");
    WriteNew(state.created.back(), text);
    error.clear();
    Publish(state.created.back(), directory_ + "/" + prefix + ".json", directory_, state.published, error);
    cache_[channelId] = std::move(state.entries);
    manifests_.at(channelId).swap(fingerprint);
    CleanupCommitted(channelId, state, error);
}

void CalibrationStore::RollbackCandidate(const SaveState &state) const
{
    if (state.published) {
        return;
    }
    for (const auto &file : state.created) {
        try {
            Remove(file);
        } catch (const std::exception &) {
        }
    }
}

bool CalibrationStore::Save(unsigned channelId, const std::vector<VnaService::CalibrationSetPtr> &sets,
    std::uint64_t selectedId, std::string &error)
{
    SaveState state;
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        error.reserve(256U);
        const auto prefix = Prefix(channelId);
        CheckPool(sets, selectedId);
        EnsureDirectory(directory_);
        StoreLock lock(directory_ + "/" + prefix + ".lock");
        EnsureLoaded(channelId);
        EnsureBaseline(channelId, state, error);
        PrepareCandidate(channelId, sets, state);
        CommitCandidate(channelId, selectedId, state, error);
        return true;
    } catch (const std::exception &failure) {
        RollbackCandidate(state);
        error = failure.what();
        return false;
    }
}
}  // 命名空间 VnaServer
