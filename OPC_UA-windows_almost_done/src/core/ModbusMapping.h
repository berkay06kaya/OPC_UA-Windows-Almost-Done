#pragma once

struct ModbusMapping {
    enum class Format {
        UINT16, INT16,

        UINT_ABCD, UINT_DCBA, UINT_CDAB, UINT_BADC,
        INT_ABCD, INT_DCBA, INT_CDAB,

        FLOAT_ABCD, FLOAT_DCBA, FLOAT_CDAB, FLOAT_BADC,

        DOUBLE_ABCDEFGH, DOUBLE_HGFEDCBA, DOUBLE_BADCFEHG, DOUBLE_GHEFCDAB,
        INT64_ABCDEFGH, INT64_HGFEDCBA, INT64_BADCFEHG, INT64_GHEFCDAB,
        UINT64_ABCDEFGH, UINT64_HGFEDCBA, UINT64_BADCFEHG, UINT64_GHEFCDAB,

        UINT_BCD_ABCD, UINT_BCD_CDAB,
        UINT32_MODICON,
        INT_TEXT_TO_NUMBER,
        TEXT_GMT,

        TEXT
    };

    enum class RegisterType { HoldingRegister, InputRegister, Coil, DiscreteInput };

    int registerAddress = 0;
    int registerCount = 2;
    RegisterType registerType = RegisterType::HoldingRegister;
    Format format = Format::FLOAT_ABCD;
};
