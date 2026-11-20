#!/usr/bin/env python3
"""ConsoleInfo.java for build_apk.sh from machines/consoles/<id>/console.txt (key = value
lines: title, media, exts, buttons (4, '-' unused), start, select, keys (0 none,
1 keypad, 2 keyboard), aspect). No file (a computer): TITLE null.

usage: console_info.py machines/consoles/<id>/console.txt > ConsoleInfo.java"""
import os
import sys


def jstr(v):
    if v is None or v == "-" or v == "":
        return "null"
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    d = {}
    path = sys.argv[1]
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            d[k.strip()] = v.strip()
    buttons = (d.get("buttons", "- - - -").split() + ["-"] * 4)[:4]
    print("package com.chusogar.cmesys.app;\n")
    print("/** Written by build_apk.sh (tools/console_info.py): the console of this APK (TITLE null: a computer). */")
    print("final class ConsoleInfo {")
    print("    static final String TITLE = %s;" % (jstr(d.get("title")) if d else "null"))
    print("    static final String MEDIA_NAME = %s;" % jstr(d.get("media", "Cartucho")))
    print("    static final String EXTS = %s;" % jstr(d.get("exts", ".bin")))
    print("    static final String[] BUTTONS = {%s};" % ", ".join(jstr(b) for b in buttons))
    print("    static final String START = %s;" % jstr(d.get("start")))
    print("    static final String SELECT = %s;" % jstr(d.get("select")))
    print("    static final int KEYS = %d;" % int(d.get("keys", "0")))
    print("    static final float ASPECT = %sf;" % float(d.get("aspect", "0")))
    print("\n    private ConsoleInfo() {}\n}")


if __name__ == "__main__":
    main()
