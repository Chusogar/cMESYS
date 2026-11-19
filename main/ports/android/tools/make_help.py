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
    "amiga": dict(
        title="cMESYS Amiga",
        intro="Commodore Amiga 500 (PAL, OCS) con el núcleo Amiga de cMESYS (migrado de dsp-cpp): 68000, "
              "Agnus, Denise y Paula (copper, blitter, sprites, HAM, los cuatro canales de sonido con los filtros "
              "del A500), las dos CIA 8520, 512 KB de chip RAM y 512 KB de slow RAM, dos disqueteras (DF0 y DF1) "
              "y el teclado con su protocolo serie.",
        media=("Discos", "Meter un disco <code>.adf</code> en DF0 o en DF1 (la unidad externa), cambiarlo o "
               "expulsarlo. Para los juegos de varios discos, cambia el de DF0 cuando el juego lo pida."),
        reset="Reset (como Ctrl + Amiga + Amiga).",
        settings="Kickstart (1.3, 1.2, 2.04 o 3.1)",
        keys=("Teclas Amiga", "Ctrl + Amiga + Amiga, ESC, HELP, F1, F10, Amiga + M / N…"),
        keyboard="una foto de un A500 español",
        same="la tecla pulsada se hunde como en los teclados de dsp-cpp",
        mods="<kbd>Mayúsculas</kbd>, <kbd>Ctrl</kbd>, <kbd>Alt</kbd> y las teclas <kbd>Amiga</kbd>",
        physical="Las teclas del PC son las del Amiga por su posición; Windows (o RePág / AvPág) = Amiga izq. / "
                 "dcha., Fin = HELP, Insert = \\ |, Bloq Num / Bloq Despl = ( ) del bloque numérico.",
        pad="el joystick (puerto 2), con el disparo en A y X, o los cursores",
        extra="""<h2>Ratón</h2>
<p>La imagen es un <b>trackpad</b> para el ratón del Amiga: un dedo que se mueve mueve el puntero; un
toque es un clic con el botón izquierdo (dos toques, doble clic); tocar y volver a tocar y mover arrastra
con el botón izquierdo pulsado; un segundo dedo es el botón derecho mientras está apoyado (los menús de
Workbench: mantenlo y mueve el primero). Un ratón USB o Bluetooth también vale.</p>
""",
        start="<p>El Kickstart pide un disco (la mano con el disquete): mete uno en DF0 en <b>Discos</b> y "
              "arranca solo.</p>",
        kbcredit="Teclado en pantalla: foto de un Amiga 500 español.",
        credits="núcleo Amiga migrado de dsp-cpp (del emulador DSP), 68000 de Musashi (Karl Stenerud)"),
    "apple2": dict(
        title="cMESYS Apple II",
        intro="Apple ][, ][+, //e y //e Enhanced con el núcleo Apple II de cMESYS (migrado de dsp-cpp): 6502 "
              "(65C02 en el Enhanced), texto de 40 y 80 columnas, baja y alta resolución, doble alta resolución "
              "del //e, el altavoz, 64 KB (128 KB en el //e con la tarjeta de 80 columnas extendida), el joystick y "
              "la Disk II en el slot 6.",
        media=("Discos", "Meter un disco <code>.dsk .do .po .nib</code> en la unidad 1 (la máquina se enciende "
               "de nuevo y lo arranca), sacarlo, o teclear <code>PR#6</code> o <code>CATALOG</code>."),
        reset="CTRL-RESET (el botón RESET: la RAM se conserva) o apagar y encender (arranca el disco).",
        settings="modelo (][, ][+, //e o //e Enhanced)",
        keys=("Teclas Apple", "CTRL-RESET, CTRL-C, ESC, RETURN, Open Apple + CTRL-RESET…"),
        keyboard=("el teclado de cada modelo, dibujado para cMESYS: el del ][ / ][+ (mayúsculas, REPT, "
                  "RESET y dos flechas) o el del //e (con DELETE, las teclas Open y Solid Apple y cuatro "
                  "flechas), que cambia con el modelo"),
        same="la tecla pulsada se hunde",
        mods="<kbd>SHIFT</kbd>, <kbd>CTRL</kbd> y las teclas <kbd>Open</kbd> / <kbd>Solid Apple</kbd>",
        physical="Las teclas del PC son las del Apple; <kbd>Retroceso</kbd> = ←, <kbd>Supr</kbd> = DELETE, "
                 "Alt izq. / dcha. = Open / Solid Apple, <kbd>F12</kbd> = RESET.",
        pad="el joystick (botones 0 y 1) o los cursores",
        start="<p>Sin disco, el ][ entra en el monitor o en Integer BASIC, el ][+ en Applesoft y el //e busca "
              "un disco (pulsa CTRL-RESET para ir a BASIC). Con la Disk II, abre un <code>.dsk</code> en "
              "<b>Discos</b> y arranca solo. Lo que se escribe en el disco se guarda en el archivo.</p>",
        kbcredit="Teclados en pantalla dibujados para cMESYS (tools/gen_apple_vkb.py).",
        credits="núcleo Apple II migrado de dsp-cpp (del emulador DSP), 6502 de cMESYS, Disk II a partir de MAME"),
    "apple2gs": dict(
        title="cMESYS Apple IIGS",
        intro="Apple IIGS (ROM 3) con el núcleo IIGS de cMESYS (migrado de dsp-cpp): 65C816, 4 MB de RAM, "
              "Mega II y los modos de vídeo del IIe y Super Hi-Res, el sintetizador Ensoniq 5503, el reloj, "
              "el teclado y el ratón ADB y cuatro discos SmartPort.",
        media=("Discos", "Meter discos <code>.po .2mg .hdv</code> (orden ProDOS) en las cuatro unidades "
               "SmartPort: el primero es el de arranque (la máquina se reinicia); cambiarlos o sacarlos."),
        reset="CTRL-RESET o apagar y encender.",
        settings="el mando",
        keys=("Teclas IIGS", "CTRL-RESET, el panel de control (CTRL + ⌘ + ESC), CTRL + ⌘ + RESET, ⌘ + Q…"),
        keyboard="el teclado Apple Desktop Bus del IIGS, dibujado para cMESYS, con el bloque numérico y RESET",
        same="la tecla pulsada se hunde",
        mods="<kbd>shift</kbd>, <kbd>control</kbd>, <kbd>option</kbd> y <kbd>⌘</kbd>",
        physical="Las teclas del PC son las del IIGS; Alt = option, Windows = ⌘, <kbd>F12</kbd> = RESET.",
        pad="los cursores, RETURN, ESPACIO, ESC y TAB",
        extra="""<h2>Ratón</h2>
<p>La imagen es un <b>trackpad</b> para el ratón: un dedo que se mueve mueve el puntero; un toque es un clic
(dos toques, doble clic); tocar, volver a tocar y mover arrastra con el botón pulsado (para los menús: toca,
vuelve a tocar sobre el título y baja). Un ratón USB o Bluetooth también vale.</p>
""",
        start="<p>Sin disco, el IIGS busca uno en las unidades (<i>Check startup device</i>): mete un disco de "
              "arranque de ProDOS o GS/OS en la primera unidad de <b>Discos</b>. CTRL + ⌘ + ESC abre el panel "
              "de control.</p>",
        kbcredit="Teclado en pantalla dibujado para cMESYS (tools/gen_apple_vkb.py).",
        credits="núcleo IIGS migrado de dsp-cpp (del emulador DSP), 65C816 y Ensoniq 5503 de cMESYS"),
    "macplus": dict(
        title="cMESYS Mac Plus",
        intro="Macintosh Plus con el núcleo Mac Plus de cMESYS (migrado de dsp-cpp): 68000 a 7,8 MHz, 4 MB "
              "de RAM, pantalla de 512 x 342, la VIA, el SCC (ratón), la IWM con la disquetera de 800K (y "
              "1,44 MB), el reloj, el sonido y el SCSI con un disco duro.",
        media=("Discos", "Meter un disquete (<code>.dsk</code> de 400K, 800K o 1,44 MB, o Disk Copy 4.2) o "
               "conectar un disco duro SCSI (imágenes mayores: <code>.hda .img</code>; lo que se escribe se "
               "guarda en el archivo); expulsarlos."),
        reset="Reinicio (el botón de reset).",
        settings="el mando",
        keys=("Teclas Mac", "⌘ + E, ⌘ + Q, ⌘ + W, ⌘ + . y ⌘ + ⇧ + 1 (expulsar el disquete)…"),
        keyboard="el teclado M0110A del Mac Plus, dibujado para cMESYS, con el bloque numérico y las flechas",
        same="la tecla pulsada se hunde",
        mods="<kbd>shift</kbd>, <kbd>option</kbd> y <kbd>⌘</kbd>",
        physical="Las teclas del PC son las del Mac; Alt = option, Ctrl o Windows = ⌘.",
        pad="los cursores, RETURN, ESPACIO, ESC y TAB",
        extra="""<h2>Ratón</h2>
<p>La imagen es un <b>trackpad</b> para el ratón: un dedo que se mueve mueve el puntero; un toque es un clic
(dos toques, doble clic); tocar, volver a tocar y mover arrastra con el botón pulsado (para los menús: toca,
vuelve a tocar sobre el título y baja). Un ratón USB o Bluetooth también vale.</p>
""",
        start="<p>Sin disco, el Mac muestra el disquete con la interrogación: mete un disquete de arranque "
              "(System 6 o 7) o conecta un disco duro en <b>Discos</b>.</p>",
        kbcredit="Teclado en pantalla dibujado para cMESYS (tools/gen_apple_vkb.py).",
        credits="núcleo Mac Plus migrado de dsp-cpp (del emulador DSP, a partir de Mini vMac), 68000 de Musashi "
                "(Karl Stenerud)"),
    "macii": dict(
        title="cMESYS Mac II",
        intro="Macintosh II con el núcleo Mac II de cMESYS (migrado de dsp-cpp): 68020 con FPU (Musashi 4), "
              "8 MB de RAM, la tarjeta de vídeo Display Card 8*24 (640 x 480 en color), las dos VIA, el ASC "
              "(sonido), el teclado y el ratón ADB y el SCSI con un disco duro.",
        media=("Discos", "Conectar un disco duro SCSI (<code>.hda .img</code>; lo que se escribe se guarda en el "
               "archivo) y arrancarlo, o desconectarlo."),
        reset="Reinicio.",
        settings="el mando",
        keys=("Teclas Mac", "⌘ + E, ⌘ + Q, ⌘ + W, ⌘ + ., la tecla de encendido…"),
        keyboard="el Apple Extended Keyboard, dibujado para cMESYS: F1–F15, las teclas de navegación, el "
                 "bloque numérico y la de encendido",
        same="la tecla pulsada se hunde",
        mods="<kbd>shift</kbd>, <kbd>control</kbd>, <kbd>option</kbd> y <kbd>⌘</kbd>",
        physical="Las teclas del PC son las del Mac; Alt = option, Windows = ⌘, F1–F12, Insert = help.",
        pad="los cursores, RETURN, ESPACIO, ESC y TAB",
        extra="""<h2>Ratón</h2>
<p>La imagen es un <b>trackpad</b> para el ratón: un dedo que se mueve mueve el puntero; un toque es un clic
(dos toques, doble clic); tocar, volver a tocar y mover arrastra con el botón pulsado (para los menús: toca,
vuelve a tocar sobre el título y baja). Un ratón USB o Bluetooth también vale.</p>
""",
        start="<p>Sin disco, el Mac II muestra el disquete con la interrogación: conecta un disco duro con el "
              "sistema (System 6.0.x o 7) en <b>Discos</b>.</p>",
        kbcredit="Teclado en pantalla dibujado para cMESYS (tools/gen_apple_vkb.py).",
        credits="núcleo Mac II migrado de dsp-cpp (del emulador DSP), 68020 de Musashi 4 (Karl Stenerud y "
                "colaboradores, con SoftFloat de John R. Hauser)"),
    "specnext": dict(
        title="cMESYS Spectrum Next",
        intro="ZX Spectrum Next con el núcleo Next de cMESYS (migrado de dsp-cpp): Z80N a 3,5, 7, 14 y 28 MHz, "
              "2 MB de RAM, la ULA con los modos Timex, ULANext y ULA+, LoRes, Layer 2, el tilemap, 128 sprites, "
              "el copper, el zxnDMA, la CTC, tres AY (TurboSound), los DAC, la DivMMC, el Multiface y una "
              "tarjeta SD. Arranca como el real: la ROM de arranque carga TBBLUE.FW de la tarjeta y el firmware, "
              "NextZXOS.",
        media=("Tarjeta SD", "Meter una imagen de tarjeta SD (<code>.img .mmc .hdf</code>, por ejemplo la de "
               "CSpect o la de la distribución oficial): se usa donde está, sin copiarla, y lo que escribe el "
               "Next se guarda en ella, como en una tarjeta de verdad. También abre programas <code>.nex</code> "
               "(se cargan cuando NextZXOS ha arrancado). Y los botones NMI y DRIVE."),
        reset="el botón RESET (reinicio suave) o apagar y encender.",
        settings="el mando (Kempston o cursores) y abrir la última tarjeta al arrancar",
        keys=("Teclas Next", "BREAK, EDIT, DELETE, EXTEND MODE, ENTER, ESPACIO, Y, N…"),
        keyboard=("una foto del teclado del Next, con todas sus teclas: las 40 del Spectrum, BREAK, EDIT, "
                  "TRUE VIDEO, INV VIDEO, CAPS LOCK, GRAPH, EXTEND MODE, DELETE, los cursores y ; \" , ."),
        same="la tecla pulsada se hunde",
        mods="<kbd>CAPS SHIFT</kbd> y <kbd>SYMBOL SHIFT</kbd>",
        physical="Las teclas del PC son las del Next: Mayúsculas = CAPS SHIFT, Ctrl o Alt = SYMBOL SHIFT, "
                 "<kbd>Retroceso</kbd> = DELETE, <kbd>Esc</kbd> = BREAK, <kbd>Tab</kbd> = EXTEND MODE, "
                 "<kbd>`</kbd> = EDIT, los cursores y ; \" , . - = / + *.",
        pad="el joystick Kempston o los cursores",
        start="<p>Sin tarjeta, la ROM de arranque no tiene qué cargar: mete la imagen de la tarjeta SD en "
              "<b>Tarjeta SD</b> (se recuerda para la próxima vez). La primera vez con una tarjeta nueva, el "
              "firmware pregunta el modo de vídeo: <b>ENTER</b> y luego <b>Y</b>. NextZXOS empieza con una "
              "pantalla de bienvenida: <b>ESPACIO</b> va al menú.</p>",
        kbcredit="Teclado en pantalla: una foto del teclado del ZX Spectrum Next (tools/gen_specnext_vkb.py).",
        credits="núcleo Next migrado de dsp-cpp (del emulador DSP, a partir del de MAME: Andrei I. Holub), Z80 "
                "de dsp-cpp; la ROM de arranque, NextZXOS y el firmware son del proyecto ZX Spectrum Next"),
    "samcoupe": dict(
        title="cMESYS SAM Coupé",
        intro="SAM Coupé (MGT, 1989) con el núcleo SAM de cMESYS (migrado de dsp-cpp): Z80 a 6 MHz, 512 KB de "
              "RAM, los cuatro modos de pantalla del ASIC, la paleta de 128 colores (16 a la vez, cambiable "
              "línea a línea), el sonido SAA1099, dos unidades de disco (WD1772) y las cintas.",
        media=("Discos y cintas", "Meter un disco en la unidad 1 o 2 (<code>.mgt .dsk .sad .sdf</code>) o una "
               "cinta (<code>.tap .tzx</code>), <b>BOOT</b> (arranca el disco de la unidad 1, como F9 en la "
               "pantalla de inicio), reproducir o parar la cinta y escribir LOAD \"\" o DIR. Lo que escribe el "
               "SAM en un disco se queda en la memoria."),
        reset="reiniciar el SAM.",
        settings="el mando (joystick 1 o 2, o los cursores) y abrir el último disco al arrancar",
        keys=("Teclas SAM", "F9 (BOOT), ESC, EDIT, DELETE, RETURN, ESPACIO, F0, F1…"),
        keyboard=("una foto del teclado del SAM, con todas sus teclas: ESC, TAB, CAPS, DELETE, EDIT, INV, "
                  "SHIFT, CNTRL, SYMBOL, el teclado de funciones F0-F9 y los cursores"),
        same="la tecla pulsada se hunde",
        mods="<kbd>SHIFT</kbd>, <kbd>CNTRL</kbd> y <kbd>SYMBOL</kbd>",
        physical="Las teclas del PC son las del SAM: Mayúsculas = SHIFT, Ctrl izquierdo = CNTRL, Alt o Ctrl "
                 "derecho = SYMBOL, <kbd>Retroceso</kbd> = DELETE, <kbd>Inicio</kbd> = EDIT, <kbd>Fin</kbd> = "
                 "INV, F1-F10 = F1-F9 y F0, los cursores y ; : \" , . - + =.",
        pad="el joystick 1 (las teclas 6 7 8 9 0), el 2 (1 2 3 4 5) o los cursores",
        start="<p>El SAM arranca en SAM BASIC. Para un disco: mételo en la unidad 1 y elige <b>BOOT</b> en "
              "el menú (pulsa RETURN y F9 por ti), o pulsa una tecla y luego <b>F9</b>. Para una cinta: escribe <b>LOAD \"\"</b> y pulsa RETURN, y luego "
              "<b>Cinta: reproducir</b>.</p>",
        kbcredit="Teclado en pantalla: una foto del teclado del SAM Coupé (tools/gen_samcoupe_vkb.py).",
        credits="núcleo SAM migrado de dsp-cpp (del emulador DSP; el comportamiento del FDC y del ASIC "
                "contrastado con SimCoupe de Simon Owen), Z80 de dsp-cpp; la ROM del SAM es de MGT"),
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
<p>Es {d["keyboard"]}: {d.get("same", "el mismo de dsp-cpp, y la tecla pulsada se hunde")}. Admite varios dedos a la vez.
{mods}<kbd>MENÚ</kbd> abre el menú y <kbd>▼</kbd> oculta el teclado.</p>

<h2>Teclado físico y mandos</h2>
<p>{d["physical"]} La cruceta y los botones de un mando hacen de {d["pad"]}, según los ajustes.</p>

{d.get("extra", "")}<h2>Al arrancar</h2>
{d["start"]}
<p>Un archivo también se puede abrir con la app desde un gestor de archivos (<i>Abrir con</i>).</p>

<h2>Créditos</h2>
<p>cMESYS (Chusogar): {d["credits"]}. {d.get("kbcredit", "Teclados en pantalla de dsp-cpp.")}
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
