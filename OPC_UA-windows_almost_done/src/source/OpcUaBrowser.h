#pragma once

#include <string>
#include <vector>
#include "core/BrowseNode.h"

struct UA_Client;

class OpcUaBrowser {
public:
    std::vector<BrowseNode> browse(UA_Client* client, const std::string& nodeId);
};
