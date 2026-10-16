/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_csv.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "vna_service/instrument_profile.h"
#include "web/calibration_csv_layout.h"
#include "web/calibration_csv_reader.h"

namespace VnaWeb {
namespace CalibrationCsv {
namespace {
using namespace CalibrationCsvDetail;

std::string ReadPair(Rows &input, const std::string &name)
{
    std::vector<std::string> row;
    Require(input.Next(row) && row.size() == 2U && row[0U] == name);
    return row[1U];
}

std::string TextValue(const std::string &text)
{
    Require(text.compare(0U, 5U, "text:") == 0);
    return text.substr(5U);
}

bool Boolean(const std::string &text)
{
    Require(text == "true" || text == "false");
    return text == "true";
}

unsigned ReadCount(Rows &input, const std::string &key, unsigned minimum, unsigned maximum)
{
    const double value = Numeric(ReadPair(input, key));
    Require(value >= minimum && value <= maximum && std::floor(value) == value);
    return static_cast<unsigned>(value);
}

std::string NumberText(double value)
{
    std::ostringstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    return output.str();
}

void WriteRow(std::ostream &output, const std::vector<std::string> &row)
{
    bool first = true;
    for (const auto &cell : row) {
        output << (first ? "" : ",") << '"';
        first = false;
        for (const char value : cell) {
            output << (value == '"' ? "\"" : "") << value;
        }
        output << '"';
    }
    output << "\r\n";
}

VnaService::CalibrationSet Metadata(Rows &input, VnaCalibration::CalibrationData &data, unsigned &points)
{
    Require(ReadPair(input, "vnapro_calibration_csv") == "1");
    VnaService::CalibrationSet set;
    set.id = Identifier(TextValue(ReadPair(input, "source_id")));
    set.name = TextValue(ReadPair(input, "name"));
    set.deviceIdentity = TextValue(ReadPair(input, "device_identity"));
    data.method = ParseMethod(ReadPair(input, "method"));
    data.port1 = ReadCount(input, "port1", 1U, 4U);
    data.port2 = ReadCount(input, "port2", 0U, 4U);
    set.includeIsolation = Boolean(ReadPair(input, "include_isolation"));
    set.ifBandwidthHz = Numeric(ReadPair(input, "if_bandwidth_hz"));
    points = ReadCount(input, "points", VnaService::MIN_SWEEP_POINTS, VnaService::MAX_SWEEP_POINTS);
    return set;
}
}  // 匿名命名空间

std::string Encode(const VnaService::CalibrationSet &set)
{
    Validate(set);
    const auto &data = *set.data;
    std::ostringstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.imbue(std::locale::classic());
    output << "\xef\xbb\xbf" << std::setprecision(17) << "vnapro_calibration_csv,1\r\n";
    WriteRow(output, {"source_id", "text:" + std::to_string(set.id)});
    WriteRow(output, {"name", "text:" + set.name});
    WriteRow(output, {"device_identity", "text:" + set.deviceIdentity});
    WriteRow(output, {"method", MethodText(data.method)});
    WriteRow(output, {"port1", std::to_string(data.port1)});
    WriteRow(output, {"port2", std::to_string(data.port2)});
    WriteRow(output, {"include_isolation", set.includeIsolation ? "true" : "false"});
    WriteRow(output, {"if_bandwidth_hz", NumberText(set.ifBandwidthHz)});
    WriteRow(output, {"points", std::to_string(data.frequencyHz.size())});
    WriteRow(output, Columns(data));
    for (std::size_t point = 0U; point < data.frequencyHz.size(); ++point) {
        output << data.frequencyHz[point] << ',' << set.powerDbm[point];
        for (const auto value : Values(data, point)) {
            output << ',' << value;
        }
        output << "\r\n";
    }
    auto bytes = output.str();
    Require(bytes.size() <= BYTE_LIMIT);
    return bytes;
}

VnaService::CalibrationSetPtr Decode(const std::string &bytes)
{
    Rows input(bytes);
    unsigned points = 0U;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    auto set = std::make_shared<VnaService::CalibrationSet>(Metadata(input, *data, points));
    const auto columns = Columns(*data);
    std::vector<std::string> row;
    Require(input.Next(row) && row == columns);
    data->frequencyHz.reserve(points);
    set->powerDbm.reserve(points);
    for (unsigned point = 0U; point < points; ++point) {
        Require(input.Next(row) && row.size() == columns.size());
        data->frequencyHz.push_back(Numeric(row[0U]));
        set->powerDbm.push_back(Numeric(row[1U]));
        AppendValues(*data, row);
    }
    Require(!input.Next(row));
    set->data = std::move(data);
    Validate(*set);
    return set;
}
}  // 命名空间 CalibrationCsv
}  // 命名空间 VnaWeb
