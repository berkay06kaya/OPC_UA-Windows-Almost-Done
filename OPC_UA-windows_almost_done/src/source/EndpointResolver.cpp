#include "source/EndpointResolver.h"
#include <iostream>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

static std::string uaToString(const UA_String& uaStr) {
    if (uaStr.length == 0 || !uaStr.data) return "";
    return std::string(reinterpret_cast<char*>(uaStr.data), uaStr.length);
}

std::string EndpointResolver::extractIp(const std::string& url) {
    size_t start = url.find("opc.tcp://");
    if (start == std::string::npos) {
        std::cout << "[IP] extractIp  | HATA: 'opc.tcp://' yok, url='" << url << "'" << std::endl;
        return "";
    }
    start += 10;
    size_t end = url.find(":", start);
    if (end == std::string::npos) end = url.find("/", start);
    if (end == std::string::npos) end = url.length();
    std::string ip = url.substr(start, end - start);
    std::cout << "[IP] extractIp  | girilen url = " << url << "\n"
              << "[IP] extractIp  | ayiklanan IP = " << ip << std::endl;
    return ip;
}

std::string EndpointResolver::fixEndpointUrl(const std::string& brokenUrl, const std::string& realIp) {
    size_t start = brokenUrl.find("opc.tcp://");
    if (start == std::string::npos) {
        std::cout << "[IP] fixUrl     | HATA: 'opc.tcp://' yok, degistirilmedi: " << brokenUrl << std::endl;
        return brokenUrl;
    }
    start += 10;
    size_t end = brokenUrl.find(":", start);
    if (end == std::string::npos) end = brokenUrl.find("/", start);
    if (end == std::string::npos) end = brokenUrl.length();

    std::string host = brokenUrl.substr(start, end - start);
    std::string fixedUrl = brokenUrl;
    fixedUrl.replace(start, end - start, realIp);
    std::cout << "[IP] fixUrl     | sunucunun ilan ettigi host = '" << host << "'"
              << (host == realIp ? "  (zaten IP)" : "  --> '" + realIp + "' ile degistirildi") << "\n"
              << "[IP] fixUrl     | duzeltilmis url = " << fixedUrl << std::endl;
    return fixedUrl;
}

std::vector<EndpointInfo> EndpointResolver::getAvailableEndpoints(const std::string& initialUrl) {
    std::vector<EndpointInfo> endpoints;

    UA_Client* tempClient = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(tempClient));

    UA_EndpointDescription* endpointArray = nullptr;
    size_t endpointArraySize = 0;

    UA_StatusCode retval = UA_Client_getEndpoints(tempClient, initialUrl.c_str(), &endpointArraySize, &endpointArray);
    UA_Client_delete(tempClient);

    if (retval != UA_STATUSCODE_GOOD || endpointArraySize == 0) {
        return endpoints;
    }

    std::cout << "\n========== IP / ENDPOINT COZUMLEME ==========" << std::endl;
    std::string realIp = extractIp(initialUrl);
    std::cout << "[IP] Hedef gercek IP = " << realIp
              << "  | sunucudan " << endpointArraySize << " endpoint geldi" << std::endl;

    for (size_t i = 0; i < endpointArraySize; i++) {
        EndpointInfo info;
        std::string brokenUrl = uaToString(endpointArray[i].endpointUrl);
        std::cout << "[IP] --- endpoint[" << i << "] ---" << std::endl;
        info.endpointUrl = fixEndpointUrl(brokenUrl, realIp);
        info.securityPolicyUri = uaToString(endpointArray[i].securityPolicyUri);
        info.securityMode = endpointArray[i].securityMode;

        if (info.securityMode == UA_MESSAGESECURITYMODE_NONE) info.securityModeString = "None";
        else if (info.securityMode == UA_MESSAGESECURITYMODE_SIGN) info.securityModeString = "Sign";
        else if (info.securityMode == UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) info.securityModeString = "SignAndEncrypt";
        else info.securityModeString = "Unknown";

        endpoints.push_back(info);
    }

    std::cout << "=============================================\n" << std::endl;

    UA_Array_delete(endpointArray, endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
    return endpoints;
}
