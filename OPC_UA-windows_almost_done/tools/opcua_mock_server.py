#!/usr/bin/env python3
"""
Gercekci OPC UA mock sunucusu -- 1000 tag olcek testi icin.

Kullanim:
    python3 tools/opcua_mock_server.py --count 1000 --port 4850

Uygulamadan baglanti adresi:
    opc.tcp://127.0.0.1:4850/OPCUA/Mock   (guvenlik: Anonim)
"""

import argparse
import asyncio
import random
import time

from asyncua import Server, ua


async def run(count, port, interval, path, stall_after, stall_duration, bad_quality_tags):
    server = Server()
    await server.init()
    server.set_endpoint(f"opc.tcp://0.0.0.0:{port}{path}")
    server.set_server_name("JarvisIoT Mock OPC UA Server")

    idx = await server.register_namespace("http://mock.jarvisiot/opcua")
    objects = server.nodes.objects
    folder = await objects.add_folder(idx, "Tags")

    names = []
    variables = []
    for i in range(1, count + 1):
        name = "Tag_%04d" % i
        var = await folder.add_variable(idx, name, 0.0)
        await var.set_writable(False)
        names.append(name)
        variables.append(var)

    unknown_bad_quality = bad_quality_tags - set(names)
    if unknown_bad_quality:
        print("[UYARI] --bad-quality-tags icinde olusturulan tag'ler arasinda olmayan isimler var: %s"
              % ", ".join(sorted(unknown_bad_quality)))

    print("Mock OPC UA sunucusu hazir: opc.tcp://127.0.0.1:%d%s" % (port, path))
    print("%d tag olusturuldu (Tags klasoru altinda), guncelleme araligi: %.2fs" % (count, interval))
    if stall_after is not None:
        print("Durma (stall) penceresi: t=%.1fs -> t=%.1fs (deger yazma durur, TCP/oturum acik kalir)"
              % (stall_after, stall_after + stall_duration))
    if bad_quality_tags:
        print("Bad-kalite tag'ler (surekli BadWaitingForInitialData ile yazilacak): %s"
              % ", ".join(sorted(bad_quality_tags & set(names))))

    state = [random.uniform(0, 1000) for _ in variables]
    start = time.monotonic()
    stalling_announced = False
    resumed_announced = True

    async with server:
        while True:
            elapsed = time.monotonic() - start
            stalling = (stall_after is not None) and (stall_after <= elapsed < stall_after + stall_duration)

            if stalling and not stalling_announced:
                print("[STALL] t=%.1fs: deger yazma durduruldu." % elapsed)
                stalling_announced = True
                resumed_announced = False
            if (not stalling) and stalling_announced and not resumed_announced:
                print("[STALL] t=%.1fs: deger yazma devam ediyor." % elapsed)
                resumed_announced = True

            if not stalling:
                for i, var in enumerate(variables):
                    state[i] += random.uniform(-5.0, 5.0)
                    value = round(state[i], 3)
                    if names[i] in bad_quality_tags:
                        await var.write_value(ua.DataValue(
                            Value=ua.Variant(value),
                            StatusCode=ua.StatusCode(ua.StatusCodes.BadWaitingForInitialData)))
                    else:
                        await var.write_value(value)
            await asyncio.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description="Gercekci OPC UA mock sunucusu (olcek testi icin)")
    parser.add_argument("--count", type=int, default=1000, help="Olusturulacak tag sayisi")
    parser.add_argument("--port", type=int, default=4850)
    parser.add_argument("--interval", type=float, default=1.0, help="Tum tag'lerin guncellenme araligi (saniye)")
    parser.add_argument("--path", default="/OPCUA/Mock", help="Endpoint yolu")
    parser.add_argument("--stall-after", type=float, default=None,
                         help="Bu saniyeden sonra deger yazma durur (TCP/oturum acik kalir)")
    parser.add_argument("--stall-duration", type=float, default=0.0,
                         help="Durma penceresinin suresi (saniye), --stall-after ile birlikte kullanilir")
    parser.add_argument("--bad-quality-tags", default="",
                         help="Virgulle ayrilmis tag adlari; bu tag'ler surekli Bad StatusCode "
                              "(BadWaitingForInitialData) ile yazilir, deger yine de degismeye devam eder")
    args = parser.parse_args()

    bad_quality_tags = {n.strip() for n in args.bad_quality_tags.split(",") if n.strip()}

    try:
        asyncio.run(run(args.count, args.port, args.interval, args.path,
                         args.stall_after, args.stall_duration, bad_quality_tags))
    except KeyboardInterrupt:
        print("\nCikiliyor.")


if __name__ == "__main__":
    main()
