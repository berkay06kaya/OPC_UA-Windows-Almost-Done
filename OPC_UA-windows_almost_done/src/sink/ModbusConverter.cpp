#include "sink/ModbusConverter.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <variant>

#include "sink/ModbusFormatNames.h"

namespace {

using Format = ModbusMapping::Format;

int mappingWidth(const ModbusMapping& m) {
    return std::max(1, m.registerCount);
}

bool usesInputBitsSlot(ModbusMapping::RegisterType t) {
    using RegisterType = ModbusMapping::RegisterType;
    return t == RegisterType::HoldingRegister
        || t == RegisterType::InputRegister
        || t == RegisterType::DiscreteInput;
}

double asDouble(const TagValue& v) {
    return std::visit([](auto&& x) -> double {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::string>)
            return 0.0;
        else
            return static_cast<double>(x);
    }, v.value);
}

std::int64_t asInt64(const TagValue& v) {
    return std::visit([](auto&& x) -> std::int64_t {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::string>)
            return 0;
        else
            return static_cast<std::int64_t>(x);
    }, v.value);
}

std::uint64_t asUInt64(const TagValue& v) {
    return std::visit([](auto&& x) -> std::uint64_t {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::string>)
            return 0;
        else
            return static_cast<std::uint64_t>(x);
    }, v.value);
}

std::string parseableText(const TagValue& v) {
    if (auto* s = std::get_if<std::string>(&v.value)) return *s;
    return v.toString();
}

std::int64_t parseOrInt64(const TagValue& v) {
    if (auto* s = std::get_if<std::string>(&v.value)) {
        try { return std::stoll(*s); } catch (...) { return 0; }
    }
    return asInt64(v);
}

std::uint64_t bitsOfUInt32(const TagValue& v) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(asUInt64(v)));
}
std::uint64_t bitsOfInt32(const TagValue& v) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(asInt64(v))));
}
std::uint64_t bitsOfFloat(const TagValue& v) {
    float f = static_cast<float>(asDouble(v));
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
std::uint64_t bitsOfDouble(const TagValue& v) {
    double d = asDouble(v);
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}
std::uint64_t bitsOfInt64(const TagValue& v) {
    return static_cast<std::uint64_t>(asInt64(v));
}
std::uint64_t bitsOfUInt64(const TagValue& v) {
    return asUInt64(v);
}



std::vector<std::uint16_t> packWords(std::uint64_t rawBigEndianValue, int byteCount,
                                      bool wordSwap, bool byteSwap) {
    const int wordCount = byteCount / 2;
    std::vector<std::uint16_t> words;
    words.reserve(static_cast<std::size_t>(wordCount));
    for (int j = 0; j < wordCount; ++j) {
        const int shift = 8 * (byteCount - 2 * (j + 1));
        std::uint16_t word = static_cast<std::uint16_t>((rawBigEndianValue >> shift) & 0xFFFFu);
        if (byteSwap) word = static_cast<std::uint16_t>((word << 8) | (word >> 8));
        words.push_back(word);
    }
    if (wordSwap) std::reverse(words.begin(), words.end());
    return words;
}

std::vector<std::uint16_t> packInt16Family(std::uint64_t fullRaw, int registerCount,
                                            bool wordSwap, bool byteSwap) {
    return packWords(fullRaw, std::max(1, registerCount) * 2, wordSwap, byteSwap);
}
std::vector<std::uint16_t> packInt32Family(std::uint64_t narrowRaw, std::uint64_t wideRaw, int registerCount,
                                            bool wordSwap, bool byteSwap) {
    return registerCount > 2 ? packWords(wideRaw, 8, wordSwap, byteSwap)
                             : packWords(narrowRaw, 4, wordSwap, byteSwap);
}

std::uint32_t bcdEncode32(std::uint64_t value) {
    value %= 100000000ULL;
    std::uint32_t result = 0;
    for (int digit = 0; digit < 8; ++digit) {
        result |= static_cast<std::uint32_t>(value % 10) << (4 * digit);
        value /= 10;
    }
    return result;
}

std::vector<std::uint16_t> packText(const std::string& s, int registerCount) {
    const int n = std::max(0, registerCount);
    std::vector<std::uint16_t> out(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const char c0 = (2 * i < static_cast<int>(s.size())) ? s[static_cast<std::size_t>(2 * i)] : '\0';
        const char c1 = (2 * i + 1 < static_cast<int>(s.size())) ? s[static_cast<std::size_t>(2 * i + 1)] : '\0';
        out[static_cast<std::size_t>(i)] =
            static_cast<std::uint16_t>((static_cast<unsigned char>(c0) << 8) | static_cast<unsigned char>(c1));
    }
    return out;
}

std::string formatGmt(std::int64_t timestampMs) {
    const std::time_t t = static_cast<std::time_t>(timestampMs / 1000);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

}

std::vector<std::uint16_t> ModbusConverter::convert(const std::string& nodeId, const TagValue& in) const {
    ModbusMapping mapping;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_mappings.find(nodeId);
        if (it == m_mappings.end()) return {};
        mapping = it->second;
    }

    switch (mapping.format) {
        case Format::UINT16:
        case Format::INT16:
            return packInt16Family(static_cast<std::uint64_t>(asInt64(in)), mapping.registerCount, false, false);

        case Format::UINT_ABCD: return packInt32Family(bitsOfUInt32(in), bitsOfUInt64(in), mapping.registerCount, false, false);
        case Format::UINT_DCBA: return packInt32Family(bitsOfUInt32(in), bitsOfUInt64(in), mapping.registerCount, true, true);
        case Format::UINT_CDAB: return packInt32Family(bitsOfUInt32(in), bitsOfUInt64(in), mapping.registerCount, true, false);
        case Format::UINT_BADC: return packInt32Family(bitsOfUInt32(in), bitsOfUInt64(in), mapping.registerCount, false, true);

        case Format::INT_ABCD: return packInt32Family(bitsOfInt32(in), bitsOfInt64(in), mapping.registerCount, false, false);
        case Format::INT_DCBA: return packInt32Family(bitsOfInt32(in), bitsOfInt64(in), mapping.registerCount, true, true);
        case Format::INT_CDAB: return packInt32Family(bitsOfInt32(in), bitsOfInt64(in), mapping.registerCount, true, false);

        case Format::FLOAT_ABCD: return packWords(bitsOfFloat(in), 4, false, false);
        case Format::FLOAT_DCBA: return packWords(bitsOfFloat(in), 4, true, true);
        case Format::FLOAT_CDAB: return packWords(bitsOfFloat(in), 4, true, false);
        case Format::FLOAT_BADC: return packWords(bitsOfFloat(in), 4, false, true);

        case Format::DOUBLE_ABCDEFGH: return packWords(bitsOfDouble(in), 8, false, false);
        case Format::DOUBLE_HGFEDCBA: return packWords(bitsOfDouble(in), 8, true, true);
        case Format::DOUBLE_BADCFEHG: return packWords(bitsOfDouble(in), 8, false, true);
        case Format::DOUBLE_GHEFCDAB: return packWords(bitsOfDouble(in), 8, true, false);

        case Format::INT64_ABCDEFGH: return packWords(bitsOfInt64(in), 8, false, false);
        case Format::INT64_HGFEDCBA: return packWords(bitsOfInt64(in), 8, true, true);
        case Format::INT64_BADCFEHG: return packWords(bitsOfInt64(in), 8, false, true);
        case Format::INT64_GHEFCDAB: return packWords(bitsOfInt64(in), 8, true, false);

        case Format::UINT64_ABCDEFGH: return packWords(bitsOfUInt64(in), 8, false, false);
        case Format::UINT64_HGFEDCBA: return packWords(bitsOfUInt64(in), 8, true, true);
        case Format::UINT64_BADCFEHG: return packWords(bitsOfUInt64(in), 8, false, true);
        case Format::UINT64_GHEFCDAB: return packWords(bitsOfUInt64(in), 8, true, false);

        case Format::UINT_BCD_ABCD:
            return packWords(bcdEncode32(asUInt64(in)), 4, false, false);
        case Format::UINT_BCD_CDAB:
            return packWords(bcdEncode32(asUInt64(in)), 4, true, false);

        case Format::UINT32_MODICON:
            return packWords(bitsOfUInt32(in), 4, true, false);

        case Format::INT_TEXT_TO_NUMBER:
            return packWords(
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(parseOrInt64(in)))),
                4, false, false);

        case Format::TEXT_GMT:
            return packText(formatGmt(in.timestampMs), mapping.registerCount);

        case Format::TEXT:
            return packText(parseableText(in), mapping.registerCount);
    }

    return {};
}

void ModbusConverter::setMapping(const std::string& nodeId, const ModbusMapping& mapping) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_mappings[nodeId] = mapping;
}

void ModbusConverter::removeMapping(const std::string& nodeId) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_mappings.erase(nodeId);
}

bool ModbusConverter::hasMapping(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_mappings.find(nodeId) != m_mappings.end();
}

ModbusMapping ModbusConverter::mappingFor(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_mappings.find(nodeId);
    return it != m_mappings.end() ? it->second : ModbusMapping{};
}

std::string ModbusConverter::collidingNodeId(const std::string& nodeId,
                                              const ModbusMapping& mapping) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    const int newStart = mapping.registerAddress;
    const int newEnd = newStart + mappingWidth(mapping);
    for (const auto& kv : m_mappings) {
        if (kv.first == nodeId) continue;
        const ModbusMapping& other = kv.second;
        if (other.registerType == mapping.registerType) {
            const int otherStart = other.registerAddress;
            const int otherEnd = otherStart + mappingWidth(other);
            if (newStart < otherEnd && otherStart < newEnd) return kv.first;
        } else if (usesInputBitsSlot(mapping.registerType) && usesInputBitsSlot(other.registerType)
                   && mapping.registerAddress == other.registerAddress) {
            return kv.first;
        }
    }
    return {};
}

std::unordered_map<std::string, ModbusMapping> ModbusConverter::allMappings() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_mappings;
}
