/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_parser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>

#include "scpi/scpi_constants.h"

namespace VnaScpi {
std::string Trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1U);
}

std::string Upper(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string Quote(const std::string &value)
{
    std::string encoded = "\"";
    for (char character : value) {
        encoded += character;
        if (character == '"') {
            encoded += character;
        }
    }
    return encoded + "\"";
}

bool StringArgument(const std::string &text, std::string &value)
{
    if (text.size() < 2U || (text.front() != '"' && text.front() != '\'') || text.back() != text.front()) {
        return false;
    }
    value.clear();
    for (std::size_t index = 1U; index + 1U < text.size(); ++index) {
        if (text[index] == text.front()) {
            if (index + 2U >= text.size() || text[index + 1U] != text.front()) {
                return false;
            }
            ++index;
        }
        value += text[index];
    }
    return true;
}

namespace {
constexpr double KILOHERTZ_SCALE = 1000.0;
constexpr double MEGAHERTZ_SCALE = 1000000.0;
constexpr double GIGAHERTZ_SCALE = 1000000000.0;

std::vector<std::string> SplitQuoted(const std::string &input, char separator, bool &valid)
{
    std::vector<std::string> parts;
    std::string part;
    char delimiter = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const char character = input[index];
        if (delimiter != 0) {
            part += character;
            if (character == delimiter) {
                if (index + 1U < input.size() && input[index + 1U] == delimiter) {
                    ++index;
                    part += input[index];
                } else {
                    delimiter = 0;
                }
            }
        } else if (character == '\'' || character == '"') {
            delimiter = character;
            part += character;
        } else if (character == separator) {
            parts.push_back(Trim(part));
            part.clear();
        } else {
            part += character;
        }
    }
    parts.push_back(Trim(part));
    valid = delimiter == 0;
    return parts;
}

std::string Keyword(std::string token)
{
    token = Upper(token);
    std::string suffix;
    while (!token.empty() && std::isdigit(static_cast<unsigned char>(token.back()))) {
        suffix.insert(suffix.begin(), token.back());
        token.pop_back();
    }
    static const std::map<std::string, std::string> names = {{"SENSE", "SENS"}, {"FREQUENCY", "FREQ"},
        {"START", "STAR"}, {"SWEEP", "SWE"}, {"POINTS", "POIN"}, {"BANDWIDTH", "BAND"}, {"BWIDTH", "BWID"},
        {"RESOLUTION", "RES"}, {"TRIGGER", "TRIG"}, {"SEQUENCE", "SEQ"}, {"SOURCE", "SOUR"}, {"SCOPE", "SCOP"},
        {"INITIATE", "INIT"}, {"CONTINUOUS", "CONT"}, {"IMMEDIATE", "IMM"}, {"CALCULATE", "CALC"}, {"PARAMETER", "PAR"},
        {"DEFINE", "DEF"}, {"EXTENDED", "EXT"}, {"SELECT", "SEL"}, {"CATALOG", "CAT"}, {"DELETE", "DEL"},
        {"FORMAT", "FORM"}, {"VALUES", "VAL"}, {"SYSTEM", "SYST"}, {"ERROR", "ERR"}, {"COUNT", "COUN"},
        {"DISPLAY", "DISP"}, {"WINDOW", "WIND"}, {"TRACE", "TRAC"}, {"SCALE", "SCAL"}, {"PDIVISION", "PDIV"},
        {"RLEVEL", "RLEV"}, {"RPOSITION", "RPOS"}, {"MARKER", "MARK"}, {"STATE", "STAT"}, {"DISCRETE", "DISC"},
        {"CORRECTION", "CORR"}, {"COLLECTION", "COLL"}, {"COLLECT", "COLL"}, {"METHOD", "METH"}, {"ACQUIRE", "ACQ"},
        {"ABORT", "ABOR"}, {"ACTIVE", "ACT"}, {"POWER", "POW"}, {"LEVEL", "LEV"}, {"AMPLITUDE", "AMPL"},
        {"BORDER", "BORD"}};
    const auto found = names.find(token);
    if (found != names.end()) {
        token = found->second;
    }
    if (suffix.empty() && (token == "SENS" || token == "CALC" || token == "WIND" || token == "TRAC")) {
        suffix = "1";
    }
    return token + suffix;
}

bool ParseHeader(const std::string &header, std::vector<std::string> &parent, ParsedCommand &command)
{
    if (header.back() == ':') {
        command.errorCode = SYNTAX_ERROR;
        command.error = "Trailing header separator";
        return false;
    }
    const bool absolute = header.front() == ':';
    std::vector<std::string> tokens = absolute ? std::vector<std::string>() : parent;
    std::stringstream path(absolute ? header.substr(1) : header);
    std::string token;
    while (std::getline(path, token, ':')) {
        if (token.empty()) {
            command.errorCode = SYNTAX_ERROR;
            command.error = "Empty header keyword";
            return false;
        }
        tokens.push_back(Keyword(token));
    }
    if (tokens.empty()) {
        command.errorCode = SYNTAX_ERROR;
        command.error = "Missing header";
        return false;
    }
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        // AOFF 属于标记组；只有普通标记路径省略编号时才采用默认 M1。
        if (tokens[index] == "MARK" && (index + 1U == tokens.size() || tokens[index + 1U] != "AOFF")) {
            tokens[index] = "MARK1";
        }
    }
    parent = tokens;
    parent.pop_back();
    for (const auto &item : tokens) {
        if (!command.path.empty()) {
            command.path += ':';
        }
        command.path += item;
    }
    return true;
}
}  // 匿名命名空间

std::vector<std::string> SplitProgram(const std::string &input)
{
    bool valid = false;
    return SplitQuoted(input, ';', valid);
}

ParsedCommand ParseCommand(const std::string &unit, std::vector<std::string> &parent)
{
    ParsedCommand command;
    const std::string text = Trim(unit);
    const auto space = text.find_first_of(" \t");
    std::string header = text.substr(0, space);
    const std::string parameters = space == std::string::npos ? std::string() : Trim(text.substr(space));
    if (header.empty()) {
        command.errorCode = SYNTAX_ERROR;
        command.error = "Empty program unit";
        return command;
    }
    if (header.back() == '?') {
        command.query = true;
        header.pop_back();
    }
    if (header.empty()) {
        command.errorCode = SYNTAX_ERROR;
        command.error = "Missing query header";
        return command;
    }
    if (header.find('?') != std::string::npos) {
        command.errorCode = SYNTAX_ERROR;
        command.error = "Invalid query header";
        return command;
    }
    if (header.front() == '*') {
        command.path = Upper(header);
    } else if (!ParseHeader(header, parent, command)) {
        return command;
    }
    if (!parameters.empty()) {
        bool valid = false;
        command.arguments = SplitQuoted(parameters, ',', valid);
        if (!valid) {
            command.errorCode = SYNTAX_ERROR;
            command.error = "Unterminated string";
            return command;
        }
    }
    return command;
}

ParsedCommand RouteChannelCommand(const ParsedCommand &command)
{
    auto routed = command;
    if (routed.errorCode != 0 || routed.channelId != 0U) {
        return routed;
    }
    const auto separator = routed.path.find(':');
    const auto root = routed.path.substr(0U, separator);
    for (const std::string prefix : {"SENS", "CALC", "INIT", "SOUR"}) {
        if (root.compare(0U, prefix.size(), prefix) != 0) {
            continue;
        }
        const auto suffix = root.substr(prefix.size());
        if (suffix.empty() && prefix == "SOUR") {
            routed.channelId = 1U;
            routed.path = "SOUR1" + (separator == std::string::npos ? "" : routed.path.substr(separator));
            return routed;
        }
        if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos) {
            return routed;
        }
        if (!Integer(suffix, routed.channelId, (std::numeric_limits<unsigned>::max)()) || routed.channelId == 0U) {
            routed.errorCode = DATA_OUT_OF_RANGE;
            routed.error = "Channel suffix must be a supported positive integer";
            return routed;
        }
        routed.path = prefix + "1" + (separator == std::string::npos ? "" : routed.path.substr(separator));
        return routed;
    }
    return routed;
}

bool Numeric(const std::string &text, double &value, bool frequencyUnits)
{
    const std::string input = Trim(text);
    if (input.empty()) {
        return false;
    }
    char *end = nullptr;
    value = std::strtod(input.c_str(), &end);
    if (end == input.c_str() || !std::isfinite(value)) {
        return false;
    }
    const std::size_t parsedLength = static_cast<std::size_t>(end - input.c_str());
    if (input.substr(0U, parsedLength).find_first_not_of("0123456789+-.eE") != std::string::npos) {
        return false;
    }
    const std::string unit = Upper(Trim(input.substr(parsedLength)));
    double scale = 1.0;
    if (!unit.empty()) {
        if (!frequencyUnits) {
            return false;
        }
        if (unit == "HZ") {
            scale = 1.0;
        } else if (unit == "KHZ") {
            scale = KILOHERTZ_SCALE;
        } else if (unit == "MHZ") {
            scale = MEGAHERTZ_SCALE;
        } else if (unit == "GHZ") {
            scale = GIGAHERTZ_SCALE;
        } else {
            return false;
        }
    }
    value *= scale;
    return std::isfinite(value);
}

bool Integer(const std::string &text, unsigned &value, unsigned maximum)
{
    double parsed = 0;
    if (!Numeric(text, parsed) || parsed < 0.0 || parsed > maximum || std::floor(parsed) != parsed) {
        return false;
    }
    value = static_cast<unsigned>(parsed);
    return true;
}
}  // 命名空间 VnaScpi
