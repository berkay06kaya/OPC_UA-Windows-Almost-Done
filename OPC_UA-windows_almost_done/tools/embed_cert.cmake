# cert_path/key_path (.der) dosyalarini byte dizisi olarak iceren bir C++ header uretir.
# Amac: uretilen exe'nin calisma zamaninda certs/ klasorune (mutlak build-makinesi yoluna)
# bagimli olmamasi -- sertifika/anahtar binary'nin icine gomulur.
function(opc_embed_der_as_header cert_path key_path out_header)
    file(READ "${cert_path}" _cert_hex HEX)
    file(READ "${key_path}" _key_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _cert_bytes "${_cert_hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _key_bytes "${_key_hex}")

    file(WRITE "${out_header}"
"// OTOMATIK URETILDI (tools/embed_cert.cmake) -- elle duzenlemeyin.
#pragma once
#include <cstddef>

namespace ua {
inline const unsigned char kEmbeddedClientCertDer[] = { ${_cert_bytes} };
inline const std::size_t kEmbeddedClientCertDerLen = sizeof(kEmbeddedClientCertDer);

inline const unsigned char kEmbeddedClientKeyDer[] = { ${_key_bytes} };
inline const std::size_t kEmbeddedClientKeyDerLen = sizeof(kEmbeddedClientKeyDer);
}
")
endfunction()
