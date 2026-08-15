#pragma once

#include <string>
#include "core/TagValue.h"

struct Tag {
    std::string logicalName;
    std::string nodeId;
    TagValue value;
};
