#include "scpi/ScpiParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace vna_scpi {
std::string trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1U);
}

std::string upper(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string quote(const std::string &value)
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

namespace {
std::vector<std::string> splitQuoted(const std::string &input, char separator, bool *valid)
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
                    part += input[++index];
                } else {
                    delimiter = 0;
                }
            }
        } else if (character == '\'' || character == '"') {
            delimiter = character;
            part += character;
        } else if (character == separator) {
            parts.push_back(trim(part));
            part.clear();
        } else {
            part += character;
        }
    }
    parts.push_back(trim(part));
    if (valid != nullptr) {
        *valid = delimiter == 0;
    }
    return parts;
}

std::string keyword(std::string token)
{
    token = upper(token);
    std::string suffix;
    while (!token.empty() && std::isdigit(static_cast<unsigned char>(token.back()))) {
        suffix.insert(suffix.begin(), token.back());
        token.pop_back();
    }
    const std::map<std::string, std::string> names = {{"SENSE", "SENS"}, {"FREQUENCY", "FREQ"}, {"START", "STAR"},
        {"SWEEP", "SWE"}, {"POINTS", "POIN"}, {"BANDWIDTH", "BAND"}, {"BWIDTH", "BWID"}, {"RESOLUTION", "RES"},
        {"TRIGGER", "TRIG"}, {"SEQUENCE", "SEQ"}, {"SOURCE", "SOUR"}, {"SCOPE", "SCOP"}, {"INITIATE", "INIT"},
        {"CONTINUOUS", "CONT"}, {"IMMEDIATE", "IMM"}, {"CALCULATE", "CALC"}, {"PARAMETER", "PAR"}, {"DEFINE", "DEF"},
        {"EXTENDED", "EXT"}, {"SELECT", "SEL"}, {"CATALOG", "CAT"}, {"DELETE", "DEL"}, {"FORMAT", "FORM"},
        {"VALUES", "VAL"}, {"SYSTEM", "SYST"}, {"ERROR", "ERR"}, {"COUNT", "COUN"}};
    const auto found = names.find(token);
    if (found != names.end()) {
        token = found->second;
    }
    if (suffix.empty() && (token == "SENS" || token == "CALC")) {
        suffix = "1";
    }
    return token + suffix;
}
}  // namespace

std::vector<std::string> splitProgram(const std::string &input)
{
    return splitQuoted(input, ';', nullptr);
}

ParsedCommand parseCommand(const std::string &unit, std::vector<std::string> &parent)
{
    ParsedCommand command;
    const std::string text = trim(unit);
    const auto space = text.find_first_of(" \t");
    std::string header = text.substr(0, space);
    const std::string parameters = space == std::string::npos ? std::string() : trim(text.substr(space));
    if (header.empty()) {
        command.errorCode = -102;
        command.error = "Empty program unit";
        return command;
    }
    if (header.back() == '?') {
        command.query = true;
        header.pop_back();
    }
    if (header.empty()) {
        command.errorCode = -102;
        command.error = "Missing query header";
        return command;
    }
    if (header.find('?') != std::string::npos) {
        command.errorCode = -102;
        command.error = "Invalid query header";
        return command;
    }
    if (header.front() == '*') {
        command.path = upper(header);
    } else {
        if (header.back() == ':') {
            command.errorCode = -102;
            command.error = "Trailing header separator";
            return command;
        }
        const bool absolute = header.front() == ':';
        if (absolute) {
            header.erase(header.begin());
        }
        std::vector<std::string> tokens = absolute ? std::vector<std::string>() : parent;
        std::stringstream path(header);
        std::string token;
        while (std::getline(path, token, ':')) {
            if (token.empty()) {
                command.errorCode = -102;
                command.error = "Empty header keyword";
                return command;
            }
            tokens.push_back(keyword(token));
        }
        if (tokens.empty()) {
            command.errorCode = -102;
            command.error = "Missing header";
            return command;
        }
        parent = tokens;
        parent.pop_back();
        for (const auto &item : tokens) {
            if (!command.path.empty()) {
                command.path += ':';
            }
            command.path += item;
        }
    }
    if (!parameters.empty()) {
        bool valid = false;
        command.arguments = splitQuoted(parameters, ',', &valid);
        if (!valid) {
            command.errorCode = -102;
            command.error = "Unterminated string";
            return command;
        }
    }
    return command;
}

bool numeric(const std::string &text, double &value, bool frequencyUnits)
{
    const std::string input = trim(text);
    if (input.empty()) {
        return false;
    }
    char *end = nullptr;
    value = std::strtod(input.c_str(), &end);
    if (end == input.c_str() || !std::isfinite(value)) {
        return false;
    }
    const std::size_t parsedLength = static_cast<std::size_t>(end - input.c_str());
    const std::string unit = upper(trim(input.substr(parsedLength)));
    double scale = 1.0;
    if (!unit.empty()) {
        if (!frequencyUnits) {
            return false;
        }
        if (unit == "HZ") {
            scale = 1.0;
        } else if (unit == "KHZ") {
            scale = 1000.0;
        } else if (unit == "MHZ") {
            scale = 1000000.0;
        } else if (unit == "GHZ") {
            scale = 1000000000.0;
        } else {
            return false;
        }
    }
    value *= scale;
    return std::isfinite(value);
}

bool integer(const std::string &text, unsigned &value, unsigned maximum)
{
    double parsed = 0;
    if (!numeric(text, parsed) || parsed < 0.0 || parsed > maximum || std::floor(parsed) != parsed) {
        return false;
    }
    value = static_cast<unsigned>(parsed);
    return true;
}
}  // namespace vna_scpi
