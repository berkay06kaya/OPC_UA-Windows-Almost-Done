#!/usr/bin/env python3
"""
OPC uygulamasinin CPU/RAM maliyetini olcmek icin basit izleyici. Ek bagimlilik yok
(psutil kurulu degilse de calisir) -- sadece `ps` cikan komutunu okur.

Kullanim:
    python3 tools/monitor_resources.py --name OPC
    python3 tools/monitor_resources.py --pid 12345 --log run1.csv
"""

import argparse
import csv
import subprocess
import sys
import time


def find_pid(name):
    try:
        out = subprocess.check_output(["pgrep", "-x", name], text=True).strip()
        pids = [int(p) for p in out.splitlines() if p]
        return pids[0] if pids else None
    except subprocess.CalledProcessError:
        return None


def read_usage(pid):
    try:
        out = subprocess.check_output(["ps", "-o", "%cpu=,rss=", "-p", str(pid)], text=True).strip()
    except subprocess.CalledProcessError:
        return None
    parts = out.split()
    if len(parts) != 2:
        return None
    cpu_percent = float(parts[0])
    rss_mb = float(parts[1]) / 1024.0
    return cpu_percent, rss_mb


def main():
    parser = argparse.ArgumentParser(description="OPC surecinin CPU/RAM kullanimini izler")
    parser.add_argument("--pid", type=int, default=None)
    parser.add_argument("--name", default="OPC", help="Surec adi (varsayilan: OPC)")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--log", default=None, help="CSV log dosyasi (opsiyonel)")
    args = parser.parse_args()

    pid = args.pid
    if pid is None:
        pid = find_pid(args.name)
        if pid is None:
            print("HATA: '%s' adinda calisan bir surec bulunamadi." % args.name)
            sys.exit(1)

    print("Izleniyor: pid=%d" % pid)

    log_file = open(args.log, "w", newline="") if args.log else None
    writer = csv.writer(log_file) if log_file else None
    if writer:
        writer.writerow(["timestamp", "cpu_percent", "rss_mb"])

    samples = []
    start = time.time()

    try:
        while True:
            usage = read_usage(pid)
            if usage is None:
                print("Surec artik calismıyor (pid=%d). Cikiliyor." % pid)
                break
            cpu, rss = usage
            samples.append((cpu, rss))
            elapsed = time.time() - start
            print("\rt=%6.1fs  CPU=%5.1f%%  RSS=%7.1f MB  (ortalama CPU=%5.1f%% RSS=%7.1f MB, tepe CPU=%5.1f%% RSS=%7.1f MB)" % (
                elapsed, cpu, rss,
                sum(c for c, _ in samples) / len(samples),
                sum(r for _, r in samples) / len(samples),
                max(c for c, _ in samples),
                max(r for _, r in samples),
            ), end="", flush=True)
            if writer:
                writer.writerow([round(elapsed, 1), cpu, rss])
                log_file.flush()
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        if log_file:
            log_file.close()

    if samples:
        print("\n\n===== OZET (%d ornek) =====" % len(samples))
        print("Ortalama CPU: %.1f%%   Tepe CPU: %.1f%%" % (
            sum(c for c, _ in samples) / len(samples), max(c for c, _ in samples)))
        print("Ortalama RSS: %.1f MB   Tepe RSS: %.1f MB" % (
            sum(r for _, r in samples) / len(samples), max(r for _, r in samples)))


if __name__ == "__main__":
    main()
