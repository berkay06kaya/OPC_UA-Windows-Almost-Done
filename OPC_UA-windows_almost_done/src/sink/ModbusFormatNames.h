#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/ModbusMapping.h"

std::vector<std::pair<ModbusMapping::Format, std::string>> modbusFormatNames();
std::vector<std::pair<ModbusMapping::RegisterType, std::string>> modbusRegisterTypeNames();
int modbusFixedRegisterCount(ModbusMapping::Format format);
bool modbusFormatIsPlainInteger(ModbusMapping::Format format);
