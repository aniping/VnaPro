#ifndef VNAPRO_SCPI_PARSER_H
#define VNAPRO_SCPI_PARSER_H
#include <string>
#include <vector>

#include "scpi/parsed_command.h"

namespace vna_scpi {
std::vector<std::string> splitProgram(const std::string &input);
ParsedCommand parseCommand(const std::string &unit, std::vector<std::string> &parent);
std::string upper(std::string value);
std::string trim(const std::string &value);
std::string quote(const std::string &value);
bool numeric(const std::string &text, double &value, bool frequencyUnits = false);
bool integer(const std::string &text, unsigned &value, unsigned maximum);
}  // namespace vna_scpi
#endif
