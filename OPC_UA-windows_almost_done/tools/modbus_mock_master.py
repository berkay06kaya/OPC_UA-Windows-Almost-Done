#!/usr/bin/env python3
"""
Gercekci Modbus TCP master mock'u — OPC->Modbus gateway'in ModbusSink cikisini denemek icin.

Kullanim:
    python3 tools/modbus_mock_master.py --port 1502                         # ham hex tarama
    python3 tools/modbus_mock_master.py --port 1502 --tags tags.json        # cozumlenmis degerler

tags.json ornegi:
    [
      {"name": "Aktif_Enerji", "address": 100, "count": 2, "format": "FLOAT_ABCD"},
      {"name": "Sayac_ID",     "address": 110, "count": 4, "format": "UINT64_ABCDEFGH"}
    ]
"""

import argparse
import json
import os
import struct
import sys
import time

from pymodbus.client import ModbusTcpClient

MAX_REGISTERS_PER_READ = 125

# (word_swap, byte_swap, kind) -- ModbusConverter.cpp::convert()'teki packWords/packInt16Family/
# packInt32Family cagrilariyla birebir ayni. kind: "uint" | "int" | "float" | "double".
FORMAT_SWAP = {
    "UINT16": (False, False, "uint"),
    "INT16": (False, False, "int"),
    "UINT_ABCD": (False, False, "uint"),
    "UINT_DCBA": (True, True, "uint"),
    "UINT_CDAB": (True, False, "uint"),
    "UINT_BADC": (False, True, "uint"),
    "INT_ABCD": (False, False, "int"),
    "INT_DCBA": (True, True, "int"),
    "INT_CDAB": (True, False, "int"),
    "FLOAT_ABCD": (False, False, "float"),
    "FLOAT_DCBA": (True, True, "float"),
    "FLOAT_CDAB": (True, False, "float"),
    "FLOAT_BADC": (False, True, "float"),
    "DOUBLE_ABCDEFGH": (False, False, "double"),
    "DOUBLE_HGFEDCBA": (True, True, "double"),
    "DOUBLE_BADCFEHG": (False, True, "double"),
    "DOUBLE_GHEFCDAB": (True, False, "double"),
    "INT64_ABCDEFGH": (False, False, "int"),
    "INT64_HGFEDCBA": (True, True, "int"),
    "INT64_BADCFEHG": (False, True, "int"),
    "INT64_GHEFCDAB": (True, False, "int"),
    "UINT64_ABCDEFGH": (False, False, "uint"),
    "UINT64_HGFEDCBA": (True, True, "uint"),
    "UINT64_BADCFEHG": (False, True, "uint"),
    "UINT64_GHEFCDAB": (True, False, "uint"),
}

# Sabit register genisligi olan formatlar (float/double/64-bit native). Digerleri (UINT16/INT16/
# UINT_*/INT_* 16-32 bit aileleri) auto-widen nedeniyle degisken genislikte, tag config'teki
# "count" alanindan okunur.
FIXED_REGISTER_COUNT = {
    "FLOAT_ABCD": 2, "FLOAT_DCBA": 2, "FLOAT_CDAB": 2, "FLOAT_BADC": 2,
    "DOUBLE_ABCDEFGH": 4, "DOUBLE_HGFEDCBA": 4, "DOUBLE_BADCFEHG": 4, "DOUBLE_GHEFCDAB": 4,
    "INT64_ABCDEFGH": 4, "INT64_HGFEDCBA": 4, "INT64_BADCFEHG": 4, "INT64_GHEFCDAB": 4,
    "UINT64_ABCDEFGH": 4, "UINT64_HGFEDCBA": 4, "UINT64_BADCFEHG": 4, "UINT64_GHEFCDAB": 4,
}

UNSUPPORTED_FORMATS = {
    "UINT_BCD_ABCD", "UINT_BCD_CDAB", "UINT32_MODICON", "INT_TEXT_TO_NUMBER", "TEXT", "TEXT_GMT",
}


def registers_needed(fmt, configured_count):
    if fmt in FIXED_REGISTER_COUNT:
        return FIXED_REGISTER_COUNT[fmt]
    return max(1, int(configured_count))


def decode_raw(regs, word_swap, byte_swap):
    ordered = list(reversed(regs)) if word_swap else list(regs)
    raw = 0
    for w in ordered:
        if byte_swap:
            w = ((w & 0xFF) << 8) | ((w >> 8) & 0xFF)
        raw = (raw << 16) | (w & 0xFFFF)
    return raw


def interpret(raw, byte_count, kind):
    bit_length = byte_count * 8
    if kind == "uint":
        return raw
    if kind == "int":
        if raw >= (1 << (bit_length - 1)):
            raw -= 1 << bit_length
        return raw
    data = raw.to_bytes(byte_count, "big")
    if kind == "float":
        return struct.unpack(">f", data)[0]
    if kind == "double":
        return struct.unpack(">d", data)[0]
    return raw


def decode_tag(fmt, regs, configured_count):
    if fmt in UNSUPPORTED_FORMATS or fmt not in FORMAT_SWAP:
        return None
    word_swap, byte_swap, kind = FORMAT_SWAP[fmt]
    raw = decode_raw(regs, word_swap, byte_swap)
    byte_count = registers_needed(fmt, configured_count) * 2
    return interpret(raw, byte_count, kind)


def hex_words(regs):
    return "[" + ",".join("%04X" % (w & 0xFFFF) for w in regs) + "]"


def read_registers(client, address, count, device_id):
    words = []
    remaining = count
    addr = address
    while remaining > 0:
        chunk = min(remaining, MAX_REGISTERS_PER_READ)
        rr = client.read_input_registers(addr, count=chunk, device_id=device_id)
        if rr.isError():
            return None, str(rr)
        words.extend(rr.registers)
        addr += chunk
        remaining -= chunk
    return words, None


def clear_screen():
    sys.stdout.write("\033c")
    sys.stdout.flush()


def run_tags_mode(client, tags, device_id, host, port, interval):
    while True:
        clear_screen()
        now = time.strftime("%H:%M:%S")
        connected = client.connected or client.connect()
        print("Modbus Mock Master  ->  %s:%d   (%s)" % (host, port, now))
        print("Baglanti: %s" % ("BAGLI" if connected else "BAGLANAMADI, yeniden deneniyor..."))
        print("-" * 90)
        print("%-20s %-8s %-16s %-8s %-20s %s" % ("Isim", "Adres", "Format", "Sayi", "Ham Hex", "Deger"))
        print("-" * 90)
        if connected:
            for tag in tags:
                name, address, count, fmt = tag["name"], tag["address"], tag["count"], tag["format"]
                needed = registers_needed(fmt, count)
                regs, err = read_registers(client, address, needed, device_id)
                if err:
                    print("%-20s %-8d %-16s %-8d %-20s HATA: %s" % (name, address, fmt, needed, "-", err))
                    continue
                value = decode_tag(fmt, regs, count)
                value_str = "cozumleme yok" if value is None else str(value)
                print("%-20s %-8d %-16s %-8d %-20s %s" % (name, address, fmt, needed, hex_words(regs), value_str))
        print("-" * 90)
        print("Cikmak icin Ctrl+C")
        time.sleep(interval)


def run_scan_mode(client, start, count, device_id, host, port, interval):
    while True:
        clear_screen()
        now = time.strftime("%H:%M:%S")
        connected = client.connected or client.connect()
        print("Modbus Mock Master (ham tarama)  ->  %s:%d   (%s)" % (host, port, now))
        print("Baglanti: %s" % ("BAGLI" if connected else "BAGLANAMADI, yeniden deneniyor..."))
        print("Aralik: Input Register #%d..#%d" % (start, start + count - 1))
        print("-" * 90)
        if connected:
            regs, err = read_registers(client, start, count, device_id)
            if err:
                print("HATA: %s" % err)
            else:
                for i in range(0, len(regs), 8):
                    row = regs[i:i + 8]
                    addr = start + i
                    print("#%-6d %s" % (addr, hex_words(row)))
        print("-" * 90)
        print("Cikmak icin Ctrl+C")
        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description="Gercekci Modbus TCP master mock'u")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--device-id", type=int, default=1)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--tags", default=None, help="Tag tanimlarini iceren JSON dosyasi")
    parser.add_argument("--start", type=int, default=0, help="Ham tarama modunda baslangic adresi")
    parser.add_argument("--count", type=int, default=40, help="Ham tarama modunda register sayisi")
    args = parser.parse_args()

    client = ModbusTcpClient(args.host, port=args.port)

    try:
        if args.tags:
            with open(args.tags, "r", encoding="utf-8") as f:
                tags = json.load(f)
            run_tags_mode(client, tags, args.device_id, args.host, args.port, args.interval)
        else:
            run_scan_mode(client, args.start, args.count, args.device_id, args.host, args.port, args.interval)
    except KeyboardInterrupt:
        print("\nCikiliyor.")
    finally:
        client.close()


if __name__ == "__main__":
    main()
