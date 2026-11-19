#!/usr/bin/env python3
"""Poke the light bar from a laptop/Mac (pip install bleak). Runs on the machine with Bluetooth,
not on OMV.   lightbar-ble.py state | white 3500 128 | colour 240 255 200 | step 0 10 | toggle | config 80 250 22
"""
import asyncio, struct, sys
from bleak import BleakClient, BleakScanner
U = lambda n: f"6c7b{n}-8a3e-4f5e-9d2b-4c1ee1b0a001"

async def main(a):
    dev = await BleakScanner.find_device_by_filter(lambda d, ad: U("0001") in (ad.service_uuids or []), timeout=10)
    if not dev: sys.exit("no Lightbar found")
    async with BleakClient(dev) as c:
        cmd = a[0] if a else "state"
        if cmd == "white":      # white <kelvin> <brightness>
            await c.write_gatt_char(U("0002"), struct.pack("<BBBBHH", 1, 0, int(a[2]), 255, 0, int(a[1])), response=True)
        elif cmd == "colour":   # colour <hue> <sat> <brightness>
            await c.write_gatt_char(U("0002"), struct.pack("<BBBBHH", 1, 1, int(a[3]), int(a[2]), int(a[1]), 3000), response=True)
        elif cmd == "step":     # step <target 0..3> <delta>
            await c.write_gatt_char(U("0003"), struct.pack("<Bh", int(a[1]), int(a[2])), response=False)
        elif cmd == "toggle":
            await c.write_gatt_char(U("0003"), struct.pack("<Bh", 4, 0), response=False)
        elif cmd == "config":   # config <limit%> <fade_ms> <gamma*10>
            await c.write_gatt_char(U("0004"), struct.pack("<BHB", int(a[1]), int(a[2]), int(a[3])), response=True)
        on, mode, br, sat, hue, cct = struct.unpack("<BBBBHH", await c.read_gatt_char(U("0002")))
        print(dict(on=on, mode=mode, brightness=br, sat=sat, hue=hue, cct=cct))
        print("config", struct.unpack("<BHB", await c.read_gatt_char(U("0004"))))
        print((await c.read_gatt_char(U("0005"))).decode())

asyncio.run(main(sys.argv[1:]))
