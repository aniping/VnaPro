/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_measurement/measurement.h"

#include <cctype>
#include <utility>

#include "vna_core/frame_layout.h"

namespace VnaMeasurement {
namespace {
std::string TrimParameter(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string()
                                      : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1U);
}

bool Port(const std::string &value, unsigned &port)
{
    const auto text = TrimParameter(value);
    if (text.empty()) {
        return false;
    }
    port = 0U;
    for (const auto digit : text) {
        if (digit < '0' || digit > '9' || port > VnaCore::PHYSICAL_PORT_COUNT) {
            return false;
        }
        port = port * 10U + static_cast<unsigned>(digit - '0');
    }
    return port > 0U && port <= VnaCore::PHYSICAL_PORT_COUNT;
}
}  // 匿名命名空间

std::string Measurement::Parameter() const
{
    if (kind == MeasurementKind::S_PARAMETER) {
        return "S" + std::to_string(receiverPort) + std::to_string(sourcePort);
    }
    return std::string(1U, receiverKind) + std::to_string(receiverPort) + "," + std::to_string(sourcePort);
}

bool Measurement::IsSupported() const noexcept
{
    if (kind == MeasurementKind::S_PARAMETER) {
        return receiverKind == 'b' && sourcePort >= 1U && sourcePort <= VnaCore::PHYSICAL_PORT_COUNT &&
               receiverPort >= 1U && receiverPort <= VnaCore::PHYSICAL_PORT_COUNT;
    }
    return kind == MeasurementKind::RECEIVER && (receiverKind == 'a' || receiverKind == 'b') && receiverPort >= 1U &&
           receiverPort <= VnaCore::PHYSICAL_PORT_COUNT && sourcePort >= 1U &&
           sourcePort <= VnaCore::PHYSICAL_PORT_COUNT;
}

bool Measurement::SetParameter(const std::string &parameter)
{
    const auto text = TrimParameter(parameter);
    auto candidate = *this;
    constexpr char maxPort = static_cast<char>('0' + VnaCore::PHYSICAL_PORT_COUNT);
    if ((text.size() == 3U || (text.size() == 4U && text[2U] == '_')) && text[0U] == 'S' && text[1U] >= '1' &&
        text[1U] <= maxPort && text.back() >= '1' && text.back() <= maxPort) {
        candidate.kind = MeasurementKind::S_PARAMETER;
        candidate.receiverKind = 'b';
        candidate.receiverPort = static_cast<unsigned>(text[1U] - '0');
        candidate.sourcePort = static_cast<unsigned>(text.back() - '0');
    } else {
        const auto comma = text.find(',');
        if (comma == std::string::npos) {
            return false;
        }
        const auto receiver = TrimParameter(text.substr(0U, comma));
        if (receiver.size() != 2U) {
            return false;
        }
        candidate.kind = MeasurementKind::RECEIVER;
        candidate.receiverKind = static_cast<char>(std::tolower(static_cast<unsigned char>(receiver.front())));
        if (!Port(receiver.substr(1U), candidate.receiverPort) ||
            !Port(text.substr(comma + 1U), candidate.sourcePort)) {
            return false;
        }
    }
    if (!candidate.IsSupported()) {
        return false;
    }
    *this = std::move(candidate);
    return true;
}

}  // 命名空间 VnaMeasurement
