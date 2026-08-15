#include "source/UaUtils.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <open62541/types_generated.h>
#include <open62541/types_generated_handling.h>

namespace ua {

std::int64_t nowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
}

UA_ByteString loadFile(const std::string& filename) {
    UA_ByteString str = UA_BYTESTRING_NULL;
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return str;

    str.length = file.tellg();
    str.data = (UA_Byte*)UA_malloc(str.length);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(str.data), str.length);
    return str;
}

UA_ByteString byteStringFromBytes(const unsigned char* data, std::size_t len) {
    UA_ByteString str = UA_BYTESTRING_NULL;
    if (len == 0) return str;
    str.length = len;
    str.data = (UA_Byte*)UA_malloc(len);
    std::memcpy(str.data, data, len);
    return str;
}

std::string hostFromUrl(const std::string& url) {
    size_t start = url.find("opc.tcp://");
    if (start == std::string::npos) return "";
    start += 10;
    size_t end = url.find(":", start);
    if (end == std::string::npos) end = url.find("/", start);
    if (end == std::string::npos) end = url.length();
    return url.substr(start, end - start);
}

std::string variantToString(const UA_Variant& v) {
    if (UA_Variant_isEmpty(&v)) return "(bos)";
    if (!UA_Variant_isScalar(&v)) return "(dizi/yapisal)";
    const UA_UInt16 t = v.type ? v.type->typeKind : 0;
    std::ostringstream os;
    if (v.type == &UA_TYPES[UA_TYPES_BOOLEAN]) os << (*(UA_Boolean*)v.data ? "true" : "false");
    else if (v.type == &UA_TYPES[UA_TYPES_SBYTE])   os << (int)*(UA_SByte*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_BYTE])    os << (unsigned)*(UA_Byte*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_INT16])   os << *(UA_Int16*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_UINT16])  os << *(UA_UInt16*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_INT32])   os << *(UA_Int32*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_UINT32])  os << *(UA_UInt32*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_INT64])   os << *(UA_Int64*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_UINT64])  os << *(UA_UInt64*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_FLOAT])   os << *(UA_Float*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_DOUBLE])  os << *(UA_Double*)v.data;
    else if (v.type == &UA_TYPES[UA_TYPES_STRING]) {
        const UA_String* s = (UA_String*)v.data;
        os << std::string(reinterpret_cast<char*>(s->data), s->length);
    } else {
        os << "[tip:" << t << "]";
    }
    return os.str();
}

TagValue variantToTagValue(const UA_Variant& v) {
    TagValue tv;
    if (UA_Variant_isEmpty(&v))
        return tv;

    if (!UA_Variant_isScalar(&v)) {
        tv.value = variantToString(v);
        return tv;
    }

    if (v.type == &UA_TYPES[UA_TYPES_BOOLEAN])      tv.value = (bool)(*(UA_Boolean*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_SBYTE])   tv.value = (std::int64_t)(*(UA_SByte*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_BYTE])    tv.value = (std::uint64_t)(*(UA_Byte*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_INT16])   tv.value = (std::int64_t)(*(UA_Int16*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_UINT16])  tv.value = (std::uint64_t)(*(UA_UInt16*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_INT32])   tv.value = (std::int64_t)(*(UA_Int32*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_UINT32])  tv.value = (std::uint64_t)(*(UA_UInt32*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_INT64])   tv.value = (std::int64_t)(*(UA_Int64*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_UINT64])  tv.value = (std::uint64_t)(*(UA_UInt64*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_FLOAT])   tv.value = (double)(*(UA_Float*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_DOUBLE])  tv.value = (double)(*(UA_Double*)v.data);
    else if (v.type == &UA_TYPES[UA_TYPES_STRING]) {
        const UA_String* s = (UA_String*)v.data;
        tv.value = std::string(reinterpret_cast<char*>(s->data), s->length);
    } else {
        tv.value = variantToString(v);
    }
    return tv;
}

}
