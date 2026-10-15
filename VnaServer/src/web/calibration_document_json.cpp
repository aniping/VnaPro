/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include "vna_core/sweep_plan.h"
#include "web/calibration_json.h"
#include "web/calibration_json_fields.h"

namespace VnaWeb {
namespace {
using namespace CalibrationJsonDetail;

bool PowerValid(double power) noexcept
{
    return std::isfinite(power) && power >= VnaCore::MIN_POWER_DBM && power <= VnaCore::MAX_POWER_DBM;
}

void AppendComplex(Json &row, const VnaCalibration::Complex &value)
{
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::invalid_argument("Cannot export nonfinite calibration coefficients");
    }
    row.array.emplace_back(value.real());
    row.array.emplace_back(value.imag());
}

void AppendReflection(Json &row, const VnaCalibration::OnePortTerms &terms)
{
    AppendComplex(row, terms.directivity);
    AppendComplex(row, terms.sourceMatch);
    AppendComplex(row, terms.reflectionTracking);
}

void AppendTransmission(Json &row, const VnaCalibration::TransmissionTerms &terms)
{
    AppendComplex(row, terms.isolation);
    AppendComplex(row, terms.loadMatch);
    AppendComplex(row, terms.transmissionTracking);
}

VnaCalibration::Complex ComplexAt(const Json &row, std::size_t offset)
{
    for (const auto index : {offset, offset + 1U}) {
        if (row.array[index].type != Json::Type::NUMBER || !std::isfinite(row.array[index].number)) {
            throw std::invalid_argument("Calibration coefficients must be finite numbers");
        }
    }
    return {row.array[offset].number, row.array[offset + 1U].number};
}

VnaCalibration::OnePortTerms ReflectionAt(const Json &row, std::size_t offset)
{
    return {ComplexAt(row, offset), ComplexAt(row, offset + 2U), ComplexAt(row, offset + 4U)};
}

VnaCalibration::TransmissionTerms TransmissionAt(const Json &row, std::size_t offset)
{
    return {ComplexAt(row, offset), ComplexAt(row, offset + 2U), ComplexAt(row, offset + 4U)};
}

Json ExportTerms(const VnaCalibration::CalibrationData &data, bool reflection)
{
    Json values = Json::MakeArray();
    if (reflection) {
        for (const auto &terms : data.onePortTerms) {
            Json row = Json::MakeArray();
            AppendReflection(row, terms);
            values.array.push_back(std::move(row));
        }
    } else {
        for (const auto &terms : data.twoPortTerms) {
            Json row = Json::MakeArray();
            // 与原程序 n×n 接收端/激励端矩阵完全相同的系数顺序。
            AppendReflection(row, terms.forwardReflection);
            AppendTransmission(row, terms.reverseTransmission);
            AppendTransmission(row, terms.forwardTransmission);
            AppendReflection(row, terms.reverseReflection);
            values.array.push_back(std::move(row));
        }
    }
    return values;
}

void ImportAxis(const Json &document, VnaCalibration::CalibrationData &data)
{
    const auto &axis = Field(document, "frequencyHz", Json::Type::ARRAY).array;
    if (axis.size() < VnaService::MIN_SWEEP_POINTS || axis.size() > VnaService::MAX_SWEEP_POINTS) {
        throw std::invalid_argument("Calibration frequency axis exceeds supported point count");
    }
    for (const auto &point : axis) {
        if (point.type != Json::Type::NUMBER || !std::isfinite(point.number)) {
            throw std::invalid_argument("Calibration frequency axis must be finite numbers");
        }
        data.frequencyHz.push_back(point.number);
    }
}

void ImportPower(const Json &document, double schema, VnaService::CalibrationSet &set, std::size_t count)
{
    const auto &power = Field(document, schema == 1.0 ? "powerRaw" : "powerDbm", Json::Type::ARRAY).array;
    if (power.size() != count) {
        throw std::invalid_argument("Calibration power and frequency lengths differ");
    }
    for (const auto &point : power) {
        if (schema == 1.0) {
            // 旧 uint64 ABI 的全零位型与 double 的 0 dBm 相同，其余编码不能推断为物理功率。
            if (point.type != Json::Type::STRING || point.string != "0") {
                throw std::invalid_argument(
                    "Legacy calibration power encoding is incompatible; recalibration required");
            }
            set.powerDbm.push_back(0.0);
        } else {
            if (point.type != Json::Type::NUMBER || !PowerValid(point.number)) {
                throw std::invalid_argument("Calibration powerDbm must be finite numbers in [-120, 100]");
            }
            set.powerDbm.push_back(point.number);
        }
    }
}

void ImportTermRow(const Json &row, VnaCalibration::CalibrationData &data, bool reflection)
{
    if (row.type != Json::Type::ARRAY || row.array.size() != (reflection ? 6U : 24U)) {
        throw std::invalid_argument("Invalid calibration coefficient row length");
    }
    if (reflection) {
        data.onePortTerms.push_back(ReflectionAt(row, 0U));
    } else {
        VnaCalibration::TwoPortTerms terms;
        terms.forwardReflection = ReflectionAt(row, 0U);
        terms.reverseTransmission = TransmissionAt(row, 6U);
        terms.forwardTransmission = TransmissionAt(row, 12U);
        terms.reverseReflection = ReflectionAt(row, 18U);
        data.twoPortTerms.push_back(std::move(terms));
    }
}

void ImportTerms(const Json &document, VnaCalibration::CalibrationData &data)
{
    const auto &one = Field(document, "onePortTerms", Json::Type::ARRAY).array;
    const auto &two = Field(document, "twoPortTerms", Json::Type::ARRAY).array;
    const bool reflection = data.method == VnaCalibration::CalibrationMethod::ONE_PORT;
    const auto count = data.frequencyHz.size();
    if ((reflection && (one.size() != count || !two.empty())) ||
        (!reflection && (two.size() != count || !one.empty()))) {
        throw std::invalid_argument("Calibration model and coefficient shapes differ");
    }
    for (const auto &row : reflection ? one : two) {
        ImportTermRow(row, data, reflection);
    }
}
}  // 匿名命名空间

Json CalibrationJson::Export(const VnaService::CalibrationSet &set)
{
    if (!set.data) {
        throw std::invalid_argument("Calibration set has no coefficients");
    }
    const auto &data = *set.data;
    Json value = Json::MakeObject();
    value["schema"] = Json(2U);
    value["id"] = Json(std::to_string(set.id));
    value["name"] = Json(set.name);
    value["deviceIdentity"] = Json(set.deviceIdentity);
    value["ifBandwidthHz"] = Json(set.ifBandwidthHz);
    value["includeIsolation"] = Json(set.includeIsolation);
    value["method"] = Json(MethodName(data.method));
    value["port1"] = Json(data.port1);
    value["port2"] = Json(data.port2);
    Json power = Json::MakeArray();
    for (const double dbm : set.powerDbm) {
        if (!PowerValid(dbm)) {
            throw std::invalid_argument("Cannot export invalid calibration powerDbm");
        }
        power.array.emplace_back(dbm);
    }
    value["powerDbm"] = std::move(power);
    Json frequencies = Json::MakeArray();
    for (const double hz : data.frequencyHz) {
        frequencies.array.emplace_back(hz);
    }
    value["frequencyHz"] = std::move(frequencies);
    value["onePortTerms"] = ExportTerms(data, true);
    value["twoPortTerms"] = ExportTerms(data, false);
    return value;
}

VnaService::CalibrationSetPtr CalibrationJson::Import(const Json &document)
{
    const double schema = Number(document, "schema");
    if (schema != 1.0 && schema != 2.0) {
        throw std::invalid_argument("Unsupported calibration schema");
    }
    Fields(document, {"schema", "id", "name", "deviceIdentity", "ifBandwidthHz", "includeIsolation",
                         schema == 1.0 ? "powerRaw" : "powerDbm", "method", "port1", "port2", "frequencyHz",
                         "onePortTerms", "twoPortTerms"});
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->name = Text(document, "name");
    set->deviceIdentity = Text(document, "deviceIdentity");
    set->ifBandwidthHz = Number(document, "ifBandwidthHz");
    set->includeIsolation = Field(document, "includeIsolation", Json::Type::BOOLEAN).boolean;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = Method(Text(document, "method"));
    data->port1 = Port(document, "port1");
    data->port2 = Port(document, "port2", true);
    ImportAxis(document, *data);
    ImportPower(document, schema, *set, data->frequencyHz.size());
    ImportTerms(document, *data);
    set->data = std::move(data);
    return set;
}
}  // 命名空间 VnaWeb
