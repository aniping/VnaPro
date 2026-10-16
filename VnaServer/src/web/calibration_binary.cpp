/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_binary.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "vna_service/instrument_profile.h"
#include "web/calibration_binary_layout.h"

namespace VnaWeb {
namespace CalibrationBinary {
namespace {
using Method = VnaCalibration::CalibrationMethod;
namespace Layout = CalibrationBinaryDetail;
constexpr char MAGIC[] = "VNBCAL02";
constexpr std::size_t HEADER_BYTES = 32U;
constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
constexpr std::uint64_t NONFINITE_EXPONENT = 0x7ff0000000000000ULL;
static_assert(CHAR_BIT == 8, "Calibration binary requires eight-bit bytes");
static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
    "Calibration binary requires IEEE 754 binary64");
const bool HOST_LITTLE_ENDIAN = [] {
    const std::uint64_t marker = 1U;
    unsigned char first = 0U;
    std::memcpy(&first, &marker, sizeof(first));
    return first == 1U;
}();

void Require(bool condition)
{
    if (!condition) {
        throw std::invalid_argument("Invalid calibration binary document");
    }
}

void Put(std::string &bytes, std::size_t offset, std::uint64_t value, unsigned count)
{
    for (unsigned index = 0U; index < count; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xffU);
    }
}

std::uint64_t Checksum(const std::string &bytes)
{
    // FNV 仅检测意外损坏，不提供来源认证。
    const auto size = bytes.size();
    Require(size >= HEADER_BYTES);
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
    const auto *end = data + size;
    std::uint64_t hash = FNV_OFFSET;
    for (const auto *current = data + HEADER_BYTES; current != end; ++current) {
        hash = (hash ^ *current) * FNV_PRIME;
    }
    return hash;
}

struct Writer final {
    void Integer(std::uint64_t value, unsigned count)
    {
        char encoded[8U];
        for (unsigned index = 0U; index < count; ++index) {
            encoded[index] = static_cast<char>((value >> (index * 8U)) & 0xffU);
        }
        bytes.append(encoded, count);
    }

    void Number(double value)
    {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        Require((bits & NONFINITE_EXPONENT) != NONFINITE_EXPONENT);
        if (HOST_LITTLE_ENDIAN) {
            bytes.append(reinterpret_cast<const char *>(&bits), sizeof(bits));
        } else {
            Integer(bits, 8U);
        }
    }

    std::string bytes;
};

class Reader final {
public:
    explicit Reader(const std::string &bytes)
        : bytes_(bytes), data_(reinterpret_cast<const unsigned char *>(bytes.data())), size_(bytes.size())
    {
    }

    std::uint64_t Integer(unsigned count)
    {
        Require(count <= size_ - position_);
        std::uint64_t value = 0U;
        for (unsigned index = 0U; index < count; ++index) {
            value |= static_cast<std::uint64_t>(data_[position_++]) << (index * 8U);
        }
        return value;
    }

    std::string Text(std::size_t count)
    {
        Require(count <= size_ - position_);
        auto value = bytes_.substr(position_, count);
        position_ += count;
        return value;
    }

    double Number()
    {
        std::uint64_t bits = 0U;
        if (HOST_LITTLE_ENDIAN) {
            Require(sizeof(bits) <= size_ - position_);
            std::memcpy(&bits, data_ + position_, sizeof(bits));
            position_ += sizeof(bits);
        } else {
            bits = Integer(8U);
        }
        Require((bits & NONFINITE_EXPONENT) != NONFINITE_EXPONENT);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

private:
    const std::string &bytes_;
    const unsigned char *data_;
    const std::size_t size_;
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
    throw std::invalid_argument("Unknown calibration binary method");
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
            throw std::invalid_argument("Unknown calibration binary method");
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

void WritePayload(const VnaService::CalibrationSet &set, Writer &writer)
{
    const auto &data = *set.data;
    const auto components = Components(data.method);
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
    for (const auto frequency : data.frequencyHz) {
        writer.Number(frequency);
    }
    for (const auto power : set.powerDbm) {
        writer.Number(power);
    }
    VisitTerms(data, [&writer](const VnaCalibration::Complex &value) {
        writer.Number(value.real());
        writer.Number(value.imag());
    });
}

VnaService::CalibrationSetPtr ReadPayload(Reader &reader, std::size_t size)
{
    auto set = std::make_shared<VnaService::CalibrationSet>();
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    set->id = reader.Integer(8U);
    data->method = ReadMethod(reader.Integer(4U));
    const auto isolation = reader.Integer(4U);
    Require(isolation <= 1U);
    set->includeIsolation = isolation != 0U;
    data->port1 = static_cast<unsigned>(reader.Integer(4U));
    data->port2 = static_cast<unsigned>(reader.Integer(4U));
    const auto points = static_cast<std::size_t>(reader.Integer(4U));
    const auto name = static_cast<std::size_t>(reader.Integer(4U));
    const auto identity = static_cast<std::size_t>(reader.Integer(4U));
    Require(reader.Integer(4U) == 0U && points >= VnaService::MIN_SWEEP_POINTS &&
            points <= VnaService::MAX_SWEEP_POINTS && name <= 128U && identity != 0U && identity <= 512U);
    Require(size == Layout::PayloadSize(points, Components(data->method), name, identity));
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
    Layout::ResizeTerms(*data, points);
    VisitTerms(*data, [&reader](VnaCalibration::Complex &value) {
        const auto real = reader.Number();
        const auto imaginary = reader.Number();
        value = {real, imaginary};
    });
    set->data = std::move(data);
    Layout::ValidateShape(*set);
    return set;
}
}  // 匿名命名空间

std::string Encode(const VnaService::CalibrationSet &set)
{
    Layout::ValidateShape(set);
    const auto payloadSize = Layout::PayloadSize(
        set.data->frequencyHz.size(), Components(set.data->method), set.name.size(), set.deviceIdentity.size());
    Require(payloadSize <= BYTE_LIMIT - HEADER_BYTES);
    Writer writer;
    writer.bytes.reserve(HEADER_BYTES + payloadSize);
    writer.bytes.append(MAGIC, 8U);
    writer.Integer(2U, 4U);
    writer.Integer(0U, 4U);
    writer.Integer(payloadSize, 8U);
    writer.Integer(0U, 8U);
    WritePayload(set, writer);
    Put(writer.bytes, 24U, Checksum(writer.bytes), 8U);
    return std::move(writer.bytes);
}

VnaService::CalibrationSetPtr Decode(const std::string &bytes)
{
    Require(bytes.size() >= HEADER_BYTES + Layout::FIXED_PAYLOAD_BYTES && bytes.size() <= BYTE_LIMIT);
    Reader reader(bytes);
    Require(reader.Text(8U) == MAGIC && reader.Integer(4U) == 2U && reader.Integer(4U) == 0U &&
            reader.Integer(8U) == bytes.size() - HEADER_BYTES && reader.Integer(8U) == Checksum(bytes));
    return ReadPayload(reader, bytes.size() - HEADER_BYTES);
}
}  // 命名空间 CalibrationBinary
}  // 命名空间 VnaWeb
