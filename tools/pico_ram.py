#!/usr/bin/env python3
"""RAM use of an RP2040 firmware: the static part of the 256 KB main RAM (the
heap gets the rest: the game's allocations and PicoDVI's line buffers)."""
import subprocess, sys
elf = sys.argv[1]
out = subprocess.run(["arm-none-eabi-size", "-A", "-d", elf], capture_output=True, text=True).stdout
ram = 0
for line in out.splitlines():
    p = line.split()
    if len(p) == 3 and p[2].isdigit() and 0x20000000 <= int(p[2]) < 0x20040000 and p[0] != ".heap":
        ram += int(p[1])
print("%s: %d KB of the 256 KB main RAM static, %d KB for the heap" % (elf.split("/")[-1], ram // 1024, 256 - ram // 1024))
