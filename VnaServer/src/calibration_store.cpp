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

#include "calibration_cache.h"

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
#include "web/calibration_json.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace VnaServer {
namespace {
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
            (InternalFile(file, prefix + "-set-", ".json") || InternalFile(file, prefix + "-manifest-", ".tmp"))) {
            Remove(directory + "/" + file);
        }
    }
}

std::string CacheFile(const std::string &jsonFile)
{
    return jsonFile.substr(0U, jsonFile.size() - 5U) + ".bin";
}

bool ReadCache(const std::string &directory, const std::string &jsonFile, const std::string &digest,
    std::size_t sourceBytes, VnaService::CalibrationSetPtr &set) noexcept
{
    try {
        std::string bytes;
        return ReadFile(directory + "/compact-cache/" + CacheFile(jsonFile), CalibrationCache::BYTE_LIMIT, bytes) &&
               CalibrationCache::Decode(bytes, digest, sourceBytes, set);
    } catch (const std::exception &) {
        return false;
    }
}

bool WriteCache(const std::string &directory, const std::string &jsonFile, const std::string &digest,
    std::size_t sourceBytes, const VnaService::CalibrationSet &set, std::string &warning) noexcept
{
    std::string temporary;
    try {
        const auto bytes = CalibrationCache::Encode(set, digest, sourceBytes);
        const auto folder = directory + "/compact-cache";
        EnsureDirectory(folder);
        temporary = folder + "/" + jsonFile.substr(0U, jsonFile.find("-set-")) + "-cache-" + Unique() + ".tmp";
        WriteNew(temporary, bytes, false);
        const auto destination = folder + "/" + CacheFile(jsonFile);
#ifdef _WIN32
        if (!MoveFileExW(WindowsPath(temporary).c_str(), WindowsPath(destination).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            throw std::runtime_error("Cannot replace compact calibration cache");
        }
#else
        if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
            IoError("Cannot replace compact calibration cache", destination);
        }
#endif
        return true;
    } catch (const std::exception &failure) {
        try {
            if (warning.empty()) {
                warning = failure.what();
            }
        } catch (...) {
        }
        if (!temporary.empty()) {
            try {
                Remove(temporary);
            } catch (const std::exception &) {
            }
        }
    } catch (...) {
    }
    return false;
}

void CleanCache(const std::string &directory, unsigned channelId, const std::set<std::string> &keep) noexcept
{
    try {
        const auto folder = directory + "/compact-cache";
        if (!Directory(folder)) {
            return;
        }
        const auto prefix = Prefix(channelId);
        for (const auto &file : Files(folder)) {
            const bool temporary = InternalFile(file, prefix + "-cache-", ".tmp");
            const bool orphan = InternalFile(file, prefix + "-set-", ".bin") &&
                                keep.count(file.substr(0U, file.size() - 4U) + ".json") == 0U;
            if (temporary || orphan) {
                Remove(folder + "/" + file);
            }
        }
    } catch (const std::exception &failure) {
        VnaLog::Warn(VnaLog::Module::SERVER, "紧凑校准缓存清理失败，channel={}，error={}", channelId, failure.what());
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

std::size_t Bytes(const VnaService::CalibrationSet &set)
{
    if (!set.data) {
        throw std::invalid_argument("Calibration store set has no coefficients");
    }
    const auto &data = *set.data;
    std::size_t bytes = sizeof(VnaService::CalibrationSet) + sizeof(VnaCalibration::CalibrationData) +
                        sizeof(VnaService::CalibrationSetPtr);
    const auto add = [&bytes](std::size_t count, std::size_t size) {
        if (count > (COEFFICIENT_BYTE_LIMIT - bytes) / size) {
            throw std::invalid_argument("Calibration pool exceeds its coefficient storage budget");
        }
        bytes += count * size;
    };
    add(set.name.size(), sizeof(char));
    add(set.deviceIdentity.size(), sizeof(char));
    add(2U, sizeof(char));
    add(data.frequencyHz.size(), sizeof(double));
    add(set.powerDbm.size(), sizeof(double));
    add(data.onePortTerms.size(), sizeof(VnaCalibration::OnePortTerms));
    add(data.twoPortTerms.size(), sizeof(VnaCalibration::TwoPortTerms));
    add(data.fourPortTerms.size(), sizeof(VnaCalibration::FourPortTerms));
    return bytes;
}

void CheckShape(const VnaService::CalibrationSet &set)
{
    const auto &data = *set.data;
    const auto points = data.frequencyHz.size();
    const bool one = data.method == VnaCalibration::CalibrationMethod::ONE_PORT;
    const bool two = data.method == VnaCalibration::CalibrationMethod::TWO_PORT;
    const bool four = data.method == VnaCalibration::CalibrationMethod::FOUR_PORT;
    if (set.name.size() > 128U || set.deviceIdentity.empty() || set.deviceIdentity.size() > 512U ||
        points < VnaService::MIN_SWEEP_POINTS || points > VnaService::MAX_SWEEP_POINTS ||
        set.powerDbm.size() != points || (!one && !two && !four) || data.onePortTerms.size() != (one ? points : 0U) ||
        data.twoPortTerms.size() != (two ? points : 0U) || data.fourPortTerms.size() != (four ? points : 0U)) {
        throw std::invalid_argument("Calibration set exceeds its string, point or coefficient shape limits");
    }
}

void CheckPool(const std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t selected)
{
    if (sets.size() > SET_LIMIT) {
        throw std::invalid_argument("Calibration pool exceeds its set count budget");
    }
    std::set<std::uint64_t> ids;
    std::size_t bytes = 0U;
    for (const auto &set : sets) {
        if (!set || set->id == 0U || !ids.insert(set->id).second) {
            throw std::invalid_argument("Calibration pool contains a missing or duplicate identifier");
        }
        const auto count = Bytes(*set);
        CheckShape(*set);
        if (count > COEFFICIENT_BYTE_LIMIT - bytes) {
            throw std::invalid_argument("Calibration pool exceeds its coefficient storage budget");
        }
        bytes += count;
    }
    if (selected != 0U && ids.count(selected) == 0U) {
        throw std::invalid_argument("Selected calibration identifier is absent from the pool");
    }
}

bool Same(const VnaCalibration::OnePortTerms &a, const VnaCalibration::OnePortTerms &b)
{
    return a.directivity == b.directivity && a.sourceMatch == b.sourceMatch &&
           a.reflectionTracking == b.reflectionTracking;
}

bool Same(const VnaCalibration::TransmissionTerms &a, const VnaCalibration::TransmissionTerms &b)
{
    return a.isolation == b.isolation && a.loadMatch == b.loadMatch && a.transmissionTracking == b.transmissionTracking;
}

bool Same(const VnaCalibration::TwoPortTerms &a, const VnaCalibration::TwoPortTerms &b)
{
    return Same(a.forwardReflection, b.forwardReflection) && Same(a.reverseReflection, b.reverseReflection) &&
           Same(a.forwardTransmission, b.forwardTransmission) && Same(a.reverseTransmission, b.reverseTransmission);
}

bool Same(const VnaCalibration::FourPortTerms &a, const VnaCalibration::FourPortTerms &b)
{
    for (std::size_t cell = 0U; cell < a.size(); ++cell) {
        if (a[cell].offset != b[cell].offset || a[cell].match != b[cell].match ||
            a[cell].tracking != b[cell].tracking) {
            return false;
        }
    }
    return true;
}

template <class T>
bool Same(const std::vector<T> &a, const std::vector<T> &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const T &left, const T &right) {
        return Same(left, right);
    });
}

bool Same(const VnaService::CalibrationSet &a, const VnaService::CalibrationSet &b)
{
    if (&a == &b) {
        return true;
    }
    if (a.id != b.id || a.name != b.name || a.deviceIdentity != b.deviceIdentity ||
        a.includeIsolation != b.includeIsolation || a.ifBandwidthHz != b.ifBandwidthHz || a.powerDbm != b.powerDbm ||
        !a.data || !b.data) {
        return false;
    }
    const auto &x = *a.data;
    const auto &y = *b.data;
    return a.data == b.data || (x.method == y.method && x.port1 == y.port1 && x.port2 == y.port2 &&
                                   x.frequencyHz == y.frequencyHz && Same(x.onePortTerms, y.onePortTerms) &&
                                   Same(x.twoPortTerms, y.twoPortTerms) && Same(x.fourPortTerms, y.fourPortTerms));
}

VnaService::CalibrationSetPtr Decode(Json &document)
{
    const auto id = Identifier(Field(document, "id", Json::Type::STRING));
    Json body = Json::MakeObject();
    body["action"] = Json("import");
    body["data"] = std::move(document);
    auto decoded = VnaWeb::CalibrationJson::Change(body).importedSet;
    document = std::move(body["data"]);
    auto restored = std::make_shared<VnaService::CalibrationSet>(*decoded);
    restored->id = id;
    return restored;
}
}  // 匿名命名空间

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

CalibrationStore::Entries CalibrationStore::Read(unsigned channelId, std::vector<VnaService::CalibrationSetPtr> &sets,
    std::uint64_t &selectedId, std::string &manifestDigest, bool rebuildCache)
{
    const auto prefix = Prefix(channelId);
    std::string text;
    if (!ReadFile(directory_ + "/" + prefix + ".json", MANIFEST_BYTE_LIMIT, text)) {
        for (const auto &file : Files(directory_)) {
            if (InternalFile(file, prefix + "-set-", ".json")) {
                throw std::runtime_error("Calibration manifest is missing from a nonempty pool: " + directory_);
            }
        }
        selectedId = 0U;
        manifestDigest.clear();
        return {};
    }
    const auto fingerprint = Digest(text);
    const auto manifest = VnaWeb::ParseJson(text, MANIFEST_NODE_LIMIT);
    if (manifest.type != Json::Type::OBJECT || manifest.object.size() != 4U ||
        Field(manifest, "version", Json::Type::NUMBER).number != 1.0 ||
        Field(manifest, "channelId", Json::Type::NUMBER).number != channelId) {
        throw std::invalid_argument("Invalid calibration manifest version or channel");
    }
    selectedId = Identifier(Field(manifest, "selectedId", Json::Type::STRING), true);
    const auto &rows = Field(manifest, "sets", Json::Type::ARRAY).array;
    if (rows.size() > SET_LIMIT) {
        throw std::invalid_argument("Calibration manifest exceeds 256 sets");
    }
    Entries entries;
    std::size_t diskBytes = 0U;
    std::size_t cacheHits = 0U;
    std::size_t rebuilt = 0U;
    std::size_t cacheFailures = 0U;
    std::string cacheWarning;
    for (const auto &row : rows) {
        const auto id = Identifier(Field(row, "id", Json::Type::STRING));
        const auto file = Field(row, "file", Json::Type::STRING).string;
        const auto digest = Field(row, "digest", Json::Type::STRING).string;
        if (row.object.size() != 3U || entries.count(id) != 0U ||
            !InternalFile(file, prefix + "-set-" + std::to_string(id) + "-", ".json")) {
            throw std::invalid_argument("Invalid calibration manifest identity or internal filename");
        }
        if (!ReadFile(directory_ + "/" + file, VnaWeb::CALIBRATION_JSON_BYTE_LIMIT, text) || Digest(text) != digest) {
            throw std::runtime_error("Missing or damaged calibration set file: " + directory_ + "/" + file);
        }
        if (text.size() > DISK_BYTE_LIMIT - diskBytes) {
            throw std::runtime_error("Calibration pool exceeds its serialized disk budget");
        }
        diskBytes += text.size();
        VnaService::CalibrationSetPtr set;
        const bool hit = ReadCache(directory_, file, digest, text.size(), set) && set->id == id;
        if (hit) {
            ++cacheHits;
        } else {
            auto document = VnaWeb::ParseJson(text, VnaWeb::CALIBRATION_JSON_NODE_LIMIT);
            set = Decode(document);
        }
        if (set->id != id) {
            throw std::invalid_argument("Calibration manifest and document identifiers differ");
        }
        entries.emplace(id, Entry{set, file, digest, text.size()});
        sets.push_back(set);
        CheckPool(sets, 0U);
        if (!hit && rebuildCache) {
            if (WriteCache(directory_, file, digest, text.size(), *set, cacheWarning)) {
                ++rebuilt;
            } else {
                ++cacheFailures;
            }
        }
    }
    CheckPool(sets, selectedId);
    manifestDigest = fingerprint;
    VnaLog::Info(VnaLog::Module::SERVER, "紧凑校准缓存，channel={}，hit={}，miss={}，rebuilt={}，writeFailed={}",
        channelId, cacheHits, rows.size() - cacheHits, rebuilt, cacheFailures);
    if (cacheFailures != 0U) {
        VnaLog::Warn(VnaLog::Module::SERVER, "校准 JSON 恢复成功，缓存写入失败不影响恢复，channel={}，error={}",
            channelId, cacheWarning);
    }
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
        CleanCache(directory_, channelId, keep);
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

bool CalibrationStore::Save(unsigned channelId, const std::vector<VnaService::CalibrationSetPtr> &sets,
    std::uint64_t selectedId, std::string &error)
{
    std::vector<std::string> created;
    bool published = false;
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        error.reserve(256U);
        const auto prefix = Prefix(channelId);
        CheckPool(sets, selectedId);
        EnsureDirectory(directory_);
        StoreLock lock(directory_ + "/" + prefix + ".lock");
        if (cache_.count(channelId) == 0U) {
            std::vector<VnaService::CalibrationSetPtr> previous;
            std::uint64_t selected = 0U;
            std::string fingerprint;
            auto entries = Read(channelId, previous, selected, fingerprint, false);
            manifests_[channelId] = std::move(fingerprint);
            cache_[channelId] = std::move(entries);
        }
        std::string current;
        const bool exists = ReadFile(directory_ + "/" + prefix + ".json", MANIFEST_BYTE_LIMIT, current);
        if ((exists ? Digest(current) : std::string()) != manifests_.at(channelId)) {
            throw std::runtime_error("Calibration manifest changed outside this store; reload before saving");
        }
        if (!exists) {
            Json baseline = Json::MakeObject();
            baseline["version"] = Json(1U);
            baseline["channelId"] = Json(channelId);
            baseline["selectedId"] = Json("0");
            baseline["sets"] = Json::MakeArray();
            const auto text = VnaWeb::StringifyJson(baseline, MANIFEST_BYTE_LIMIT);
            auto fingerprint = Digest(text);
            created.push_back(directory_ + "/" + prefix + "-manifest-" + Unique() + ".tmp");
            WriteNew(created.back(), text);
            // 首次候选写入前发布空池；崩溃留下的新集仍能按旧空 manifest 清理。
            // 此提交不是候选提交，后续失败仍返回假，并保留这个空基线。
            bool baselinePublished = false;
            error.clear();
            Publish(created.back(), directory_ + "/" + prefix + ".json", directory_, baselinePublished, error);
            manifests_.at(channelId).swap(fingerprint);
            created.pop_back();
        }
        std::set<std::string> committed;
        for (const auto &entry : cache_.at(channelId)) {
            committed.insert(entry.second.file);
        }
        Clean(directory_, channelId, committed);
        Json manifest = Json::MakeObject();
        manifest["version"] = Json(1U);
        manifest["channelId"] = Json(channelId);
        manifest["selectedId"] = Json(std::to_string(selectedId));
        Json rows = Json::MakeArray();
        Entries entries;
        std::set<std::string> keep;
        std::size_t diskBytes = 0U;
        bool changedFiles = sets.size() != cache_.at(channelId).size();
        std::array<std::uint64_t, SET_LIMIT> changedIds{};
        std::size_t changedCount = 0U;
        std::size_t cacheFailures = 0U;
        std::string cacheWarning;
        for (const auto &set : sets) {
            Entry entry;
            const auto previous = cache_[channelId].find(set->id);
            if (previous != cache_[channelId].end() && Same(*set, *previous->second.set)) {
                entry = previous->second;
                if (!FileSizeMatches(directory_ + "/" + entry.file, entry.bytes)) {
                    throw std::runtime_error("Cached calibration file is missing or changed: " + entry.file);
                }
                entry.set = set;
            } else {
                changedFiles = true;
                auto document = VnaWeb::CalibrationJson::Export(*set);
                static_cast<void>(Decode(document));
                VnaWeb::JsonEncoder encoder(std::move(document), VnaWeb::CALIBRATION_JSON_BYTE_LIMIT);
                while (!encoder.Advance(4096U)) {
                }
                const auto text = encoder.Take();
                entry = {set, prefix + "-set-" + std::to_string(set->id) + "-" + Unique() + ".json", Digest(text),
                    text.size()};
                created.push_back(directory_ + "/" + entry.file);
                WriteNew(created.back(), text);
                changedIds[changedCount++] = set->id;
            }
            if (entry.bytes > DISK_BYTE_LIMIT - diskBytes) {
                throw std::runtime_error("Calibration pool exceeds its serialized disk budget");
            }
            diskBytes += entry.bytes;
            Json row = Json::MakeObject();
            row["id"] = Json(std::to_string(set->id));
            row["file"] = Json(entry.file);
            row["digest"] = Json(entry.digest);
            rows.array.push_back(std::move(row));
            keep.insert(entry.file);
            entries.emplace(set->id, std::move(entry));
        }
        manifest["sets"] = std::move(rows);
        const auto text = VnaWeb::StringifyJson(manifest, MANIFEST_BYTE_LIMIT);
        auto fingerprint = Digest(text);
        created.push_back(directory_ + "/" + prefix + "-manifest-" + Unique() + ".tmp");
        WriteNew(created.back(), text);
        error.clear();
        Publish(created.back(), directory_ + "/" + prefix + ".json", directory_, published, error);
        cache_[channelId] = std::move(entries);
        manifests_.at(channelId).swap(fingerprint);
        // 提交后清理不得把已成功持久化的操作重新报告失败；下次 Load 会重试并明报清理失败。
        try {
            Clean(directory_, channelId, keep);
        } catch (const std::exception &) {
            if (error.empty()) {
                error = "Calibration manifest committed; orphan cleanup failed and will be retried";
            }
        }
        if (changedFiles) {
            CleanCache(directory_, channelId, keep);
        }
        // 派生缓存不得在权威 manifest 发布前占用本次提交所需空间。
        // 已加载实例的热选择保存不读取、生成或枚举缓存文件。
        for (std::size_t index = 0U; index < changedCount; ++index) {
            const auto &entry = cache_.at(channelId).at(changedIds[index]);
            if (!WriteCache(directory_, entry.file, entry.digest, entry.bytes, *entry.set, cacheWarning)) {
                ++cacheFailures;
            }
        }
        if (cacheFailures != 0U) {
            VnaLog::Warn(VnaLog::Module::SERVER, "校准 manifest 已提交，缓存写入失败不影响提交，channel={}，error={}",
                channelId, cacheWarning);
        }
        return true;
    } catch (const std::exception &failure) {
        if (!published) {
            for (const auto &file : created) {
                try {
                    Remove(file);
                } catch (const std::exception &) {
                }
            }
        }
        error = failure.what();
        return false;
    }
}
}  // 命名空间 VnaServer
