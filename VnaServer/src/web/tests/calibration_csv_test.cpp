/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_csv.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "web/calibration_binary.h"
#include "web/calibration_csv_reader.h"

namespace {
using Method = VnaCalibration::CalibrationMethod;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::CalibrationSet MakeSet(Method method, unsigned points = 4U)
{
    VnaService::CalibrationSet set;
    set.id = (std::numeric_limits<std::uint64_t>::max)() - 1U;
    set.name = u8"=四端口, \"线缆\"";
    set.deviceIdentity = u8"Device,\"仪器\"";
    set.ifBandwidthHz = 1000.0;
    set.includeIsolation = method != Method::ONE_PORT;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = method;
    data->port1 = method == Method::FOUR_PORT ? 1U : method == Method::ONE_PORT ? 3U : 2U;
    data->port2 = method == Method::ONE_PORT ? 0U : 4U;
    for (unsigned index = 0U; index < points; ++index) {
        data->frequencyHz.push_back(10000000.0 + index * 1000.0);
        set.powerDbm.push_back(std::vector<double>{-120.0, -12.375, -0.0, 100.0}[index % 4U]);
        VnaCalibration::OnePortTerms reflection{
            {-0.0, std::numeric_limits<double>::denorm_min()}, {0.03125, -1e-300}, {1.25, -2.5e-200}};
        VnaCalibration::TransmissionTerms reverse{{21.125, -0.25}, {0.015625, -0.03125}, {1.5, 0.125}};
        VnaCalibration::TransmissionTerms forward{{42.25, 0.5}, {-0.015625, 0.0625}, {1.75, -0.125}};
        if (method == Method::ONE_PORT) {
            data->onePortTerms.push_back(reflection);
        } else if (method == Method::TWO_PORT) {
            data->twoPortTerms.push_back({reflection, reflection, forward, reverse});
        } else {
            VnaCalibration::FourPortTerms terms;
            for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
                terms[cell] = {{0.125 + cell, -0.25}, {0.015625, -0.03125}, {1.5, 0.125}};
            }
            data->fourPortTerms.push_back(terms);
        }
    }
    set.data = std::move(data);
    return set;
}

void EqualBits(const VnaService::CalibrationSet &left, const VnaService::CalibrationSet &right)
{
    Require(VnaWeb::CalibrationBinary::Encode(left) == VnaWeb::CalibrationBinary::Encode(right),
        "CSV changed metadata or binary64 bit patterns");
}

std::string Replace(std::string source, const std::string &before, const std::string &after)
{
    const auto position = source.find(before);
    Require(position != std::string::npos, "CSV test mutation target missing: " + before);
    source.replace(position, before.size(), after);
    return source;
}

void Reject(const std::string &bytes)
{
    try {
        VnaWeb::CalibrationCsv::Decode(bytes);
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("CSV accepted a malformed document");
}

void RoundTrips()
{
    for (const auto method : {Method::ONE_PORT, Method::TWO_PORT, Method::FOUR_PORT}) {
        const auto source = MakeSet(method);
        const auto csv = VnaWeb::CalibrationCsv::Encode(source);
        Require(csv.find("text:=") != std::string::npos, "Text prefix prevents spreadsheet formula interpretation");
        const auto decoded = VnaWeb::CalibrationCsv::Decode(csv);
        EqualBits(source, *decoded);
        EqualBits(source, *VnaWeb::CalibrationCsv::Decode(csv.substr(3U)));
    }
    auto source = MakeSet(Method::TWO_PORT);
    source.deviceIdentity += "\r\nUnit 2";
    const auto decoded = VnaWeb::CalibrationCsv::Decode(VnaWeb::CalibrationCsv::Encode(source));
    Require(decoded->deviceIdentity == source.deviceIdentity, "Quoted UTF8 metadata preserves embedded CRLF");
}

void PortOrderAndIsolation()
{
    for (const auto method : {Method::TWO_PORT, Method::FOUR_PORT}) {
        auto source = MakeSet(method);
        auto data = std::make_shared<VnaCalibration::CalibrationData>(*source.data);
        source.includeIsolation = false;
        if (method == Method::TWO_PORT) {
            data->port1 = 4U;
            data->port2 = 2U;
            for (auto &terms : data->twoPortTerms) {
                terms.forwardTransmission.isolation = {-0.0, 0.0};
                terms.reverseTransmission.isolation = {0.0, -0.0};
            }
        } else {
            for (auto &terms : data->fourPortTerms) {
                for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
                    if (cell / 4U != cell % 4U) {
                        terms[cell].offset = {-0.0, 0.0};
                    }
                }
            }
        }
        source.data = std::move(data);
        const auto csv = VnaWeb::CalibrationCsv::Encode(source);
        EqualBits(source, *VnaWeb::CalibrationCsv::Decode(csv));
        if (method == Method::TWO_PORT) {
            Require(csv.find("S42_isolation_real") < csv.find("S24_isolation_real"), "Reversed P4/P2 stays physical");
        }
    }
}

void LocaleAndLineEndings()
{
    struct CommaNumbers final : std::numpunct<char> {
        char do_decimal_point() const override
        {
            return ',';
        }
    };

    struct RestoreLocale final {
        std::locale saved = std::locale();

        ~RestoreLocale()
        {
            std::locale::global(saved);
        }
    } restore;

    std::locale::global(std::locale(std::locale::classic(), new CommaNumbers));
    const auto source = MakeSet(Method::TWO_PORT);
    const auto csv = VnaWeb::CalibrationCsv::Encode(source);
    EqualBits(source, *VnaWeb::CalibrationCsv::Decode(csv));
    std::string lf;
    for (const char value : csv) {
        if (value != '\r') {
            lf += value;
        }
    }
    EqualBits(source, *VnaWeb::CalibrationCsv::Decode(lf));
}

void PhysicalColumns()
{
    const auto csv = VnaWeb::CalibrationCsv::Encode(MakeSet(Method::TWO_PORT));
    VnaWeb::CalibrationCsvDetail::Rows input(csv);
    std::vector<std::string> row;
    for (unsigned index = 0U; index < 11U; ++index) {
        Require(input.Next(row), "CSV metadata/header row missing");
    }
    Require(row.size() == 26U && row[2U] == "S22_directivity_real", "Two-port columns retain physical P2/P4");
    Require(row[8U] == "S24_isolation_real" && row[14U] == "S42_isolation_real", "Transmission directions explicit");
    Require(input.Next(row) && row[8U] == "21.125" && row[14U] == "42.25", "Reverse/forward offsets not swapped");
    const auto four = VnaWeb::CalibrationCsv::Encode(MakeSet(Method::FOUR_PORT));
    Require(four.find("S34_load_match_real") < four.find("S41_isolation_real"), "Four-port matrix is receiver-major");
}

void InvalidMetadataAndRows()
{
    const auto csv = VnaWeb::CalibrationCsv::Encode(MakeSet(Method::ONE_PORT));
    Reject(Replace(csv, "vnapro_calibration_csv,1", "vnapro_calibration_csv,2"));
    Reject(Replace(csv, "\"name\",", "\"unknown\","));
    Reject(Replace(csv, "\"device_identity\",", "\"name\","));
    Reject(Replace(csv, "text:Device", "Device"));
    Reject(Replace(csv, "text:Device,\"\"仪器\"\"", "text:"));
    Reject(Replace(csv, "text:18446744073709551614", "text:18446744073709551616"));
    Reject(Replace(csv, "\"points\",\"4\"", "\"points\",\"10002\""));
    Reject(Replace(csv, "\"points\",\"4\"", "\"points\",\"3\""));
    Reject(Replace(csv, "\"points\",\"4\"", "\"points\",\"5\""));
    Reject(Replace(csv, "S33_source_match_real", "S33_directivity_real"));
    Reject(Replace(csv, "\"port2\",\"0\"", "\"port2\",\"1\""));
    Reject(Replace(csv, "\"if_bandwidth_hz\",\"1000\"", "\"if_bandwidth_hz\",\"0\""));
    Reject(Replace(csv, "10001000,-12.375", "10000000,-12.375"));
    Reject(Replace(csv, "10001000,-12.375", "9999999,-12.375"));
    Reject(csv + "\r\n");
    Reject(csv + "1,2\r\n");
    Reject(csv.substr(0U, csv.rfind("\r\n", csv.size() - 3U) + 2U));
    Reject(std::string(VnaWeb::CalibrationCsv::BYTE_LIMIT + 1U, 'x'));
}

void InvalidNumbersAndQuotes()
{
    const auto csv = VnaWeb::CalibrationCsv::Encode(MakeSet(Method::ONE_PORT));
    for (const auto *invalid : {"NaN", "Inf", "1e309", "+1", "01", " 1", "\t1", "1\t", "1 2", "=1+1"}) {
        Reject(Replace(csv, "10000000,-120", std::string(invalid) + ",-120"));
    }
    Reject(Replace(csv, "10000000,-120", "10000000,-120.001"));
    Reject(Replace(csv, "10000000,-120", "10000000,"));
    Reject(Replace(csv, "10000000,-120", "10000000,-120,1"));
    Reject(Replace(csv, "10000000,-120", "10000000,\"-120\"junk"));
    Reject(Replace(csv, "10000000,-120", "10000000,\"-120"));
    Reject(Replace(csv, "text:Device", std::string("text:") + char(0xff)));
    Reject(Replace(csv, "\"include_isolation\",\"false\"", "\"include_isolation\",\"true\""));
}

void LargeFourPortFiles()
{
    for (const auto points : {800U, 4000U, 6700U}) {
        const auto source = MakeSet(Method::FOUR_PORT, points);
        const auto csv = VnaWeb::CalibrationCsv::Encode(source);
        const auto decoded = VnaWeb::CalibrationCsv::Decode(csv);
        EqualBits(source, *decoded);
        std::cout << "CSV roundtrip " << points << " points, " << csv.size() << " bytes\n";
    }
}

void InvalidEncode()
{
    for (unsigned scenario = 0U; scenario < 5U; ++scenario) {
        auto source = MakeSet(Method::ONE_PORT);
        auto data = std::make_shared<VnaCalibration::CalibrationData>(*source.data);
        if (scenario == 0U) {
            source.deviceIdentity.clear();
        } else if (scenario == 1U) {
            source.powerDbm.pop_back();
        } else if (scenario == 2U) {
            data->onePortTerms.pop_back();
        } else if (scenario == 3U) {
            data->onePortTerms.front().sourceMatch = {std::numeric_limits<double>::infinity(), 0.0};
        } else {
            source.id = 0U;
        }
        source.data = std::move(data);
        try {
            VnaWeb::CalibrationCsv::Encode(source);
        } catch (const std::invalid_argument &) {
            continue;
        }
        throw std::runtime_error("CSV encoded an invalid domain object");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        RoundTrips();
        PortOrderAndIsolation();
        LocaleAndLineEndings();
        PhysicalColumns();
        InvalidMetadataAndRows();
        InvalidNumbersAndQuotes();
        LargeFourPortFiles();
        InvalidEncode();
        std::cout << "PASS calibration CSV metadata, binary64 precision, physical directions and malformed input\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
