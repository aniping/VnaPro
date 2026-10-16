/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "calibration_cache.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using VnaCalibration::CalibrationMethod;
using VnaCalibration::Complex;
using VnaService::CalibrationSet;
using VnaService::CalibrationSetPtr;
namespace Cache = VnaServer::CalibrationCache;
constexpr std::size_t HEADER_BYTES = 56U;
constexpr std::size_t STRING_OFFSET = 104U;
constexpr std::size_t SOURCE_BYTES = 12345U;
const std::string g_sourceDigest = "1234567890abcdef";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

double Number(std::size_t index)
{
    const std::array<double, 12U> values{{-0.0, 0.0, (std::numeric_limits<double>::denorm_min)(),
        -(std::numeric_limits<double>::denorm_min)(), (std::numeric_limits<double>::min)(),
        -(std::numeric_limits<double>::min)(), (std::numeric_limits<double>::max)(),
        -(std::numeric_limits<double>::max)(), std::nextafter(1.0, 2.0), std::nextafter(-1.0, -2.0), 0.125, -0.375}};
    return values[index % values.size()];
}

Complex Pair(std::size_t index)
{
    return {Number(index), index < 12U ? Number(index + 1U) : static_cast<double>(index) + 0.125};
}

VnaCalibration::OnePortTerms Reflection(std::size_t index)
{
    return {Pair(index), Pair(index + 2U), Pair(index + 4U)};
}

VnaCalibration::TransmissionTerms Transmission(std::size_t index)
{
    return {Pair(index), Pair(index + 2U), Pair(index + 4U)};
}

CalibrationSetPtr Set(CalibrationMethod method, std::size_t points, bool isolation = false)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = method;
    data->port1 = method == CalibrationMethod::TWO_PORT ? 2U : 1U;
    data->port2 = method == CalibrationMethod::ONE_PORT ? 0U : 4U;
    auto set = std::make_shared<CalibrationSet>();
    set->id = 9007199254740993ULL;
    set->name = u8"缓存 'opaque' \\;\" 名称";
    set->deviceIdentity = u8"VNB,设备 A,Explicit codec fixture,1.0";
    set->includeIsolation = isolation;
    set->ifBandwidthHz = Number(points);
    for (std::size_t point = 0U; point < points; ++point) {
        // Codec 保留有限位型；仪器频轴及数学适用性仍须由 Restore 验证。
        data->frequencyHz.push_back(Number(point));
        set->powerDbm.push_back(point % 2U == 0U ? -0.0 : 0.0);
        if (method == CalibrationMethod::ONE_PORT) {
            data->onePortTerms.push_back(Reflection(point * 25U));
        } else if (method == CalibrationMethod::TWO_PORT) {
            data->twoPortTerms.push_back({Reflection(point * 25U), Reflection(point * 25U + 6U),
                Transmission(point * 25U + 12U), Transmission(point * 25U + 18U)});
        } else {
            VnaCalibration::FourPortTerms row;
            for (std::size_t cell = 0U; cell < row.size(); ++cell) {
                const auto index = point * 101U + cell * 6U;
                row[cell] = {Pair(index), Pair(index + 2U), Pair(index + 4U)};
            }
            data->fourPortTerms.push_back(row);
        }
    }
    set->data = std::move(data);
    return set;
}

void Same(double expected, double actual)
{
    Require(std::memcmp(&expected, &actual, sizeof(double)) == 0, "Every finite double must retain its exact bits");
}

void Same(const Complex &expected, const Complex &actual)
{
    Same(expected.real(), actual.real());
    Same(expected.imag(), actual.imag());
}

void Same(const VnaCalibration::OnePortTerms &expected, const VnaCalibration::OnePortTerms &actual)
{
    Same(expected.directivity, actual.directivity);
    Same(expected.sourceMatch, actual.sourceMatch);
    Same(expected.reflectionTracking, actual.reflectionTracking);
}

void Same(const VnaCalibration::TransmissionTerms &expected, const VnaCalibration::TransmissionTerms &actual)
{
    Same(expected.isolation, actual.isolation);
    Same(expected.loadMatch, actual.loadMatch);
    Same(expected.transmissionTracking, actual.transmissionTracking);
}

void Same(const VnaCalibration::TwoPortTerms &expected, const VnaCalibration::TwoPortTerms &actual)
{
    Same(expected.forwardReflection, actual.forwardReflection);
    Same(expected.reverseReflection, actual.reverseReflection);
    Same(expected.forwardTransmission, actual.forwardTransmission);
    Same(expected.reverseTransmission, actual.reverseTransmission);
}

void Same(const VnaCalibration::FourPortTerms &expected, const VnaCalibration::FourPortTerms &actual)
{
    for (std::size_t cell = 0U; cell < expected.size(); ++cell) {
        Same(expected[cell].offset, actual[cell].offset);
        Same(expected[cell].match, actual[cell].match);
        Same(expected[cell].tracking, actual[cell].tracking);
    }
}

template <class Value>
void Same(const std::vector<Value> &expected, const std::vector<Value> &actual)
{
    Require(expected.size() == actual.size(), "Vector lengths must roundtrip");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        Same(expected[index], actual[index]);
    }
}

void Same(const CalibrationSet &expected, const CalibrationSet &actual)
{
    Require(expected.id == actual.id && expected.name == actual.name &&
                expected.deviceIdentity == actual.deviceIdentity &&
                expected.includeIsolation == actual.includeIsolation,
        "IDs, opaque UTF-8 strings and isolation must roundtrip");
    Same(expected.ifBandwidthHz, actual.ifBandwidthHz);
    Same(expected.powerDbm, actual.powerDbm);
    Require(expected.data && actual.data && expected.data->method == actual.data->method &&
                expected.data->port1 == actual.data->port1 && expected.data->port2 == actual.data->port2,
        "Model and physical port routing must roundtrip");
    Same(expected.data->frequencyHz, actual.data->frequencyHz);
    Same(expected.data->onePortTerms, actual.data->onePortTerms);
    Same(expected.data->twoPortTerms, actual.data->twoPortTerms);
    Same(expected.data->fourPortTerms, actual.data->fourPortTerms);
}

void Put(std::string &bytes, std::size_t offset, std::uint64_t value, std::size_t width)
{
    Require(offset <= bytes.size() && width <= bytes.size() - offset, "Test mutation must stay inside its buffer");
    for (std::size_t byte = 0U; byte < width; ++byte) {
        bytes[offset + byte] = static_cast<char>((value >> (byte * 8U)) & 0xffU);
    }
}

void PutDouble(std::string &bytes, std::size_t offset, double value)
{
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "Cache requires binary64");
    std::memcpy(&bits, &value, sizeof(bits));
    Put(bytes, offset, bits, sizeof(bits));
}

void Seal(std::string &bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = HEADER_BYTES; index < bytes.size(); ++index) {
        hash = (hash ^ static_cast<unsigned char>(bytes[index])) * 1099511628211ULL;
    }
    Put(bytes, 40U, bytes.size() - HEADER_BYTES, 8U);
    Put(bytes, 48U, hash, 8U);
}

void Reject(const std::string &bytes, const std::string &reason, const std::string &digest = g_sourceDigest,
    std::size_t sourceBytes = SOURCE_BYTES)
{
    const auto previous = Set(CalibrationMethod::ONE_PORT, 2U);
    auto output = previous;
    Require(!Cache::Decode(bytes, digest, sourceBytes, output), reason);
    Require(output == previous, "Cache miss/failure must not replace caller output: " + reason);
}

void Roundtrip()
{
    const std::array<CalibrationMethod, 3U> methods{
        {CalibrationMethod::ONE_PORT, CalibrationMethod::TWO_PORT, CalibrationMethod::FOUR_PORT}};
    for (const auto method : methods) {
        for (std::size_t variant = 0U; variant < 12U; ++variant) {
            const auto source = Set(method, variant + 2U, method != CalibrationMethod::ONE_PORT && variant % 2U != 0U);
            const auto bytes = Cache::Encode(*source, g_sourceDigest, SOURCE_BYTES);
            CalibrationSetPtr decoded;
            Require(Cache::Decode(bytes, g_sourceDigest, SOURCE_BYTES, decoded), "Valid cache must decode");
            Require(decoded != source && decoded->data != source->data, "Decoded data must own independent storage");
            Same(*source, *decoded);
            Require(Cache::Encode(*decoded, g_sourceDigest, SOURCE_BYTES) == bytes, "Encoding must be deterministic");
        }
    }
}

void HeaderAndTruncation()
{
    const auto bytes = Cache::Encode(*Set(CalibrationMethod::FOUR_PORT, 2U), g_sourceDigest, SOURCE_BYTES);
    Require(bytes.compare(0U, 8U, "VNBCAL01") == 0 && bytes[8U] == 1 && bytes[9U] == 0,
        "Versioned cache signature must use the documented byte order");
    Require(static_cast<unsigned char>(bytes[HEADER_BYTES]) == 1U &&
                static_cast<unsigned char>(bytes[HEADER_BYTES + 6U]) == 32U,
        "uint64 IDs above JSON's exact integer range must use explicit little endian");
    for (std::size_t length = 0U; length < bytes.size(); ++length) {
        Reject(bytes.substr(0U, length), "Every truncated cache must be rejected");
    }
    for (const auto offset : {0U, 8U, 12U, 16U, 24U, 40U, 48U}) {
        auto changed = bytes;
        changed[offset] ^= 1;
        Reject(changed, "Magic, version, reserved, source identity and checksum must be checked");
    }
    Reject(bytes + "trailing", "Trailing bytes must not be accepted");
    Reject(bytes, "Source digest mismatch must miss", "1234567890abcdee");
    Reject(bytes, "Source length mismatch must miss", g_sourceDigest, SOURCE_BYTES + 1U);
    Reject(bytes, "Malformed source digest must miss", "not-a-digest");
}

void TwoPortWireOrder()
{
    const auto source = Set(CalibrationMethod::TWO_PORT, 2U, true);
    const auto bytes = Cache::Encode(*source, g_sourceDigest, SOURCE_BYTES);
    const auto &row = source->data->twoPortTerms.front();
    const std::array<Complex, 12U> expected{{row.forwardReflection.directivity, row.forwardReflection.sourceMatch,
        row.forwardReflection.reflectionTracking, row.reverseTransmission.isolation, row.reverseTransmission.loadMatch,
        row.reverseTransmission.transmissionTracking, row.forwardTransmission.isolation,
        row.forwardTransmission.loadMatch, row.forwardTransmission.transmissionTracking,
        row.reverseReflection.directivity, row.reverseReflection.sourceMatch,
        row.reverseReflection.reflectionTracking}};
    std::string wire(expected.size() * 16U, '\0');
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        PutDouble(wire, index * 16U, expected[index].real());
        PutDouble(wire, index * 16U + 8U, expected[index].imag());
    }
    const auto offset = STRING_OFFSET + source->name.size() + source->deviceIdentity.size() + 2U * 16U;
    Require(
        bytes.compare(offset, wire.size(), wire) == 0, "Two-port wire order must match the versioned JSON term order");
}

void MetadataBoundaries()
{
    auto source = std::make_shared<CalibrationSet>(*Set(CalibrationMethod::ONE_PORT, 2U));
    source->id = UINT64_MAX - 1U;
    source->name.assign(128U, 'n');
    source->deviceIdentity.assign(512U, 'i');
    source->deviceIdentity[0U] = '\0';
    source->deviceIdentity[1U] = '\n';
    source->ifBandwidthHz = -0.0;
    source->powerDbm = {-120.0, 100.0};
    const auto bytes = Cache::Encode(*source, g_sourceDigest, SOURCE_BYTES);
    CalibrationSetPtr decoded;
    Require(Cache::Decode(bytes, g_sourceDigest, SOURCE_BYTES, decoded), "Valid metadata boundaries must decode");
    Same(*source, *decoded);
    auto changed = bytes;
    changed[STRING_OFFSET + source->name.size()] = static_cast<char>(0xffU);
    Seal(changed);
    Reject(changed, "Device identity still requires valid UTF-8");
    for (const auto power : {-120.001, 100.001, (std::numeric_limits<double>::quiet_NaN)()}) {
        changed = bytes;
        PutDouble(changed, STRING_OFFSET + source->name.size() + source->deviceIdentity.size() + 16U, power);
        Seal(changed);
        Reject(changed, "Power must be finite and inside both inclusive limits");
    }
}

void MalformedPayload()
{
    const auto source = Set(CalibrationMethod::FOUR_PORT, 2U);
    const auto bytes = Cache::Encode(*source, g_sourceDigest, SOURCE_BYTES);

    struct Mutation final {
        std::size_t offset;
        std::uint64_t value;
        std::size_t width;
    };

    const std::array<Mutation, 12U> mutations{
        {{56U, 0U, 8U}, {56U, UINT64_MAX, 8U}, {64U, 3U, 4U}, {68U, 2U, 4U}, {72U, 0U, 4U}, {76U, 3U, 4U},
            {80U, 1U, 4U}, {80U, 10002U, 4U}, {80U, UINT32_MAX, 4U}, {84U, 129U, 4U}, {88U, 513U, 4U}, {92U, 1U, 4U}}};
    for (const auto &mutation : mutations) {
        auto changed = bytes;
        Put(changed, mutation.offset, mutation.value, mutation.width);
        Seal(changed);
        Reject(changed, "Invalid payload field must fail even with a valid checksum");
    }
    const auto numeric = STRING_OFFSET + source->name.size() + source->deviceIdentity.size();
    const std::array<std::size_t, 3U> offsets{{96U, numeric, numeric + 32U}};
    for (const auto offset : offsets) {
        auto changed = bytes;
        PutDouble(changed, offset, (std::numeric_limits<double>::infinity)());
        Seal(changed);
        Reject(changed, "Nonfinite IFBW, frequency or coefficient must fail");
    }
    auto changed = bytes;
    PutDouble(changed, numeric + 16U, -121.0);
    Seal(changed);
    Reject(changed, "Power must retain the JSON range contract");
    changed = bytes;
    changed[STRING_OFFSET] = static_cast<char>(0xffU);
    Seal(changed);
    Reject(changed, "Opaque names still require valid UTF-8");
    changed = bytes + std::string(8U, '\0');
    Seal(changed);
    Reject(changed, "Extra payload must fail even when length and checksum match");
}

void ShapeAndStringValidation()
{
    const auto one = Cache::Encode(*Set(CalibrationMethod::ONE_PORT, 2U), g_sourceDigest, SOURCE_BYTES);
    auto changed = one;
    Put(changed, 68U, 1U, 4U);
    Seal(changed);
    Reject(changed, "One-port data cannot claim isolation");
    const auto two = Cache::Encode(*Set(CalibrationMethod::TWO_PORT, 2U), g_sourceDigest, SOURCE_BYTES);
    changed = two;
    Put(changed, 76U, 2U, 4U);
    Seal(changed);
    Reject(changed, "Two-port physical ports must differ");
    for (const auto character : {'\0', '\n', '\x7f'}) {
        changed = one;
        changed[STRING_OFFSET] = character;
        Seal(changed);
        Reject(changed, "Names must reject control characters");
    }
    changed = one;
    Put(changed, 88U, 0U, 4U);
    Seal(changed);
    Reject(changed, "Empty device identity cannot be restored");
    changed = one;
    Put(changed, 40U, UINT64_MAX, 8U);
    Reject(changed, "Hostile lengths must fail without allocation based on the claimed length");
    Reject(std::string(Cache::BYTE_LIMIT + 1U, '\0'), "Cache byte budget must be enforced");
}

void BoundedSizeAndOwnership()
{
    const std::array<CalibrationMethod, 3U> methods{
        {CalibrationMethod::ONE_PORT, CalibrationMethod::TWO_PORT, CalibrationMethod::FOUR_PORT}};
    const std::array<std::size_t, 3U> strides{{64U, 208U, 784U}};
    for (std::size_t method = 0U; method < methods.size(); ++method) {
        const auto small = Cache::Encode(*Set(methods[method], 2U), g_sourceDigest, SOURCE_BYTES);
        const auto source = Set(methods[method], 10001U);
        const auto large = Cache::Encode(*source, g_sourceDigest, SOURCE_BYTES);
        Require(large.size() == small.size() + 9999U * strides[method] && large.size() <= Cache::BYTE_LIMIT,
            "Cache must use the exact bounded 64/208/784-byte point representation");
        CalibrationSetPtr decoded;
        Require(Cache::Decode(large, g_sourceDigest, SOURCE_BYTES, decoded), "Maximum supported point count must fit");
        Same(*source, *decoded);
        const std::weak_ptr<const CalibrationSet> previous = decoded;
        const std::weak_ptr<const VnaCalibration::CalibrationData> previousData = decoded->data;
        Require(
            Cache::Decode(small, g_sourceDigest, SOURCE_BYTES, decoded), "A following set must decode independently");
        Require(previous.expired() && previousData.expired(), "Codec must not retain earlier decoded coefficient sets");
    }
}
}  // 命名空间

int main()
{
    try {
        Roundtrip();
        HeaderAndTruncation();
        TwoPortWireOrder();
        MetadataBoundaries();
        MalformedPayload();
        ShapeAndStringValidation();
        BoundedSizeAndOwnership();
        std::cout << "PASS calibration cache bit-exact models, rejection, bounds and ownership\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
