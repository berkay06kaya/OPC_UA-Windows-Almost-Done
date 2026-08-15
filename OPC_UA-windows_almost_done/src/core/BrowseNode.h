#pragma once
#include <string>

struct BrowseNode {
    std::string nodeId;
    std::string browseName;
    std::string displayName;

    enum class Class { Object, Variable, Method, Other };
    Class nodeClass = Class::Other;

    bool expandable = false;
};
