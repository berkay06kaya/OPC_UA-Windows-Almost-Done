#!/bin/zsh
# =====================================================================
# İstemci sertifikası + özel anahtar üretir (self-signed).
# Bu dosyalar .gitignore'da: repoya girmez, herkes yerelde üretir.
#
# Kullanım:
#     cd certs && zsh generate_certs.sh
#
# Üretilenler:
#   client_cert.pem / client_cert.der  -> genel sertifika
#   client_key.pem  / client_key.der   -> ÖZEL ANAHTAR (paylaşma!)
# =====================================================================
set -e
cd "$(dirname "$0")"

echo "[*] Anahtar + self-signed sertifika uretiliyor (cert.cnf'e gore)..."
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout client_key.pem -out client_cert.pem -config cert.cnf

echo "[*] DER formatina cevriliyor (open62541 DER bekliyor)..."
openssl x509 -in client_cert.pem -outform der -out client_cert.der
openssl rsa  -in client_key.pem  -outform der -out client_key.der

chmod 600 client_key.der client_key.pem
echo "[+] Tamam. Uretilen dosyalar certs/ altinda (git tarafindan yok sayilir)."
