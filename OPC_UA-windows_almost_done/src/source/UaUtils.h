#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <open62541/types.h>
#include "core/TagValue.h"

namespace ua {

UA_ByteString loadFile(const std::string& filename);
UA_ByteString byteStringFromBytes(const unsigned char* data, std::size_t len);
std::string hostFromUrl(const std::string& url);
std::string variantToString(const UA_Variant& v);
TagValue variantToTagValue(const UA_Variant& v);

std::int64_t nowMs();

}
