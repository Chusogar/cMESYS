#!/usr/bin/env python3
"""RAM use of an RP2040 / RP2350 firmware: the static part of the main RAM
(256 KB / 512 KB; the heap gets the rest: the game's allocations, PicoDVI's
line buffers and, on the RP2350, the second frame buffer)."""
import subprocess, sys
elf = sys.argv[1]
attrs = subprocess.run(["arm-none-eabi-readelf", "-A", elf], capture_output=True, text=True).stdout
kb = 512 if "8-M" in attrs else 256         # RP2350 (Cortex-M33) or RP2040 (Cortex-M0+)
out = subprocess.run(["arm-none-eabi-size", "-A", "-d", elf], capture_output=True, text=True).stdout
ram = 0
for line in out.splitlines():
    p = line.split()
    if len(p) == 3 and p[2].isdigit() and 0x20000000 <= int(p[2]) < 0x20000000 + kb * 1024 and p[0] != ".heap":
        ram += int(p[1])
print("%s: %d KB of the %d KB main RAM static, %d KB for the heap" % (elf.split("/")[-1], ram // 1024, kb, kb - ram // 1024))
