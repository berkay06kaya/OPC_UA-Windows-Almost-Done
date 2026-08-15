#pragma once

#include <string>
#include <variant>
#include <cstdint>
#include <type_traits>

enum class Quality { Good, Bad, Uncertain };

struct TagValue {
    using Value = std::variant<std::monostate, bool, std::int64_t, std::uint64_t,
                               double, std::string>;

    Value value;
    Quality quality = Quality::Uncertain;
    std::int64_t timestampMs = 0;

    bool hasValue() const { return value.index() != 0; }

    std::string toString() const {
        return std::visit([](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "(bos)";
            else if constexpr (std::is_same_v<T, bool>)        return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return v;
            else return std::to_string(v);
        }, value);
    }
};
