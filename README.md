# Arcades para ESP32 CYD (ST7789) y escritorio (SDL2)

Cuatro máquinas en un único árbol de fuentes, cada una compilable como firmware para la placa ESP32-2432S028 ("Cheap Yellow Display", variante ST7789 240x320 por SPI) o como programa de escritorio con SDL2:

| Juego | `GAME` | CPUs | Sonido | Origen del driver |
|---|---|---|---|---|
| Indiana Jones and the Temple of Doom (Atari System 1, 1985) | `indytemp` | 68010 + 6502 | YM2151, POKEY, TMS5220 | dsp-cpp `atari_system1.cpp` → `zxtiny/zxm/indytemp_core.c` |
| Snow Bros (Toaplan, 1990) | `snowbros` | 68000 + Z80 | YM3812 | `zxtiny/zxm/snowbros.c` |
| Track & Field (Konami, 1983) | `trackfld` | Konami-1 (6809) + Z80 | SN76489, VLM5030, DAC | `zxtiny/zxm/trackfld.c` |
| Puzzle Bobble (Taito B System, 1994) | `pbobble` | 68000 + Z80 | YM2610B (FM + ADPCM-A) | nuevo, según MAME `taito_b.cpp`; chips de dsp-cpp (`ym2610`/`fmopn`, `tc0140syt`, `eeprom93c46`) pasados a C |

Cada firmware lleva un solo juego. Las ROMs van dentro y cada juego configura el 68000 a su manera. No caben todos juntos en los 4 MB de flash (la partición de la aplicación ocupa 0x3F0000 bytes: Puzzle Bobble lleva 2,6 MB de ROMs).

## Estructura

```
CMakeLists.txt            proyecto ESP-IDF (idf.py -DGAME=<juego> build)
Makefile                  escritorio SDL2 (make GAME=<juego>)
build_all.sh              los 8 firmwares (4 juegos x SPI 40/80 MHz) -> dist/, y los 4 de escritorio
partitions.csv, sdkconfig.defaults
main/
  CMakeLists.txt          elige las fuentes del juego
  cpu/
    m68k/                 Musashi (68000/68010); m68kconf.h incluye el m68k_game_conf.h del juego
    m6502/                6502 (port de dsp-cpp)
    mc6809/               6809 / Konami-1
    z80/                  Z80
  sound/                  ym2151, pokey (+pokey_poly17.inc), tms5220, sn76489, vlm5030, emu8950/ (YM3812),
                          ym2610 (YM2610/YM2610B)
  machine/                via6522, slapstic, tc0140syt (buzón Taito 68000 <-> Z80), eeprom93c46
  video/                  atari_mo (objetos móviles Atari), tc0180vcu (vídeo del Taito B System)
  drivers/arcades/
    indytemp/             indytemp_core.c/.h (máquina), indytemp_cyd.c (frontend CYD),
                          indytemp_sdl.c (frontend SDL2), indytemp_roms.c (carga desde directorios),
                          rom_data.c (ROMs), m68k_game_conf.h, m68k_tables.c, indytemp.lf (IRAM)
    snowbros/             sb_core.c/.h, snowbros_cyd.c, snowbros_sdl.c, rom_data.c,
                          m68k_game_conf.h, m68k_tables.c, snowbros.lf
    trackfld/             tf_core.c/.h, trackfld_cyd.c, trackfld_sdl.c, rom_data.c, trackfld.lf
    pbobble/              pb_core.c/.h, pbobble_cyd.c, pbobble_sdl.c, rom_data.c,
                          m68k_game_conf.h, m68k_tables.c, pbobble.lf
  ports/
    esp32-cyd/            cyd.c/.h (LCD SPI/DMA, táctil XPT2046 y calibración, menú de ajustes en NVS,
                          diagnóstico de cuelgues, arranque), dac_audio (DAC GPIO26 asíncrono),
                          nunchuk (I2C), font5x7.h
    sdl2/                 sdl_port.c/.h (ventana, cola de audio, teclado, ritmo, capturas)
tools/
  regen.sh <juego>        regenera rom_data.c y m68k_tables.c del juego
  gen_roms_*.{c,py}, gen_m68k_tables.c, m68k_stub.c
roms/                     indytemp, atarisy1 (BIOS de Indy), snowbros, trackfld, pbobble (descomprimidas)
```

Los núcleos de los juegos (`*_core.c`) no dependen de la plataforma. El frontend CYD y el SDL2 de cada juego usan exactamente el mismo núcleo.

Las funciones del 68000 que van en IRAM (las más usadas, medidas en cada juego) se eligen en el `.lf` del juego. El código de Musashi no se toca. La tabla de ciclos de Musashi también es de cada juego: 68010 en Indy, 68000 en Snow Bros.

## Firmware CYD

### Flashear

```
esptool.py --chip esp32 -b 921600 write_flash 0x0 dist/<juego>_cyd_st7789_spi40mhz_full.bin
```

`spi80mhz` pone la pantalla a 80 MHz y da más fps si tu panel lo aguanta. Si ves basura, usa el de 40 MHz. `*_app_0x10000.bin` es solo la aplicación, para escribir en 0x10000.

### Primer arranque (todos los juegos)

1. Calibración del táctil: toca las 3 cruces y, en apaisado, una más.
2. Menú (6 s, o hasta tocar PLAY): invertir colores, orden RGB/BGR, girar 180°, volumen 0-4, recalibrar, PLAY. Cada juego guarda sus ajustes aparte.
3. Mantén BOOT al encender para volver al menú y recalibrar. Tras un cuelgue, el menú muestra el motivo y la traza.

Comunes a todos:
- **BOOT** o **C+Z del Nunchuk**: moneda y START.
- **Nunchuk Wii**: SDA = GPIO22, SCL = GPIO27, 3,3 V, por el conector CN1. Se puede conectar en caliente.
- **Puerto serie**: 115200 baudios.

### Controles: Indiana Jones

Táctil en 3x3 zonas sobre la imagen (se muestran 2,5 s al empezar):

| | | |
|---|---|---|
| MONEDA + START | ARRIBA | BOTÓN DERECHO (salto) |
| IZQUIERDA | BOTÓN IZQUIERDO (látigo) | DERECHA |
| BOTÓN IZQUIERDO (látigo) | ABAJO | BOTÓN DERECHO (salto) |

- Nunchuk: stick = joystick, Z = látigo, C = salto.
- Serie: `a d w s` mover, `j` látigo, `k` salto, `5` moneda, `1` start.
- En SELECT: botón derecho = 3 vidas, botón izquierdo = 7 vidas (cuesta un crédito más).
- Las puntuaciones se guardan en NVS.

### Controles: Snow Bros

```
+----+------------------------------------+----+
| CN |  tocar arriba: andar + saltar      | ▲  |
+----+                                    +----+
| ST |   mitad izq. = ◀    mitad der. = ▶ | ▼  |
+----+                                    +----+
| SH |        imagen del juego 256x224    | JP |
+----+------------------------------------+----+
 CN = moneda  ST = start  SH = disparar nieve  JP = saltar
```

- Nunchuk: stick = 4 direcciones, Z = disparar nieve, C = saltar.
- Serie: `a d w s`, `j` disparo, `k` salto, `5`, `1`.

### Controles: Track & Field

```
+-----+------------------------------------+-----+
| CN  |   mitad izquierda = CORRER          |  J  |
| ST  |   mitad derecha   = SALTAR          |  M  |
| RUN |        imagen del juego 256x224    |  P  |
+-----+------------------------------------+-----+
```

- Correr: cada toque nuevo pulsa el *otro* botón de carrera (RUN 1 / RUN 2). Saltar/lanzar: mantén pulsado para el ángulo.
- Nunchuk: stick izquierda/derecha = RUN 1 / RUN 2 (agítalo), Z = el otro botón de carrera en cada pulsación, C = salto.
- Serie: `c` RUN 1, `z` RUN 2, `x` salto, `5`, `1`.

### Controles: Puzzle Bobble

Táctil en 3x3 zonas sobre la imagen (se muestran 2,5 s al empezar):

| | | |
|---|---|---|
| MONEDA + START | DISPARAR | DERECHA |
| IZQUIERDA | DISPARAR | DERECHA |
| IZQUIERDA | DISPARAR | DERECHA |

- Izquierda / derecha giran el lanzador mientras se mantienen.
- Nunchuk: stick izquierda/derecha = apuntar, Z o C = disparar.
- Serie: `a d` apuntar, `j` (o `w`, espacio) disparar, `5` moneda, `1` start, `2` start del jugador 2.
- La EEPROM del juego (ajustes y récords) se guarda en NVS.
- Imagen 320x224 a pantalla completa (franja negra de 8 líneas arriba y abajo).

### Compilar

ESP-IDF 5.3:

```
. $IDF_PATH/export.sh
idf.py -B build/indytemp -DGAME=indytemp build       # o snowbros / trackfld / pbobble
LCD_SPI_MHZ=80 idf.py -B build/indytemp-80 -DGAME=indytemp build
./build_all.sh                                       # todo -> dist/
```

Prueba sin pantalla en QEMU (imprime un hash de la imagen cada 300 frames, comparable con la versión de PC): `CYD_HEADLESS_TEST=1 idf.py -B build/q-<juego> -DGAME=<juego> build`.

## Escritorio (SDL2)

```
make GAME=indytemp        # o snowbros / trackfld / pbobble; "make all" para todos -> build/sdl/<juego>
./build/sdl/indytemp      # ROMs incluidas; Indy acepta también: ./build/sdl/indytemp <dir indytemp> <dir atarisy1>
```

Teclas comunes: `5`/`6` moneda, `1`/`2` start, cursores, `Z`/Ctrl = botón 1, `X`/Alt = botón 2, `C`/Espacio = botón 3, F3 reset, P pausa, ESC salir.
- **Indy**: Z látigo, X salto. La EEPROM se guarda en `indytemp.eep`.
- **Snow Bros**: Z disparo, X salto.
- **Track & Field**: C = RUN 1, Z = RUN 2 (altérnalos), X = salto.
- **Puzzle Bobble**: cursores izquierda/derecha apuntan, Z dispara. La EEPROM se guarda en `pbobble.eep`.

Para pruebas: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_PORT_FRAMES=1200 SDL_PORT_SHOT=1200 ./build/sdl/<juego>` guarda el frame 1200 en `shot.ppm`.

## ROMs

Para regenerar las ROMs compiladas (`rom_data.c`) a partir de `roms/`: `tools/regen.sh <juego>`. En Indy, Snow Bros y Puzzle Bobble también regenera las tablas de Musashi.

Puzzle Bobble usa el set de MAME `pbobble` (Taito B System, versión japonesa): `pb-ic18.ic18`, `pb-ic2.ic2`, `pb-ic27.ic27`, `pb-ic14.ic14`, `pb-ic9.ic9`, `pb-ic15.ic15` en `roms/pbobble/` (el script comprueba tamaños y CRC). No sirve el Puzzle Bobble de Neo Geo.

## Puzzle Bobble: cómo funciona

- **Hardware** (MAME `taito_b.cpp`, máquina `pbobble`): 68000 a 12 MHz, Z80 a 8 MHz, YM2610B a 8 MHz, vídeo TC0180VCU (dos planos de 64x64 tiles de 16x16 con scroll por bloques, texto de 64x32 de 8x8 y sprites con zoom en un frame buffer), entradas TC0640FIO, buzón TC0140SYT, EEPROM 93C46 y control de volumen MB87078.
- **Planificación**: el 68000 corre por tramos entre los eventos de interrupción (INTH = IRQ3 al empezar el vblank, INTL = IRQ5 8 líneas después); el Z80 le alcanza al final de cada tramo y también cada vez que el 68000 toca el buzón (MAME sincroniza ahí). Los temporizadores del YM2610B y los fines de muestra ADPCM-A se cuentan en ciclos del Z80, exactos a la instrucción.
- **Frame buffer de sprites**: con el borrado activado (como lo usa el juego) cada página solo contiene los sprites dibujados en el último vblank, así que en vez de 2 x 128 KB se guarda la lista de sprites resuelta y se dibuja al componer (mismo resultado, mismo retraso de un frame que el hardware).
- **Atajos con el mismo resultado** (comprobado frame a frame, imagen y audio bit a bit, con y sin ellos):
  - Bucle de espera del 68000 en `$1706`: entre interrupciones el juego hace avanzar su generador aleatorio sin parar (de ahí salen los colores de las burbujas). Las pasadas enteras se calculan en C con el mismo resultado (semilla, D0, flags y ciclos).
  - Bucle principal del Z80 en `$0152` sin órdenes pendientes: se saltan pasadas enteras hasta el siguiente evento del YM2610B.
- **Sonido**: YM2610B sintetizado a partir de las escrituras de registros con marca de tiempo (27 778 Hz en el CYD, 55 555 Hz en el escritorio), FM de 6 canales con LFO, ADPCM-A como ymfm de MAME; el volumen del MB87078 se aplica como en MAME.
- **Verificación contra MAME** (mismas entradas): imagen idéntica en los 800 primeros frames salvo un frame de desfase en la demo (el 68000 de MAME cuenta ciclos algo distinto); como el generador aleatorio depende del tiempo de CPU, la partida diverge después, como es de esperar. El audio coincide en espectro y nivel.

