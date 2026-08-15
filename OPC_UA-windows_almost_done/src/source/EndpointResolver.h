#pragma once
#include <string>
#include <vector>
#include "core/EndpointInfo.h"

class EndpointResolver {
private:
    std::string extractIp(const std::string& url);
    std::string fixEndpointUrl(const std::string& brokenUrl, const std::string& realIp);

public:
    std::vector<EndpointInfo> getAvailableEndpoints(const std::string& initialUrl);
};
