#pragma once

#include <string>
#include "core/TagValue.h"

template <typename Out>
class IConverter {
public:
    virtual ~IConverter() = default;
    virtual Out convert(const std::string& nodeId, const TagValue& in) const = 0;
};
