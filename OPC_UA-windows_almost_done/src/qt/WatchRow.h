#pragma once

#include <string>

struct WatchRow {
    std::string nodeId;
    std::string logicalName;
    std::string value;
    bool good = false;
    bool hasValue = false;
    bool hasModbusMapping = false;
    std::string modbusPreview;
};
