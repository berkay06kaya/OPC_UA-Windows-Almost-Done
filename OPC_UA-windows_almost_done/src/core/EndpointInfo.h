#pragma once
#include <string>

struct EndpointInfo {
    std::string endpointUrl;
    std::string securityPolicyUri;
    int securityMode;
    std::string securityModeString;

    std::string getShortSecurityPolicy() const {
        size_t pos = securityPolicyUri.find_last_of('#');
        if (pos != std::string::npos) {
            return securityPolicyUri.substr(pos + 1);
        }
        return securityPolicyUri;
    }

    bool isSupportedByClient() const {
        const std::string p = getShortSecurityPolicy();
        return p == "None" || p == "Basic128Rsa15" || p == "Basic256" ||
               p == "Basic256Sha256" || p == "Aes128_Sha256_RsaOaep";
    }
};
