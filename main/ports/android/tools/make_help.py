#!/usr/bin/env python3
"""Writes the help page of each machine's app (machines/<machine>/assets/
help.html, in Spanish) from one template: the menu, the on-screen keyboard,
physical keyboards and pads are the same for all; each machine adds its
media, keys and start-up.

  python3 main/ports/android/tools/make_help.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

STYLE = """body { background:#181a22; color:#e8e8ec; font-family:sans-serif; margin:16px; line-height:1.45 }
h1 { font-size:1.4em; color:#fff } h2 { font-size:1.1em; color:#f0c040; margin-top:1.4em }
code, kbd { background:#2a2d38; padding:1px 5px; border-radius:3px }
td { padding:3px 8px; vertical-align:top } a { color:#8ab4ff }"""

MACHINES = {
    "ql": dict(
        title="cMESYS QL",
        intro="Sinclair QL (68008, ZX8301, ZX8302, IPC 8749 y dos microdrives) con el núcleo del "
              "driver QL de cMESYS: el mismo código que funciona en la CYD (ESP32) y en la RP2350.",
        media=("Microdrive", "Cargar un cartucho <code>.mdv</code> (formato QLAY, 174930 bytes) en MDV1 y "
               "arrancarlo (reset + F1), insertarlo en MDV1 / MDV2 sin reiniciar, el cartucho incluido en la "
               "app, o expulsarlos."),
        reset="Reset en frío, o reset y arranque del cartucho de MDV1.",
        settings="ROM del sistema (JS, JM, Minerva o una de 48 KB elegida por ti)",
        keys=("Teclas QL", "F1–F5, ESC, CTRL+ESPACIO (Break), CTRL+C, CTRL+F5…"),
        keyboard="la foto del teclado del QL (sin los microdrives)",
        mods="<kbd>SHIFT</kbd>, <kbd>CTRL</kbd> y <kbd>ALT</kbd>",
        physical="Las teclas del PC son las del QL; <kbd>Retroceso</kbd> = CTRL+←, <kbd>Supr</kbd> = CTRL+→, "
                 "<kbd>`</kbd> = £.",
        pad="joystick 1 (cursores y ESPACIO) o joystick 2 (F1–F4 y F5)",
        start="<p>El QL pregunta <i>F1 monitor / F2 TV</i>. Con un cartucho en MDV1, F1 carga su programa "
              "<code>BOOT</code>; la opción <b>Cargar en MDV1 y arrancar</b> lo hace sola. Lo que el QL escribe "
              "en un cartucho se conserva mientras esté insertado.</p>",
        credits="núcleo QL portado de dsp-cpp y MAME (sinclair/ql.cpp), 68000 de Musashi (Karl Stenerud), "
                "MCS-48 y ZX8301/ZX8302 a partir de MAME"),
    "zxspec": dict(
        title="cMESYS Spectrum",
        intro="ZX Spectrum 48K, 128K y +3, Pentagon 128, Pentagon 1024 y Scorpion ZS-256 con el núcleo "
              "Spectrum de cMESYS (migrado de dsp-cpp): contención, bus flotante, ULA+, AY, Beta Disk y el "
              "+3 con su disquetera.",
        media=("Medios", "Abrir una cinta (<code>.tap .tzx .pzx .csw</code>), un snapshot (<code>.sna .z80</code>), "
               "una grabación <code>.rzx</code>, un disco (<code>.trd .scl</code> en los clones, <code>.dsk</code> en "
               "el +3) o un cartucho de Interface 2; reproducir o pausar la cinta; teclear <code>LOAD \"\"</code>; "
               "el botón mágico de los clones."),
        reset="Reset.",
        settings="modelo (48K, 128K, +3, Pentagon, Pentagon 1024, Scorpion)",
        keys=("Teclas Spectrum", "BREAK, DELETE, EDIT, CAPS LOCK, GRAPHICS y el modo extendido."),
        keyboard="el teclado de goma del 48K que dibuja dsp-cpp, con sus palabras clave",
        mods="<kbd>CAPS SHIFT</kbd> y <kbd>SYMBOL SHIFT</kbd>",
        physical="Mayúsculas = CAPS SHIFT, Ctrl / Mayúsculas derecha = SYMBOL SHIFT, <kbd>Retroceso</kbd> = DELETE, "
                 "<kbd>Esc</kbd> = BREAK, los cursores = CAPS+5…8.",
        pad="un joystick Kempston, Sinclair 1 o 2 o de cursores (Protek)",
        start="<p>Para cargar una cinta: ábrela en <b>Medios</b>, teclea <code>LOAD \"\"</code> (en el 48K, la "
              "opción del menú lo hace; en el 128K basta ENTER en <i>Tape Loader</i>) y pulsa <b>Cinta: "
              "reproducir</b>. Los snapshots arrancan solos.</p>",
        credits="núcleo Spectrum migrado de dsp-cpp (del emulador DSP)"),
    "cpc": dict(
        title="cMESYS CPC",
        intro="Amstrad CPC 6128 con el núcleo CPC de cMESYS (migrado de dsp-cpp): Z80 con las esperas del "
              "Gate Array, CRTC 6845, AY-3-8912, la disquetera uPD765 con AMSDOS y el lector de cintas.",
        media=("Medios", "Abrir un disco (<code>.dsk</code>), una cinta (<code>.cdt .tzx .csw</code>) o un snapshot "
               "<code>.sna</code>; reproducir o pausar la cinta; teclear <code>CAT</code>, <code>RUN\"</code>, "
               "<code>|TAPE</code> + <code>RUN\"</code> o <code>|CPM</code>; monitor verde o en color."),
        reset="Reset.",
        settings="mando",
        keys=("Teclas CPC", "ESC, COPIA, INTRO, CTRL+MAYS+ESC (reset), f0–f2."),
        keyboard="el teclado del CPC 464 en español que dibuja dsp-cpp, con el bloque numérico y los cursores",
        mods="<kbd>MAYS</kbd> y <kbd>CTRL</kbd>",
        physical="Las teclas del PC son las del CPC; Alt = COPIA, <kbd>Retroceso</kbd> = BORR, <kbd>Supr</kbd> = CLR, "
                 "F1–F10 = f1–f0.",
        pad="el joystick (dos botones) o los cursores con ESPACIO y COPIA",
        start="<p>Para un disco: ábrelo y teclea <code>CAT</code> y <code>RUN\"nombre</code> (o usa las opciones "
              "del menú). Para una cinta: <code>|TAPE</code>, <code>RUN\"</code> y <b>Cinta: reproducir</b>.</p>",
        credits="núcleo CPC migrado de dsp-cpp (del emulador DSP)"),
    "c64": dict(
        title="cMESYS C64",
        intro="Commodore 64 (PAL) con el núcleo C64 de cMESYS (migrado de dsp-cpp): 6510, VIC-II, SID, dos CIA y "
              "la unidad 1541 en el bus IEC.",
        media=("Medios", "Abrir un disco (<code>.d64 .g64</code>), un programa (<code>.prg .t64</code>) o una cinta "
               "<code>.tap</code>; PLAY / STOP de la cinta; teclear <code>LOAD\"*\",8,1</code>, "
               "<code>LOAD\"$\",8</code> y <code>LIST</code>, <code>LOAD</code> o <code>RUN</code>."),
        reset="Reset.",
        settings="carga rápida de disco",
        keys=("Teclas C64", "RUN/STOP, RUN/STOP+RESTORE, F1–F7, CLR/HOME, C=+SHIFT…"),
        keyboard="el teclado del C64 que dibuja dsp-cpp, con los gráficos PETSCII de la ROM de caracteres "
                 "en el frontal de las teclas, RESTORE y SHIFT LOCK",
        mods="<kbd>SHIFT</kbd>, <kbd>C=</kbd> y <kbd>CTRL</kbd> (SHIFT LOCK se queda bajada)",
        physical="Las teclas del PC son las del C64; Esc = RUN/STOP, Alt = C=, <kbd>Retroceso</kbd> = INST/DEL, "
                 "AvPág = RESTORE, F1–F8 como en el C64, los cursores con su SHIFT.",
        pad="un joystick en el puerto 2 (el de casi todos los juegos) o en el 1",
        start="<p>Para un disco: ábrelo y elige <b>Escribir LOAD\"*\",8,1</b> y luego <b>Escribir RUN</b>. "
              "Un <code>.prg</code> o <code>.t64</code> sin unidad se mete en la memoria y arranca.</p>",
        credits="núcleo C64 migrado de dsp-cpp (del emulador DSP)"),
    "msx": dict(
        title="cMESYS MSX",
        intro="MSX1 y MSX2 con el núcleo MSX de cMESYS (migrado de dsp-cpp): Z80, TMS9918 o V9938, AY-3-8910, "
              "los mappers de los cartuchos (con el SCC de Konami) y la disquetera con su ROM de disco.",
        media=("Medios", "Abrir un cartucho (<code>.rom .mx1 .mx2</code>), un disco <code>.dsk</code> o una cinta "
               "(<code>.cas .tsx .tzx</code>); sacar el cartucho; teclear <code>FILES</code>."),
        reset="Reset.",
        settings="modelo (MSX1 o MSX2)",
        keys=("Teclas MSX", "STOP, CTRL+STOP, F1–F5, SELECT, ESC."),
        keyboard="un teclado MSX internacional dibujado como los de dsp-cpp (dsp-cpp no tiene uno de MSX)",
        mods="<kbd>SHIFT</kbd>, <kbd>CTRL</kbd>, <kbd>GRAPH</kbd> y <kbd>CODE</kbd>",
        physical="Las teclas del PC son las del MSX; Alt izq. = GRAPH, Alt dcha. = CODE, F6 = SELECT, "
                 "Fin = STOP, Inicio / Insert / Supr = HOME / INS / DEL.",
        pad="el joystick 1 o 2, o los cursores con ESPACIO y M",
        start="<p>Los cartuchos y los discos arrancan solos. Al abrir una cinta se teclea <code>CLOAD</code>, "
              "<code>RUN\"CAS:\"</code> o <code>BLOAD\"CAS:\",R</code>, según la cinta.</p>",
        credits="núcleo MSX migrado de dsp-cpp (del emulador DSP)"),
    "lynx": dict(
        title="cMESYS Lynx",
        intro="Atari Lynx con el núcleo Lynx de cMESYS (migrado de dsp-cpp): 65C02, Suzy (sprites y "
              "matemáticas), Mikey (temporizadores, sonido, LCD) y la ROM de arranque.",
        media=("Cartucho", "Abrir un cartucho (<code>.lnx</code>, o <code>.lyx</code> / <code>.o</code>) o "
               "sacarlo."),
        reset="Reset.",
        settings="mando",
        keys=("Botones Lynx", "PAUSE, OPTION 1, OPTION 2, PAUSE+OPTION 1 (reiniciar el juego), "
              "PAUSE+OPTION 2 (girar la pantalla)."),
        keyboard="los mandos de la consola (cruceta con diagonales, A, B, OPTION 1, PAUSE y OPTION 2) "
                 "dibujados como los teclados de dsp-cpp",
        mods=None,
        physical="Cursores = cruceta, X = A, Z = B, 1 / 2 = OPTION 1 / 2, Intro o P = PAUSE.",
        pad="la cruceta, A y B de la consola (X / Y también A / B), L1 / R1 = OPTION 1 / 2 y SELECT = PAUSE",
        start="<p>Abre un cartucho en <b>Cartucho</b>: la consola se reinicia y lo arranca.</p>",
        credits="núcleo Lynx migrado de dsp-cpp (del emulador DSP)"),
}


def page(m, d):
    media_label, media_text = d["media"]
    keys_label, keys_text = d["keys"]
    if d["mods"]:
        mods = ("%s se quedan pulsadas para la tecla siguiente; dos toques seguidos las bloquean (hundidas y "
                "rojizas) hasta volver a tocarlas. " % d["mods"])
    else:
        mods = ""
    return f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
{STYLE}
</style></head><body>
<h1>{d["title"]}</h1>
<p>{d["intro"]}</p>

<h2>Menú</h2>
<p>Se abre con <kbd>Atrás</kbd>, con el icono de la esquina, con el botón <kbd>MENÚ</kbd> del teclado en
pantalla o con <kbd>START</kbd> de un mando. Mientras está abierto la máquina se detiene.</p>
<table>
<tr><td><b>{media_label}</b></td><td>{media_text}</td></tr>
<tr><td><b>Reset</b></td><td>{d["reset"]}</td></tr>
<tr><td><b>Pausa</b></td><td>Detiene la máquina hasta volver a pulsarlo.</td></tr>
<tr><td><b>Ajustes</b></td><td>{d["settings"][0].upper() + d["settings"][1:]}; pantalla, teclado, mando y sonido.</td></tr>
<tr><td><b>Teclado</b></td><td>Muestra u oculta el teclado en pantalla.</td></tr>
<tr><td><b>Velocidad</b></td><td>Turbo: la máquina va tan rápido como puede (sin sonido).</td></tr>
<tr><td><b>Pantalla</b></td><td>Proporción del monitor, píxeles cuadrados, pantalla completa o múltiplo entero.</td></tr>
<tr><td><b>{keys_label}</b></td><td>{keys_text}</td></tr>
<tr><td><b>Recientes</b></td><td>Los últimos archivos abiertos.</td></tr>
<tr><td><b>Expulsar</b></td><td>Saca lo que se pueda sacar (o para la cinta).</td></tr>
</table>

<h2>Teclado en pantalla</h2>
<p>Es {d["keyboard"]}: el mismo de dsp-cpp, y la tecla pulsada se hunde. Admite varios dedos a la vez.
{mods}<kbd>MENÚ</kbd> abre el menú y <kbd>▼</kbd> oculta el teclado.</p>

<h2>Teclado físico y mandos</h2>
<p>{d["physical"]} La cruceta y los botones de un mando hacen de {d["pad"]}, según los ajustes.</p>

<h2>Al arrancar</h2>
{d["start"]}
<p>Un archivo también se puede abrir con la app desde un gestor de archivos (<i>Abrir con</i>).</p>

<h2>Créditos</h2>
<p>cMESYS (Chusogar): {d["credits"]}. Teclados en pantalla de dsp-cpp.
Interfaz inspirada en <a href="https://github.com/squalldc/hataroid">Hataroid</a>.</p>
</body></html>
"""


def main():
    for m, d in MACHINES.items():
        out = os.path.join(HERE, "..", "machines", m, "assets")
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "help.html"), "w", encoding="utf-8") as f:
            f.write(page(m, d))
    print("help: " + ", ".join(MACHINES))


if __name__ == "__main__":
    main()
