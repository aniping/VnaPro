/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_cache.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "vna_core/sweep_plan.h"
#include "vna_service/instrument_profile.h"
#include "web/calibration_json.h"
#include "web/json.h"

namespace VnaServer {
namespace CalibrationCache {
namespace {
using Method = VnaCalibration::CalibrationMethod;
constexpr char MAGIC[] = "VNBCAL01";
constexpr std::size_t HEADER_BYTES = 56U;
constexpr std::size_t FIXED_PAYLOAD_BYTES = 48U;
constexpr std::uint64_t NONFINITE_EXPONENT = 0x7ff0000000000000ULL;
static_assert(CHAR_BIT == 8, "Calibration cache requires eight-bit bytes");

void Require(bool condition)
{
    if (!condition) {
        throw std::invalid_argument("Invalid compact calibration cache");
    }
}

std::uint64_t Checksum(const std::string &bytes, std::size_t offset)
{
    // 仅检测缓存意外损坏；不把 FNV 当作认证或来源证明。
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = offset; index < bytes.size(); ++index) {
        hash = (hash ^ static_cast<unsigned char>(bytes[index])) * 1099511628211ULL;
    }
    return hash;
}

void ValidateSource(const std::string &digest, std::size_t bytes)
{
    Require(bytes != 0U && bytes <= VnaWeb::CALIBRATION_JSON_BYTE_LIMIT && digest.size() == 16U &&
            digest.find_first_not_of("0123456789abcdef") == std::string::npos);
}

struct Writer final {
    void Integer(std::uint64_t value, unsigned count)
    {
        for (unsigned index = 0U; index < count; ++index) {
            bytes += static_cast<char>((value >> (index * 8U)) & 0xffU);
        }
    }

    void Number(double value)
    {
        static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
            "Calibration cache requires IEEE 754 binary64");
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        Require((bits & NONFINITE_EXPONENT) != NONFINITE_EXPONENT);
        Integer(bits, 8U);
    }

    std::string bytes;
};

class Reader final {
public:
    explicit Reader(const std::string &bytes) : bytes_(bytes)
    {
    }

    std::uint64_t Integer(unsigned count)
    {
        Require(count <= bytes_.size() - position_);
        std::uint64_t result = 0U;
        for (unsigned index = 0U; index < count; ++index) {
            result |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[position_++])) << (index * 8U);
        }
        return result;
    }

    std::string Text(std::size_t count)
    {
        Require(count <= bytes_.size() - position_);
        auto result = bytes_.substr(position_, count);
        position_ += count;
        return result;
    }

    double Number()
    {
        const auto bits = Integer(8U);
        Require((bits & NONFINITE_EXPONENT) != NONFINITE_EXPONENT);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

private:
    const std::string &bytes_;
    std::size_t position_ = 0U;
};

unsigned Components(Method method)
{
    switch (method) {
        case Method::ONE_PORT:
            return 6U;
        case Method::TWO_PORT:
            return 24U;
        case Method::FOUR_PORT:
            return 96U;
    }
    throw std::invalid_argument("Unknown calibration cache method");
}

Method ReadMethod(std::uint64_t value)
{
    switch (value) {
        case 1U:
            return Method::ONE_PORT;
        case 2U:
            return Method::TWO_PORT;
        case 4U:
            return Method::FOUR_PORT;
        default:
            throw std::invalid_argument("Unknown calibration cache method");
    }
}

void ValidateShape(const VnaService::CalibrationSet &set)
{
    Require(set.data && set.id != 0U && set.id != (std::numeric_limits<std::uint64_t>::max)());
    const auto &data = *set.data;
    const auto points = data.frequencyHz.size();
    const auto components = Components(data.method);
    Require(points >= VnaService::MIN_SWEEP_POINTS && points <= VnaService::MAX_SWEEP_POINTS &&
            set.powerDbm.size() == points && set.name.size() <= 128U && !set.deviceIdentity.empty() &&
            set.deviceIdentity.size() <= 512U && data.port1 >= 1U && data.port1 <= 4U && data.port2 <= 4U &&
            std::isfinite(set.ifBandwidthHz));
    Require(data.onePortTerms.size() == (components == 6U ? points : 0U) &&
            data.twoPortTerms.size() == (components == 24U ? points : 0U) &&
            data.fourPortTerms.size() == (components == 96U ? points : 0U));
    Require((components == 6U && data.port2 == 0U && !set.includeIsolation) ||
            (components == 24U && data.port2 != 0U && data.port1 != data.port2) ||
            (components == 96U && data.port1 == 1U && data.port2 == 4U));
    Require(std::none_of(set.name.begin(), set.name.end(), [](unsigned char character) {
        return character < 32U || character == 127U;
    }));
    VnaWeb::ValidateJsonString(set.name);
    VnaWeb::ValidateJsonString(set.deviceIdentity);
    for (const double power : set.powerDbm) {
        Require(std::isfinite(power) && power >= VnaCore::MIN_POWER_DBM && power <= VnaCore::MAX_POWER_DBM);
    }
}

template <class Data, class Visit>
void VisitTerms(Data &data, Visit visit)
{
    const auto reflection = [&visit](auto &terms) {
        visit(terms.directivity);
        visit(terms.sourceMatch);
        visit(terms.reflectionTracking);
    };
    const auto transmission = [&visit](auto &terms) {
        visit(terms.isolation);
        visit(terms.loadMatch);
        visit(terms.transmissionTracking);
    };
    for (auto &terms : data.onePortTerms) {
        reflection(terms);
    }
    for (auto &terms : data.twoPortTerms) {
        reflection(terms.forwardReflection);
        transmission(terms.reverseTransmission);
        transmission(terms.forwardTransmission);
        reflection(terms.reverseReflection);
    }
    for (auto &row : data.fourPortTerms) {
        for (auto &cell : row) {
            visit(cell.offset);
            visit(cell.match);
            visit(cell.tracking);
        }
    }
}

std::size_t PayloadSize(std::size_t points, unsigned components, std::size_t name, std::size_t identity)
{
    // 调用方已限定 points<=10001、components<=96、两个字符串<=128/512，乘加不会溢出。
    return FIXED_PAYLOAD_BYTES + name + identity + points * (components + 2U) * sizeof(double);
}

Writer Payload(const VnaService::CalibrationSet &set)
{
    const auto &data = *set.data;
    const auto components = Components(data.method);
    Writer writer;
    writer.bytes.reserve(PayloadSize(data.frequencyHz.size(), components, set.name.size(), set.deviceIdentity.size()));
    writer.Integer(set.id, 8U);
    writer.Integer(components == 6U ? 1U : components == 24U ? 2U : 4U, 4U);
    writer.Integer(set.includeIsolation ? 1U : 0U, 4U);
    writer.Integer(data.port1, 4U);
    writer.Integer(data.port2, 4U);
    writer.Integer(data.frequencyHz.size(), 4U);
    writer.Integer(set.name.size(), 4U);
    writer.Integer(set.deviceIdentity.size(), 4U);
    writer.Integer(0U, 4U);
    writer.Number(set.ifBandwidthHz);
    writer.bytes += set.name;
    writer.bytes += set.deviceIdentity;
    for (const double frequency : data.frequencyHz) {
        writer.Number(frequency);
    }
    for (const double power : set.powerDbm) {
        writer.Number(power);
    }
    VisitTerms(data, [&writer](const VnaCalibration::Complex &value) {
        writer.Number(value.real());
        writer.Number(value.imag());
    });
    return writer;
}

std::uint64_t ReadHeader(Reader &reader, const std::string &bytes, const std::string &digest, std::size_t sourceBytes)
{
    Require(reader.Text(8U) == MAGIC && reader.Integer(4U) == 1U && reader.Integer(4U) == 0U &&
            reader.Integer(8U) == sourceBytes && reader.Text(16U) == digest);
    const auto size = reader.Integer(8U);
    const auto checksum = reader.Integer(8U);
    Require(size == bytes.size() - HEADER_BYTES && checksum == Checksum(bytes, HEADER_BYTES));
    return size;
}

VnaService::CalibrationSetPtr ReadPayload(Reader &reader, std::uint64_t payloadSize)
{
    auto set = std::make_shared<VnaService::CalibrationSet>();
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    set->id = reader.Integer(8U);
    data->method = ReadMethod(reader.Integer(4U));
    const auto flags = reader.Integer(4U);
    Require(flags <= 1U);
    set->includeIsolation = flags != 0U;
    data->port1 = static_cast<unsigned>(reader.Integer(4U));
    data->port2 = static_cast<unsigned>(reader.Integer(4U));
    const auto points = static_cast<std::size_t>(reader.Integer(4U));
    const auto name = static_cast<std::size_t>(reader.Integer(4U));
    const auto identity = static_cast<std::size_t>(reader.Integer(4U));
    Require(reader.Integer(4U) == 0U && points >= VnaService::MIN_SWEEP_POINTS &&
            points <= VnaService::MAX_SWEEP_POINTS && name <= 128U && identity != 0U && identity <= 512U);
    Require(payloadSize == PayloadSize(points, Components(data->method), name, identity));
    set->ifBandwidthHz = reader.Number();
    set->name = reader.Text(name);
    set->deviceIdentity = reader.Text(identity);
    data->frequencyHz.reserve(points);
    set->powerDbm.reserve(points);
    for (std::size_t point = 0U; point < points; ++point) {
        data->frequencyHz.push_back(reader.Number());
    }
    for (std::size_t point = 0U; point < points; ++point) {
        set->powerDbm.push_back(reader.Number());
    }
    data->onePortTerms.resize(data->method == Method::ONE_PORT ? points : 0U);
    data->twoPortTerms.resize(data->method == Method::TWO_PORT ? points : 0U);
    data->fourPortTerms.resize(data->method == Method::FOUR_PORT ? points : 0U);
    VisitTerms(*data, [&reader](VnaCalibration::Complex &value) {
        const double real = reader.Number();
        const double imaginary = reader.Number();
        value = {real, imaginary};
    });
    set->data = std::move(data);
    ValidateShape(*set);
    return set;
}
}  // 匿名命名空间

std::string Encode(const VnaService::CalibrationSet &set, const std::string &sourceDigest, std::size_t sourceBytes)
{
    ValidateSource(sourceDigest, sourceBytes);
    ValidateShape(set);
    const auto payload = Payload(set);
    Writer writer;
    writer.bytes.reserve(HEADER_BYTES + payload.bytes.size());
    writer.bytes.append(MAGIC, 8U);
    writer.Integer(1U, 4U);
    writer.Integer(0U, 4U);
    writer.Integer(sourceBytes, 8U);
    writer.bytes += sourceDigest;
    writer.Integer(payload.bytes.size(), 8U);
    writer.Integer(Checksum(payload.bytes, 0U), 8U);
    writer.bytes += payload.bytes;
    Require(writer.bytes.size() <= BYTE_LIMIT);
    return std::move(writer.bytes);
}

bool Decode(const std::string &bytes, const std::string &sourceDigest, std::size_t sourceBytes,
    VnaService::CalibrationSetPtr &set) noexcept
{
    try {
        ValidateSource(sourceDigest, sourceBytes);
        Require(bytes.size() >= HEADER_BYTES + FIXED_PAYLOAD_BYTES && bytes.size() <= BYTE_LIMIT);
        Reader reader(bytes);
        const auto payloadSize = ReadHeader(reader, bytes, sourceDigest, sourceBytes);
        auto decoded = ReadPayload(reader, payloadSize);
        set = std::move(decoded);
        return true;
    } catch (...) {
        return false;
    }
}
}  // 命名空间 CalibrationCache
}  // 命名空间 VnaServer
