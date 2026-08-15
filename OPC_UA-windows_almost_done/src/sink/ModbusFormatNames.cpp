#include "sink/ModbusFormatNames.h"

std::vector<std::pair<ModbusMapping::Format, std::string>> modbusFormatNames() {
    using Format = ModbusMapping::Format;
    return {
        {Format::UINT16, "UINT16"},
        {Format::INT16, "INT16"},

        {Format::UINT_ABCD, "UINT32 (ABCD)"},
        {Format::UINT_DCBA, "UINT32 (DCBA)"},
        {Format::UINT_CDAB, "UINT32 (CDAB)"},
        {Format::UINT_BADC, "UINT32 (BADC)"},

        {Format::INT_ABCD, "INT32 (ABCD)"},
        {Format::INT_DCBA, "INT32 (DCBA)"},
        {Format::INT_CDAB, "INT32 (CDAB)"},

        {Format::FLOAT_ABCD, "FLOAT (ABCD)"},
        {Format::FLOAT_DCBA, "FLOAT (DCBA)"},
        {Format::FLOAT_CDAB, "FLOAT (CDAB)"},
        {Format::FLOAT_BADC, "FLOAT (BADC)"},

        {Format::DOUBLE_ABCDEFGH, "DOUBLE (ABCDEFGH)"},
        {Format::DOUBLE_HGFEDCBA, "DOUBLE (HGFEDCBA)"},
        {Format::DOUBLE_BADCFEHG, "DOUBLE (BADCFEHG)"},
        {Format::DOUBLE_GHEFCDAB, "DOUBLE (GHEFCDAB)"},

        {Format::INT64_ABCDEFGH, "INT64 (ABCDEFGH)"},
        {Format::INT64_HGFEDCBA, "INT64 (HGFEDCBA)"},
        {Format::INT64_BADCFEHG, "INT64 (BADCFEHG)"},
        {Format::INT64_GHEFCDAB, "INT64 (GHEFCDAB)"},

        {Format::UINT64_ABCDEFGH, "UINT64 (ABCDEFGH)"},
        {Format::UINT64_HGFEDCBA, "UINT64 (HGFEDCBA)"},
        {Format::UINT64_BADCFEHG, "UINT64 (BADCFEHG)"},
        {Format::UINT64_GHEFCDAB, "UINT64 (GHEFCDAB)"},

        {Format::UINT_BCD_ABCD, "BCD UINT32 (ABCD)"},
        {Format::UINT_BCD_CDAB, "BCD UINT32 (CDAB)"},
        {Format::UINT32_MODICON, "UINT32 (Modicon)"},
        {Format::INT_TEXT_TO_NUMBER, "Metin -> INT32"},
        {Format::TEXT_GMT, "Metin (GMT zaman damgasi)"},

        {Format::TEXT, "Metin"},
    };
}

std::vector<std::pair<ModbusMapping::RegisterType, std::string>> modbusRegisterTypeNames() {
    using RegisterType = ModbusMapping::RegisterType;
    return {
        {RegisterType::HoldingRegister, "Holding Register"},
        {RegisterType::InputRegister, "Input Register"},
        {RegisterType::Coil, "Coil"},
        {RegisterType::DiscreteInput, "Discrete Input"},
    };
}

int modbusFixedRegisterCount(ModbusMapping::Format format) {
    using Format = ModbusMapping::Format;
    switch (format) {
        case Format::UINT16:
        case Format::INT16:
            return 1;

        case Format::UINT_ABCD: case Format::UINT_DCBA:
        case Format::UINT_CDAB: case Format::UINT_BADC:
        case Format::INT_ABCD: case Format::INT_DCBA: case Format::INT_CDAB:
        case Format::FLOAT_ABCD: case Format::FLOAT_DCBA:
        case Format::FLOAT_CDAB: case Format::FLOAT_BADC:
        case Format::UINT_BCD_ABCD: case Format::UINT_BCD_CDAB:
        case Format::UINT32_MODICON:
        case Format::INT_TEXT_TO_NUMBER:
            return 2;

        case Format::DOUBLE_ABCDEFGH: case Format::DOUBLE_HGFEDCBA:
        case Format::DOUBLE_BADCFEHG: case Format::DOUBLE_GHEFCDAB:
        case Format::INT64_ABCDEFGH: case Format::INT64_HGFEDCBA:
        case Format::INT64_BADCFEHG: case Format::INT64_GHEFCDAB:
        case Format::UINT64_ABCDEFGH: case Format::UINT64_HGFEDCBA:
        case Format::UINT64_BADCFEHG: case Format::UINT64_GHEFCDAB:
            return 4;

        case Format::TEXT_GMT:
        case Format::TEXT:
            return 0;
    }
    return 0;
}

bool modbusFormatIsPlainInteger(ModbusMapping::Format format) {
    using Format = ModbusMapping::Format;
    switch (format) {
        case Format::UINT16: case Format::INT16:
        case Format::UINT_ABCD: case Format::UINT_DCBA:
        case Format::UINT_CDAB: case Format::UINT_BADC:
        case Format::INT_ABCD: case Format::INT_DCBA: case Format::INT_CDAB:
        case Format::UINT64_ABCDEFGH: case Format::UINT64_HGFEDCBA:
        case Format::UINT64_BADCFEHG: case Format::UINT64_GHEFCDAB:
        case Format::INT64_ABCDEFGH: case Format::INT64_HGFEDCBA:
        case Format::INT64_BADCFEHG: case Format::INT64_GHEFCDAB:
            return true;

        case Format::FLOAT_ABCD: case Format::FLOAT_DCBA:
        case Format::FLOAT_CDAB: case Format::FLOAT_BADC:
        case Format::DOUBLE_ABCDEFGH: case Format::DOUBLE_HGFEDCBA:
        case Format::DOUBLE_BADCFEHG: case Format::DOUBLE_GHEFCDAB:
        case Format::UINT_BCD_ABCD: case Format::UINT_BCD_CDAB:
        case Format::UINT32_MODICON:
        case Format::INT_TEXT_TO_NUMBER:
        case Format::TEXT_GMT:
        case Format::TEXT:
            return false;
    }
    return false;
}
