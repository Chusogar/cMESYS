#!/usr/bin/env python3
"""Writes the Android app files of every console of
main/drivers/consoles/consoles.mk (machines/consoles/<id>/): console.txt (for
console_info.py), res/values/strings.xml, res/values/machine_arrays.xml,
res/xml/machine.xml, the launcher icons and assets/help.html (in Spanish).

What each console is (title, media, extensions, buttons, START / SELECT,
keypad or keyboard) comes from its own cons_machine: a small program
(cons_info_dump.c) is built with the console's core and run, so the app
says what the C code does.

  python3 main/ports/android/tools/gen_consoles_android.py [id ...]
"""
import os
import re
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont
from android_dirs import machine_dir

HERE = os.path.dirname(os.path.abspath(__file__))
ANDROID = os.path.dirname(HERE)
M = os.path.normpath(os.path.join(ANDROID, "..", ".."))
MK = os.path.join(M, "drivers", "consoles", "consoles.mk")
DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144}

# What the app shows that cons_machine does not say: the short name, the
# media in Spanish, the aspect (0: the picture's own; handhelds and the
# Vectrex) and the icon's colours.
EXTRA = {
    "sg1000":     ("SG-1000", "Cartucho", 4 / 3, (30, 90, 200)),
    "coleco":     ("ColecoVision", "Cartucho", 4 / 3, (40, 40, 40)),
    "pv2000":     ("PV-2000", "Cartucho", 4 / 3, (200, 40, 40)),
    "pv1000":     ("PV-1000", "Cartucho", 4 / 3, (60, 60, 160)),
    "sms":        ("Master System", "Cartucho", 4 / 3, (200, 30, 30)),
    "gamegear":   ("Game Gear", "Cartucho", 0, (40, 40, 40)),
    "nes":        ("NES", "Cartucho", 4 / 3, (180, 30, 30)),
    "a2600":      ("Atari 2600", "Cartucho", 4 / 3, (120, 70, 30)),
    "a7800":      ("Atari 7800", "Cartucho", 4 / 3, (40, 40, 40)),
    "vectrex":    ("Vectrex", "Cartucho", 0, (30, 30, 30)),
    "gameboy":    ("Game Boy", "Cartucho", 0, (140, 160, 40)),
    "pcengine":   ("PC Engine", "HuCard", 4 / 3, (230, 230, 230)),
    "scv":        ("Super Cassette Vision", "Cartucho", 4 / 3, (200, 60, 60)),
    "wonderswan": ("WonderSwan", "Cartucho", 0, (90, 90, 180)),
    "genesis":    ("Mega Drive", "Cartucho", 4 / 3, (30, 30, 30)),
    "sega32x":    ("32X", "Cartucho", 4 / 3, (200, 40, 40)),
    "snes":       ("Super Nintendo", "Cartucho", 4 / 3, (120, 110, 190)),
    "gba":        ("Game Boy Advance", "Cartucho", 0, (80, 60, 170)),
    "psx":        ("PlayStation", "Disco", 4 / 3, (120, 120, 130)),
}

# What the BIOS-needing consoles say in their help page
BIOS = {
    "coleco": "la BIOS de la ColecoVision (<code>coleco.rom</code>, 8 KB)",
    "pv2000": "la ROM del PV-2000 (<code>hn613128pc64.bin</code>, 16 KB)",
    "vectrex": "la BIOS del Vectrex (<code>exec_rom.bin</code>, 8 KB; sin ella, ábrela como cartucho y juega a Mine Storm)",
    "scv": "la BIOS del SCV (<code>upd7801g.s01</code> + <code>epochtv.chr</code>, o ambas en un fichero de 5 KB)",
    "sega32x": "las tres BIOS del 32X (<code>32x_g_bios.bin</code>, <code>32x_m_bios.bin</code>, <code>32x_s_bios.bin</code>)",
    "gba": "la BIOS de la GBA (<code>gba_bios.bin</code>, 16 KB; o ábrela como fichero antes del cartucho)",
    "psx": "la BIOS de la PlayStation (<code>scph1001.bin</code> u otra de 512 KB; o ábrela como fichero antes del disco)",
}


def consoles():
    for line in open(MK):
        m = re.match(r"^CONSOLES := (.*)$", line)
        if m:
            return m.group(1).split()
    return []


def sources(cid):
    common, own = "", ""
    for line in open(MK):
        if line.startswith("CONS_COMMON := "):
            common = line.split(":=", 1)[1]
        if line.startswith("CONS_SRC_%s := " % cid):
            own = line.split(":=", 1)[1]
    return [os.path.join(M, s) for s in (common + " " + own).split()]


def info(cid):
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "dump")
        cmd = ["cc", "-std=gnu11", "-O0", "-w", "-I" + os.path.join(M, "drivers", "consoles", "common"),
               "-I" + os.path.join(M, "drivers", "consoles", cid), "-I" + os.path.join(M, "cpu", "m68k"),
               "-o", exe, os.path.join(HERE, "cons_info_dump.c")] + sources(cid) + ["-lm"]
        subprocess.run(cmd, check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    d = {}
    for line in out.splitlines():
        k, v = line.split("=", 1)
        d[k] = v
    return d


def xml(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("'", "\\'")


def pad_line(d):
    names = [b for b in d["buttons"].split() if b != "-"]
    parts = ["Cruceta"] + names
    s = ", ".join(parts)
    if d["start"] != "-":
        s += "; START = " + d["start"]
    if d["select"] != "-":
        s += "; SELECT = " + d["select"]
    return s


def icon(path_res, short, colour):
    base = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
    g = ImageDraw.Draw(base)
    # a pad: rounded body, the cross and two buttons
    g.rounded_rectangle((6, 26, 90, 76), radius=22, fill=colour + (255,), outline=(0, 0, 0, 255), width=3)
    light = tuple(min(255, c + 90) for c in colour) + (255,)
    g.rectangle((20, 44, 40, 52), fill=(20, 20, 20, 255))
    g.rectangle((26, 38, 34, 58), fill=(20, 20, 20, 255))
    g.ellipse((58, 36, 70, 48), fill=light, outline=(0, 0, 0, 255), width=2)
    g.ellipse((70, 48, 82, 60), fill=light, outline=(0, 0, 0, 255), width=2)
    # the initials
    initials = "".join(w[0] for w in re.split(r"[\s-]+", short) if w)[:3].upper()
    if len(initials) < 2:
        initials = short[:3].upper()
    try:
        font = ImageFont.truetype("DejaVuSans-Bold.ttf", 20)
    except OSError:
        font = ImageFont.load_default()
    w = g.textlength(initials, font=font)
    g.rounded_rectangle((48 - w / 2 - 6, 2, 48 + w / 2 + 6, 26), radius=6, fill=(255, 255, 255, 255),
                        outline=(0, 0, 0, 255), width=2)
    g.text((48 - w / 2, 3), initials, font=font, fill=(0, 0, 0, 255))
    for dens, size in DENSITIES.items():
        d = os.path.join(path_res, "mipmap-" + dens)
        os.makedirs(d, exist_ok=True)
        base.resize((size, size), Image.LANCZOS).save(os.path.join(d, "ic_launcher.png"))


HELP = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body {{ background:#181a22; color:#e8e8ec; font-family:sans-serif; margin:16px; line-height:1.45 }}
h1 {{ font-size:1.4em; color:#fff }} h2 {{ font-size:1.1em; color:#f0c040; margin-top:1.4em }}
code, kbd {{ background:#2a2d38; padding:1px 5px; border-radius:3px }}
td {{ padding:3px 8px; vertical-align:top }} a {{ color:#8ab4ff }}
</style></head><body>
<h1>cMESYS {short}</h1>
<p>{title} ({maker}) con el núcleo del driver de consola de cMESYS, migrado del emulador
<a href="https://github.com/Chusogar/dsp-cpp">dsp-cpp</a>: el mismo código que el de escritorio, la web y las placas.</p>
<h2>{media}</h2>
<p>Menú → <b>Abrir {media_l}</b>: un fichero {exts} (o dentro de un <code>.zip</code>). <b>Sacar {media_l}</b> lo expulsa
y <b>Reset</b> reinicia la consola. {bios}</p>
<h2>Mandos</h2>
<table>
<tr><td>Pantalla</td><td>la cruceta y los botones táctiles: {pad}</td></tr>
<tr><td>Mando físico</td><td>cruceta, A / B / X / Y, START y SELECT</td></tr>
<tr><td>Teclado físico</td><td>cursores; {keys}</td></tr>
</table>
{keypad}
<h2>Ajustes</h2>
<p>Escala de la imagen, el mando en pantalla y abrir el último {media_l} al empezar.</p>
<h2>Créditos</h2>
<p>cMESYS: Chusogar. dsp-cpp: Chusogar, a partir del emulador DSP de Leniad.
{credits}</p>
</body></html>
"""


def help_page(cid, d, short, media):
    keys = "Z X C V = botones 1 a 4, ENTER = START" if d["keys"] == "0" else \
        ("las teclas de la consola; ENTER = START" if d["keys"] == "1" else "el teclado de la consola")
    keypad = ""
    if d["keypad_help"] != "-":
        keypad = "<h2>Teclado de la consola</h2><p><code>%s</code></p>" % d["keypad_help"]
    bios = ""
    if cid in BIOS:
        bios = ("Necesita %s: va dentro de la app si se compiló con ella (<code>tools/regen.sh %s</code>); "
                "las BIOS no se distribuyen con cMESYS." % (BIOS[cid], cid))
    credits = ""
    if cid == "psx":
        credits = "Los chips de la PlayStation siguen a ProjectPSX (Pedro Cortés, licencia MIT)."
    exts = ", ".join("<code>%s</code>" % e for e in d["exts"].split())
    return HELP.format(short=short, title=d["title"], maker=d["maker"], media=media, media_l=media.lower(),
                       exts=exts, bios=bios, pad=pad_line(d), keys=keys, keypad=keypad, credits=credits)


def write(cid):
    d = info(cid)
    short, media, aspect, colour = EXTRA.get(cid, (d["title"], "Cartucho", 4 / 3, (60, 60, 60)))
    md = machine_dir(cid)
    res = os.path.join(md, "res")
    for sub in ("values", "xml"):
        os.makedirs(os.path.join(res, sub), exist_ok=True)
    os.makedirs(os.path.join(md, "assets"), exist_ok=True)
    with open(os.path.join(md, "console.txt"), "w") as f:
        f.write("# Written by tools/gen_consoles_android.py from %s's cons_machine.\n" % cid)
        f.write("title = %s\nmedia = %s\nexts = %s\nbuttons = %s\nstart = %s\nselect = %s\nkeys = %s\naspect = %s\n"
                % (d["title"], media, d["exts"], d["buttons"], d["start"], d["select"], d["keys"],
                   ("%.4f" % aspect) if aspect else "0"))
    with open(os.path.join(res, "values", "strings.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                '    <string name="app_name">cMESYS %s</string>\n'
                '    <string name="settings_title">Ajustes de %s</string>\n</resources>\n'
                % (xml(short), xml(d["title"])))
    with open(os.path.join(res, "values", "machine_arrays.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                '    <string-array name="pad_entries">\n        <item>%s</item>\n    </string-array>\n'
                '    <string-array name="pad_values">\n        <item>pad</item>\n    </string-array>\n'
                '</resources>\n' % xml(pad_line(d)))
    with open(os.path.join(res, "xml", "machine.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<!-- The settings of this machine (MachineActivity / SettingsActivity: then res/xml/preferences.xml). -->\n'
                '<PreferenceScreen xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <PreferenceCategory android:title="@string/pc_system">\n'
                '        <CheckBoxPreference android:key="autoload_last" android:title="Abrir el último %s al empezar"\n'
                '            android:defaultValue="false" />\n'
                '        <ListPreference android:key="pad" android:title="@string/pref_pad" android:summary="%%s"\n'
                '            android:entries="@array/pad_entries" android:entryValues="@array/pad_values" android:defaultValue="pad" />\n'
                '    </PreferenceCategory>\n</PreferenceScreen>\n' % xml(media.lower()))
    with open(os.path.join(md, "assets", "help.html"), "w") as f:
        f.write(help_page(cid, d, short, media))
    icon(res, short, colour)
    print("%-11s %s (%s)" % (cid, d["title"], d["exts"]))


def main():
    for cid in (sys.argv[1:] or consoles()):
        write(cid)


if __name__ == "__main__":
    main()
