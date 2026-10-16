/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_binary.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "../../../../VnaCalibration/tests/four_port_mechanical_golden.h"

namespace {
namespace Binary = VnaWeb::CalibrationBinary;
using Method = VnaCalibration::CalibrationMethod;
using Complex = VnaCalibration::Complex;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Same(double expected, double actual)
{
    Require(std::memcmp(&expected, &actual, sizeof(double)) == 0, "binary64 bits changed");
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

void SameShape(const VnaCalibration::CalibrationData &a, const VnaCalibration::CalibrationData &b)
{
    Require(a.method == b.method && a.port1 == b.port1 && a.port2 == b.port2 &&
                a.frequencyHz.size() == b.frequencyHz.size() && a.onePortTerms.size() == b.onePortTerms.size() &&
                a.twoPortTerms.size() == b.twoPortTerms.size() && a.fourPortTerms.size() == b.fourPortTerms.size(),
        "model, routing or dimensions changed");
}

void Same(const VnaService::CalibrationSet &expected, const VnaService::CalibrationSet &actual)
{
    Require(expected.id == actual.id && expected.name == actual.name &&
                expected.deviceIdentity == actual.deviceIdentity &&
                expected.includeIsolation == actual.includeIsolation,
        "metadata changed");
    Same(expected.ifBandwidthHz, actual.ifBandwidthHz);
    const auto &a = *expected.data;
    const auto &b = *actual.data;
    SameShape(a, b);
    Require(expected.powerDbm.size() == actual.powerDbm.size(), "power dimensions changed");
    for (std::size_t point = 0U; point < a.frequencyHz.size(); ++point) {
        Same(a.frequencyHz[point], b.frequencyHz[point]);
        Same(expected.powerDbm[point], actual.powerDbm[point]);
        if (a.method == Method::ONE_PORT) {
            Same(a.onePortTerms[point], b.onePortTerms[point]);
        } else if (a.method == Method::TWO_PORT) {
            Same(a.twoPortTerms[point].forwardReflection, b.twoPortTerms[point].forwardReflection);
            Same(a.twoPortTerms[point].reverseReflection, b.twoPortTerms[point].reverseReflection);
            Same(a.twoPortTerms[point].forwardTransmission, b.twoPortTerms[point].forwardTransmission);
            Same(a.twoPortTerms[point].reverseTransmission, b.twoPortTerms[point].reverseTransmission);
        } else {
            for (std::size_t cell = 0U; cell < 16U; ++cell) {
                Same(a.fourPortTerms[point][cell].offset, b.fourPortTerms[point][cell].offset);
                Same(a.fourPortTerms[point][cell].match, b.fourPortTerms[point][cell].match);
                Same(a.fourPortTerms[point][cell].tracking, b.fourPortTerms[point][cell].tracking);
            }
        }
    }
}

double Bits(std::size_t index)
{
    const std::array<double, 10U> values{
        {-0., 0., (std::numeric_limits<double>::denorm_min)(), -(std::numeric_limits<double>::denorm_min)(),
            (std::numeric_limits<double>::min)(), -(std::numeric_limits<double>::max)(),
            (std::numeric_limits<double>::max)(), std::nextafter(1., 2.), -.125, .375}};
    return values[index % values.size()];
}

VnaCalibration::OnePortTerms Reflection(std::size_t point)
{
    return {
        {Bits(point), Bits(point + 1U)}, {Bits(point + 2U), Bits(point + 3U)}, {Bits(point + 4U), Bits(point + 5U)}};
}

VnaCalibration::TransmissionTerms Transmission(std::size_t point)
{
    return {
        {Bits(point), Bits(point + 1U)}, {Bits(point + 2U), Bits(point + 3U)}, {Bits(point + 4U), Bits(point + 5U)}};
}

VnaService::CalibrationSet MakeSet(Method method, std::size_t points)
{
    VnaService::CalibrationSet set;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = method;
    data->port1 = method == Method::FOUR_PORT ? 1U : 4U;
    data->port2 = method == Method::ONE_PORT ? 0U : method == Method::TWO_PORT ? 2U : 4U;
    set.id = 9007199254740993ULL;
    set.name = u8"二进制校准 'opaque' \\\" 名称";
    set.deviceIdentity = u8"VNB,设备 A,Compact codec,1.0";
    set.ifBandwidthHz = 1000.;
    set.includeIsolation = method != Method::ONE_PORT;
    for (std::size_t point = 0U; point < points; ++point) {
        data->frequencyHz.push_back(10000000. + static_cast<double>(point) * 1000.);
        set.powerDbm.push_back(point % 2U == 0U ? -0. : -10.125);
        if (method == Method::ONE_PORT) {
            data->onePortTerms.push_back(Reflection(point));
        } else if (method == Method::TWO_PORT) {
            data->twoPortTerms.push_back(
                {Reflection(point), Reflection(point + 6U), Transmission(point + 12U), Transmission(point + 18U)});
        } else {
            data->fourPortTerms.push_back(FourPortMechanicalGolden::EXPECTED);
        }
    }
    set.data = std::move(data);
    return set;
}

std::string Unhex(const std::string &hex)
{
    std::string bytes;
    for (std::size_t index = 0U; index < hex.size(); index += 2U) {
        bytes += static_cast<char>(std::stoul(hex.substr(index, 2U), nullptr, 16));
    }
    return bytes;
}

void HandBuiltFixture()
{
    // 独立按公开字段宽度手工构造；FNV=2d10892e4a5e8a25，不调用产品 encoder 生成期望。
    const auto bytes = Unhex(
        "564e4243414c30320200000000000000b200000000000000258a5e4a2e89102d"
        "0807060504030201010000000000000004000000000000000200000001000000"
        "01000000000000000000000000408f40414400000000d012634100000000d012"
        "7341000000000000008000000000004024c0000000000000c03f000000000000"
        "0080000000000000d0bf0100000000000000000000000000f03f000000000000"
        "0000000000000000e0bf000000000000e83f0000000000000000000000000000"
        "f0bf000000000000ec3f000000000000c0bf");
    const auto decoded = Binary::Decode(bytes);
    Require(decoded->id == 0x0102030405060708ULL && decoded->name == "A" && decoded->deviceIdentity == "D" &&
                decoded->data->method == Method::ONE_PORT && decoded->data->port1 == 4U && decoded->data->port2 == 0U,
        "hand-built metadata/byte order differs");
    Same(1000., decoded->ifBandwidthHz);
    Same(10000000., decoded->data->frequencyHz[0U]);
    Same(20000000., decoded->data->frequencyHz[1U]);
    Same(-0., decoded->powerDbm[0U]);
    Same(-10.125, decoded->powerDbm[1U]);
    Same({.125, -0.}, decoded->data->onePortTerms[0U].directivity);
    Same({-.25, (std::numeric_limits<double>::denorm_min)()}, decoded->data->onePortTerms[0U].sourceMatch);
    Same({1., 0.}, decoded->data->onePortTerms[0U].reflectionTracking);
    Same({-.5, .75}, decoded->data->onePortTerms[1U].directivity);
    Same({0., -1.}, decoded->data->onePortTerms[1U].sourceMatch);
    Same({.875, -.125}, decoded->data->onePortTerms[1U].reflectionTracking);
    Require(Binary::Encode(*decoded) == bytes, "encoder differs from independent byte fixture");
}

void RoundtripAndCorrection()
{
    for (const auto method : {Method::ONE_PORT, Method::TWO_PORT, Method::FOUR_PORT}) {
        for (const std::size_t points : {800U, 4000U, 6700U}) {
            const auto set = MakeSet(method, points);
            const auto bytes = Binary::Encode(set);
            const auto decoded = Binary::Decode(bytes);
            Same(set, *decoded);
            Require(Binary::Encode(*decoded) == bytes && bytes.size() < Binary::BYTE_LIMIT,
                "encoding must be deterministic and bounded");
            if (method == Method::FOUR_PORT) {
                for (const auto &dut : FourPortMechanicalGolden::DUT) {
                    VnaCalibration::FourPortNetwork corrected;
                    Require(VnaCalibration::CorrectFourPort(dut.measured, decoded->data->fourPortTerms.back(),
                                corrected) == VnaCalibration::CalibrationError::NONE,
                        "decoded nonidentity four-port terms must correct DUT");
                    for (std::size_t index = 0U; index < corrected.size(); ++index) {
                        Require(std::abs(corrected[index] - dut.corrected[index]) < 1e-12,
                            "receiver/source or nonzero isolation changed during binary transport");
                    }
                }
            }
        }
    }
}

void Put(std::string &bytes, std::size_t offset, std::uint64_t value, unsigned count)
{
    for (unsigned index = 0U; index < count; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xffU);
    }
}

void Seal(std::string &bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 32U; index < bytes.size(); ++index) {
        hash = (hash ^ static_cast<unsigned char>(bytes[index])) * 1099511628211ULL;
    }
    Put(bytes, 16U, bytes.size() - 32U, 8U);
    Put(bytes, 24U, hash, 8U);
}

void Reject(const std::string &bytes)
{
    try {
        Binary::Decode(bytes);
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("malformed binary unexpectedly accepted");
}

void CorruptionAndBounds()
{
    const auto bytes = Binary::Encode(MakeSet(Method::TWO_PORT, 2U));
    for (std::size_t size = 0U; size < bytes.size(); ++size) {
        Reject(bytes.substr(0U, size));
    }
    const std::array<std::size_t, 7U> offsets{{0U, 8U, 12U, 16U, 24U, 32U, bytes.size() - 1U}};
    for (const auto offset : offsets) {
        auto invalid = bytes;
        invalid[offset] ^= 1;
        Reject(invalid);
    }
    auto invalid = bytes + "trailer";
    Seal(invalid);
    Reject(invalid);
    Reject(std::string(Binary::BYTE_LIMIT + 1U, '\0'));
    for (const auto change : {std::array<std::size_t, 2U>{{40U, 3U}}, {{44U, 2U}}, {{48U, 0U}}, {{52U, 4U}},
             {{56U, 10002U}}, {{60U, 129U}}, {{64U, 513U}}, {{68U, 1U}}}) {
        invalid = bytes;
        Put(invalid, change[0U], change[1U], 4U);
        Seal(invalid);
        Reject(invalid);
    }
    invalid = bytes;
    Put(invalid, 72U, 0x7ff0000000000000ULL, 8U);
    Seal(invalid);
    Reject(invalid);
    invalid = bytes;
    invalid[80U] = static_cast<char>(0xffU);
    Seal(invalid);
    Reject(invalid);
    invalid = bytes;
    Put(invalid, invalid.size() - 8U, 0x7ff8000000000001ULL, 8U);
    Seal(invalid);
    Reject(invalid);
    auto source = MakeSet(Method::ONE_PORT, 2U);
    source.powerDbm.clear();
    try {
        Binary::Encode(source);
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("incomplete metadata unexpectedly encoded");
}
}  // 匿名命名空间

int main()
{
    try {
        HandBuiltFixture();
        RoundtripAndCorrection();
        CorruptionAndBounds();
        std::cout << "Calibration binary: independent fixture, bit preservation, golden correction and bounds passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
