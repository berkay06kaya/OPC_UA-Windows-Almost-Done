#!/bin/zsh
# =====================================================================
# Circutor teşhis script'i — İNTERNET GEREKTİRMEZ.
# IP'ni 192.168.0.x ağına aldıktan SONRA çalıştır.
#
# 1) SADECE LİSTELE (hangi endpoint hangi index/policy):
#        zsh ~/Desktop/git/OPC/diag.sh
#
# 2) SEÇİLEN KAPIYA BAĞLAN (desteklenen bir secured endpoint seç):
#        zsh ~/Desktop/git/OPC/diag.sh <index> <kullanici> <sifre>
#    örn: zsh ~/Desktop/git/OPC/diag.sh 3 admin 1234
#    (anonim denemek için kullanici/sifre'yi bos birak: zsh ...diag.sh 3)
#
# Bittiğinde internete dön ve oluşan .log dosyasını bana göster.
# =====================================================================

SERVER="opc.tcp://192.168.0.1:54545/OPCUA/CircutorPowerStudio"
GUI="$HOME/Desktop/git/OPC/build/OPC"

INDEX="${1:--1}"      # arguman yoksa -1 => sadece listele
USER_ARG="$2"
PASS_ARG="$3"
OUT="$HOME/Desktop/git/OPC/diag-$(date +%Y%m%d-%H%M%S).log"

{
  echo "================ TARIH ================"; date
  echo "================ IP / SUBNET ================"
  ifconfig 2>/dev/null | grep -E 'inet (192|10|172)\.' | grep -v 127.0.0.1
  echo "================ TCP 54545 ERISIM ================"
  nc -z -v -G 4 192.168.0.1 54545
  echo "================ OPC (index=$INDEX, 15sn) ================"
  echo "(index=-1 => yalnizca liste. index>=0 => o kapiya baglan.)"
  QT_QPA_PLATFORM=offscreen "$GUI" --selftest "$SERVER" "$INDEX" 15 "$USER_ARG" "$PASS_ARG"
} 2>&1 | tee "$OUT"

echo ""
echo ">>> Cikti kaydedildi: $OUT"
echo ">>> Internete donunce bu dosyayi bana goster."
