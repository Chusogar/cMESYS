# Arcades para ESP32 CYD (ST7789) y escritorio (SDL2)

Tres máquinas en un único árbol de fuentes, cada una compilable como firmware para la placa ESP32-2432S028 ("Cheap Yellow Display", variante ST7789 240x320 por SPI) o como programa de escritorio con SDL2:

| Juego | `GAME` | CPUs | Sonido | Origen del driver |
|---|---|---|---|---|
| Indiana Jones and the Temple of Doom (Atari System 1, 1985) | `indytemp` | 68010 + 6502 | YM2151, POKEY, TMS5220 | dsp-cpp `atari_system1.cpp` → `zxtiny/zxm/indytemp_core.c` |
| Snow Bros (Toaplan, 1990) | `snowbros` | 68000 + Z80 | YM3812 | `zxtiny/zxm/snowbros.c` |
| Track & Field (Konami, 1983) | `trackfld` | Konami-1 (6809) + Z80 | SN76489, VLM5030, DAC | `zxtiny/zxm/trackfld.c` |

Cada firmware lleva un solo juego. Las ROMs van dentro y cada juego configura el 68000 a su manera. Los tres no caben juntos en los 4 MB de flash.

## Estructura

```
CMakeLists.txt            proyecto ESP-IDF (idf.py -DGAME=<juego> build)
Makefile                  escritorio SDL2 (make GAME=<juego>)
build_all.sh              los 6 firmwares (3 juegos x SPI 40/80 MHz) -> dist/, y los 3 de escritorio
partitions.csv, sdkconfig.defaults
main/
  CMakeLists.txt          elige las fuentes del juego
  cpu/
    m68k/                 Musashi (68000/68010); m68kconf.h incluye el m68k_game_conf.h del juego
    m6502/                6502 (port de dsp-cpp)
    mc6809/               6809 / Konami-1
    z80/                  Z80
  sound/                  ym2151, pokey (+pokey_poly17.inc), tms5220, sn76489, vlm5030, emu8950/ (YM3812)
  machine/                via6522, slapstic
  video/                  atari_mo (objetos móviles Atari)
  drivers/arcades/
    indytemp/             indytemp_core.c/.h (máquina), indytemp_cyd.c (frontend CYD),
                          indytemp_sdl.c (frontend SDL2), indytemp_roms.c (carga desde directorios),
                          rom_data.c (ROMs), m68k_game_conf.h, m68k_tables.c, indytemp.lf (IRAM)
    snowbros/             sb_core.c/.h, snowbros_cyd.c, snowbros_sdl.c, rom_data.c,
                          m68k_game_conf.h, m68k_tables.c, snowbros.lf
    trackfld/             tf_core.c/.h, trackfld_cyd.c, trackfld_sdl.c, rom_data.c, trackfld.lf
  ports/
    esp32-cyd/            cyd.c/.h (LCD SPI/DMA, táctil XPT2046 y calibración, menú de ajustes en NVS,
                          diagnóstico de cuelgues, arranque), dac_audio (DAC GPIO26 asíncrono),
                          nunchuk (I2C), font5x7.h
    sdl2/                 sdl_port.c/.h (ventana, cola de audio, teclado, ritmo, capturas)
tools/
  regen.sh <juego>        regenera rom_data.c y m68k_tables.c del juego
  gen_roms_*.{c,py}, gen_m68k_tables.c, m68k_stub.c
roms/                     indytemp, atarisy1 (BIOS de Indy), snowbros, trackfld (descomprimidas)
```

Los núcleos de los juegos (`*_core.c`) no dependen de la plataforma. El frontend CYD y el SDL2 de cada juego usan exactamente el mismo núcleo.

Las funciones del 68000 que van en IRAM (las más usadas, medidas en cada juego) se eligen en el `.lf` del juego. El código de Musashi no se toca. La tabla de ciclos de Musashi también es de cada juego: 68010 en Indy, 68000 en Snow Bros.

## Firmware CYD

### Flashear

```
esptool.py --chip esp32 -b 921600 write_flash 0x0 dist/<juego>_cyd_st7789_spi40mhz_full.bin
```

`spi80mhz` pone la pantalla a 80 MHz y da más fps si tu panel lo aguanta. Si ves basura, usa el de 40 MHz. `*_app_0x10000.bin` es solo la aplicación, para escribir en 0x10000.

### Primer arranque (los tres juegos)

1. Calibración del táctil: toca las 3 cruces y, en apaisado, una más.
2. Menú (6 s, o hasta tocar PLAY): invertir colores, orden RGB/BGR, girar 180°, volumen 0-4, recalibrar, PLAY. Cada juego guarda sus ajustes aparte.
3. Mantén BOOT al encender para volver al menú y recalibrar. Tras un cuelgue, el menú muestra el motivo y la traza.

Comunes a los tres:
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

### Compilar

ESP-IDF 5.3:

```
. $IDF_PATH/export.sh
idf.py -B build/indytemp -DGAME=indytemp build       # o snowbros / trackfld
LCD_SPI_MHZ=80 idf.py -B build/indytemp-80 -DGAME=indytemp build
./build_all.sh                                       # todo -> dist/
```

Prueba sin pantalla en QEMU (imprime un hash de la imagen cada 300 frames, comparable con la versión de PC): `CYD_HEADLESS_TEST=1 idf.py -B build/q-<juego> -DGAME=<juego> build`.

## Escritorio (SDL2)

```
make GAME=indytemp        # o snowbros / trackfld; "make all" para los tres -> build/sdl/<juego>
./build/sdl/indytemp      # ROMs incluidas; Indy acepta también: ./build/sdl/indytemp <dir indytemp> <dir atarisy1>
```

Teclas comunes: `5`/`6` moneda, `1`/`2` start, cursores, `Z`/Ctrl = botón 1, `X`/Alt = botón 2, `C`/Espacio = botón 3, F3 reset, P pausa, ESC salir.
- **Indy**: Z látigo, X salto. La EEPROM se guarda en `indytemp.eep`.
- **Snow Bros**: Z disparo, X salto.
- **Track & Field**: C = RUN 1, Z = RUN 2 (altérnalos), X = salto.

Para pruebas: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_PORT_FRAMES=1200 SDL_PORT_SHOT=1200 ./build/sdl/<juego>` guarda el frame 1200 en `shot.ppm`.

## ROMs

Para regenerar las ROMs compiladas (`rom_data.c`) a partir de `roms/`: `tools/regen.sh <juego>`. En Indy y Snow Bros también regenera las tablas de Musashi.
