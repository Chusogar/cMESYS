# cMESYS

Recreativas, ordenadores y consolas clásicas en C11, en un solo árbol de fuentes. Cada máquina tiene un núcleo independiente de la plataforma (`*_core.c`) y un frontend fino por cada destino:

- **Escritorio** con SDL2: Linux y Windows (MinGW).
- **Web** con Emscripten.
- **Android**: un APK por ordenador o consola y una app con todas las recreativas.
- **ESP32 CYD**: la ESP32-2432S028 "Cheap Yellow Display", variante ST7789 de 240x320 por SPI.
- **Waveshare RP2040-PiZero y RP2350-PiZero**, con salida HDMI.

Hay **43 máquinas**: 11 recreativas, 12 ordenadores y 20 consolas. Casi todas son migraciones a C de los drivers de [dsp-cpp](https://github.com/Chusogar/dsp-cpp), con los detalles que faltaban comprobados con MAME. Las ROMs y BIOS no están en el repositorio: se pueden compilar dentro del programa o leerse al arrancar de una carpeta, de la microSD o del almacenamiento del móvil, sueltas o en el `.zip` de MAME (ver [ROMs y BIOS](#roms-y-bios)).

## Índice

1. [Máquinas](#máquinas)
2. [Qué corre en cada plataforma](#qué-corre-en-cada-plataforma)
3. [Estructura del árbol](#estructura-del-árbol)
4. [Compilar](#compilar)
5. [Controles](#controles)
6. [ROMs y BIOS](#roms-y-bios)
7. [Recreativas: cómo funcionan](#recreativas-cómo-funcionan)
8. [Ordenadores: notas](#ordenadores-notas)
9. [Consolas: notas](#consolas-notas)
10. [Estado y pruebas](#estado-y-pruebas)

## Máquinas

El identificador `GAME` es el que se pasa a `make`, `idf.py`, CMake, `build_*.sh` y `tools/regen.sh`.

### Recreativas (`main/drivers/arcades/`)

| Juego | `GAME` | CPUs | Sonido | Origen |
|---|---|---|---|---|
| Indiana Jones and the Temple of Doom (Atari System 1, 1985) | `indytemp` | 68010 + 6502 | YM2151, POKEY, TMS5220 | dsp-cpp `atari_system1.cpp` → `atarisy1/atarisy1_core.c` |
| Road Runner (Atari System 1, 1985) | `roadrunn` | 68010 + 6502 | YM2151, POKEY, TMS5220 | el mismo núcleo que Indy |
| Gauntlet (Atari, 1985) | `gauntlet` | 68010 + 6502 | YM2151, POKEY, TMS5220C | dsp-cpp `gauntlet.cpp` + MAME |
| OutRun (Sega, 1986) | `outrun` | 2 x 68000 + Z80 | YM2151, Sega PCM | dsp-cpp `outrun.cpp` + MAME `segaorun.cpp` |
| Snow Bros (Toaplan, 1990) | `snowbros` | 68000 + Z80 | YM3812 | `zxtiny/zxm/snowbros.c` |
| Track & Field (Konami, 1983) | `trackfld` | Konami-1 (6809) + Z80 | SN76489, VLM5030, DAC | `zxtiny/zxm/trackfld.c` |
| Return of the Invaders (Taito, 1985) | `retofinv` | 3 x Z80 + MC68705P5 | 2 x SN76489 | dsp-cpp `retofinv.cpp` + MAME |
| Pole Position (Namco, 1982) | `polepos` | Z80 + 2 x Z8002 + 4 MCU Namco | WSG de 8 voces, motor, voz, ruido | dsp-cpp `polepos.cpp` + MAME |
| Pole Position II (Namco, 1983) | `polepos2` | igual | igual | el mismo núcleo que `polepos` |
| RoboCop (Data East, 1988) | `robocop` | 68000 + HuC6280 + 6502 | YM2203, YM3812, OKI M6295 | dsp-cpp `dec0.cpp` + MAME |
| Puzzle Bobble (Taito B System, 1994) | `pbobble` | 68000 + Z80 | YM2610B | nuevo, según MAME `taito_b.cpp` |

### Ordenadores (`main/drivers/computers/`)

| Máquina | `GAME` | CPU | Sonido | Driver |
|---|---|---|---|---|
| Sinclair QL (1984, ROM JS) | `ql` | 68008 + 8749 (IPC) | altavoz del IPC | `ql/ql_core.c` |
| ZX Spectrum 48K | `spectrum48` | Z80 | beeper | `zxspec/zx48.c` |
| ZX Spectrum 128K | `spectrum128` | Z80 | beeper + 2 AY (turbosound) | `zxspec/zx128.c` |
| ZX Spectrum +3 | `plus3` | Z80 | beeper + 2 AY, uPD765 | `zxspec/zx128.c` |
| Pentagon 128 | `pentagon` | Z80 | beeper + AY, Beta 128 | `zxspec/zxclone.c` |
| Pentagon 1024 | `pentagon1024` | Z80 | beeper + AY, Beta 128 | `zxspec/zxclone.c` |
| Scorpion ZS-256 | `scorpion` | Z80 | beeper + AY, Beta 128 | `zxspec/zxclone.c` |
| Amstrad CPC 6128 (también 464 / 664 en el escritorio) | `cpc6128` | Z80 | AY-3-8912 | `cpc/cpc_core.c` |
| Commodore 64 (PAL) | `c64` | 6510 (+ 6502 de la 1541) | SID 6581 | `c64/c64_core.c` |
| MSX | `msx1` | Z80 | AY-3-8910 (+ SCC) | `msx/msx_core.c` (TMS9918) |
| MSX2 | `msx2` | Z80 | AY-3-8910 (+ SCC) | `msx/msx_core.c` (V9938) |
| Commodore Amiga 500 (PAL, OCS) | `amiga` | 68000 | Paula | `amiga/amiga_core.c` + `amiga_chipset.c` |
| Apple ][ (Integer BASIC) | `apple2` | 6502 | altavoz | `apple2/apple2_core.c` |
| Apple ][+ (Applesoft) | `apple2p` | 6502 | altavoz | `apple2/apple2_core.c` |
| Apple //e | `apple2e` | 6502 | altavoz | `apple2/apple2_core.c` |
| Apple //e Enhanced | `apple2ee` | 65C02 | altavoz | `apple2/apple2_core.c` |
| Apple IIGS (ROM 3) | `apple2gs` | 65C816 | Ensoniq 5503 | `apple2gs/apple2gs_core.c` |
| Macintosh Plus | `macplus` | 68000 | PWM (VIA) | `macplus/macplus_core.c` |
| Macintosh II | `macii` | 68020 + FPU (Musashi 4) | ASC | `macii/macii_core.c` |
| ZX Spectrum Next (TBBlue 3.02) | `specnext` | Z80N | 3 AY (TurboSound) + DAC | `specnext/specnext_core.c` |
| SAM Coupé (512 KB) | `samcoupe` | Z80 (6 MHz) | SAA1099 | `samcoupe/samcoupe_core.c` |
| Exelvision EXL 100 / EXELTEL | `exl100`, `exeltel` | TMS7020 / TMS7040 + TMS7041 | TMS5220 (voz) | `exelv/exelv_core.c` |

### Consolas (`main/drivers/consoles/`)

| Consola | `GAME` | CPU | Vídeo / sonido | BIOS en `roms/<GAME>/` |
|---|---|---|---|---|
| Sega SG-1000 | `sg1000` | Z80 | TMS9918 / SN76489 | — |
| ColecoVision | `coleco` | Z80 | TMS9918 / SN76489 (+ AY del SGM) | `coleco.rom` |
| Casio PV-2000 | `pv2000` | Z80 | TMS9918 / SN76489 | `hn613128pc64.bin` |
| Casio PV-1000 | `pv1000` | Z80 | propio | — |
| Sega Master System | `sms` | Z80 | 315-5124 / SN76489 + YM2413 | — |
| Sega Game Gear | `gamegear` | Z80 | 315-5246 / SN76489 | — |
| NES | `nes` | 2A03 (6502) | 2C02 / APU | — |
| Atari 2600 | `a2600` | 6507 | TIA + RIOT | — |
| Atari 7800 | `a7800` | 6502 | MARIA / TIA | — |
| Vectrex | `vectrex` | 6809 (`cpu/m6809dsp`) | haz vectorial / AY-3-8912 | `exec_rom.bin` (o como cartucho) |
| Game Boy / Color | `gameboy` | LR35902 | PPU / APU | — |
| PC Engine | `pcengine` | HuC6280 | HuC6270 + HuC6260 / PSG | — |
| Super Cassette Vision | `scv` | uPD7801 | EPOCH TV-1 / uPD1771 | `upd7801g.s01` + `epochtv.chr` |
| WonderSwan / Color | `wonderswan` | V30MZ | propio | — |
| Mega Drive / Genesis | `genesis` | 68000 + Z80 | 315-5313 / YM2612 + SN76489 | — |
| 32X | `sega32x` | 2 x SH-2 + Mega Drive | frame buffers / PWM | `32x_g_bios.bin`, `32x_m_bios.bin`, `32x_s_bios.bin` |
| Super Nintendo | `snes` | 65C816 + SPC700 | S-PPU (modos 0-7) / sin DSP aún, como dsp-cpp | — (IPL del SPC700 incluido) |
| Game Boy Advance | `gba` | ARM7TDMI | PPU (modos 0-5) / APU + FIFO | `gba_bios.bin` |
| PlayStation | `psx` | R3000A + GTE | GPU / SPU, CD-ROM (XA), MDEC | `scph1001.bin` u otra de 512 KB |
| Atari Lynx | `lynx` | 65C02 | Suzy / Mikey (4 canales) | `lynxboot.img` / `lynx.bin` (512 bytes, opcional) |

Las 19 primeras usan la API común de consolas (`common/cons.h`). La Lynx es un driver aparte con sus propios frontends (ver [Atari Lynx](#atari-lynx)).

## Qué corre en cada plataforma

| | Escritorio / Windows | Web | Android | CYD | RP2040 | RP2350 |
|---|---|---|---|---|---|---|
| Recreativas | todas | todas | todas, en una app | todas | todas | todas |
| QL y Spectrum | sí | sí | sí | sí | sí | sí |
| CPC, C64, MSX, MSX2 | sí | sí | sí | sí | — | sí |
| Amiga 500 | sí | sí | sí | — (necesita 1 MB de RAM) | — | sí, con PSRAM |
| Lynx | sí | sí | sí | sí | — | sí |
| Apple ][, ][+, //e, //e Enhanced | sí | sí | sí, en una app | sí | — | sí |
| Apple IIGS, Macintosh Plus, Macintosh II | sí | sí | sí | — (4-5 MB de RAM) | — | sí, con PSRAM² |
| ZX Spectrum Next | sí | sí | sí | — (2 MB de RAM) | — | sí, con PSRAM² |
| SAM Coupé | sí | sí | sí | — (512 KB de RAM + disco) | — | sí, con PSRAM |
| Exelvision EXL 100, EXELTEL | sí | sí | sí, en una app | — | — | sí |
| Las 19 consolas de la API común | sí | sí | sí | sí¹ | sí¹ | sí¹ |

¹ Se compilan todas, pero las que no caben avisan en pantalla con «OUT OF MEMORY FOR THIS CONSOLE». Mega Drive, SNES, 32X, GBA y PlayStation guardan su estado en el heap; en la RP2350 va a la PSRAM. La PlayStation (unos 10 MB) no cabe en ninguna placa.

² En la RP2350 la RAM va a la PSRAM; el Mac II, con 4 MB, va más lento que el real. Los discos duros (y los SmartPort del IIGS) se leen de la microSD; lo que se escribe queda en la PSRAM hasta apagar.

Cada firmware lleva una sola máquina con sus ROMs dentro. No caben todas juntas: la partición de aplicación del CYD es de 0x3F0000 bytes, y solo Puzzle Bobble lleva 2,6 MB de ROMs.

## Estructura del árbol

```
CMakeLists.txt, partitions.csv, sdkconfig.defaults   proyecto ESP-IDF del CYD
Makefile                     escritorio SDL2 (make GAME=<id>; TARGET=windows → .exe; PORT=picohost)
build_all.sh                 firmwares del CYD (SPI 40 y 80 MHz) → dist/
build_pico.sh, build_pico2.sh  firmwares RP2040 / RP2350 → dist/<id>_rp2040_pizero.uf2, <id>_rp2350_pizero.uf2
build_web.sh                 Emscripten → build/web/<id>.html
build_windows.sh|.bat        MinGW → build/win/<id>.exe
main/
  CMakeLists.txt             elige las fuentes de la máquina en el CYD
  cpu/                       m68k (Musashi), m6502 (6502 / 65C02 / 6510), m6805, mc6809, m6809dsp, h6280,
                             mcs48, z80, z80dsp, z8002, mb88xx, lr35902, w65c816, spc700, sh2, arm7tdmi,
                             necv30 (V30MZ), upd7801, psx (R3000A + GTE)
  sound/                     ym2151, ym2413, ym2610 (YM2610/B, YM2203), ym2612, emu8950 (YM3812), pokey,
                             tms5220, vlm5030, sn76489, sn76496, ay8910, sid6581, segapcm, okim6295,
                             polepos_snd, nes_apu, gb_apu, gba_apu, huc6280_psg, wswan_apu, upd1771
  video/                     atari_mo, tc0180vcu, zx8301, mos6569, tms9918, v9938, sega_vdp,
                             sega_315_5313, nes_ppu, gb_ppu, gba_ppu, snes_ppu, huc6270, maria, tia
  machine/                   via6522, mos6526, mos6532, slapstic, tc0140syt, eeprom93c46, i2cmem,
                             sega_315_5195, taito68705, namco5x, zx8302, ql_mdv, zx_tape, zx_snap,
                             zx_rzx, zx_inflate, zx_beta, zx_nec765, zx_pbuf, c1541, msx_cart,
                             msx_disk, psx_* (CD-ROM, DMA, GPU, MDEC, SPU, timers, mandos)
  drivers/
    arcades/<id>/            núcleo, frontends <id>_cyd.c / _sdl.c / _pico.c, rom_data.c (las ROMs
                             dentro), rom_load.c (las que no: del almacenamiento), m68k_game_conf.h
                             y m68k_tables.c (68000), <id>.lf (código en IRAM)
    computers/<id>/          ql, zxspec, cpc, c64, msx, amiga, apple2, apple2gs, macplus, macii; common/
                             con los frontends compartidos (comp_sdl.h, comp_pico.h, comp_cyd.h,
                             comp_sd.h, comp_blk.h, adb_hid.h)
    consoles/<id>/           las 19 consolas de la API común y la Lynx
    consoles/common/         cons.h, cons_sdl.c, cons_pico.c, cons_cyd.c
    consoles/consoles.mk     manifiesto: CONSOLES y, por consola, CONS_SRC_, CONS_DEFS_, CONS_RAM_
  rom/                       romfs.c (las ROMs desde carpetas, .zip de MAME por nombre o CRC, la microSD
                             con romfs_sd.c), rom_embed.h (ROMS=external) y romfs_arcade.h
  ports/
    sdl2/                    ventana, audio, teclado, ritmo, capturas
    web/                     la misma API que sdl2 sobre Emscripten (USE_SDL=2 + ASYNCIFY)
    android/                 build_apk.sh, build_arcade.sh, JNI y Java (ver su README)
    esp32-cyd/               LCD SPI/DMA, táctil XPT2046, menú de ajustes en NVS, DAC, Nunchuk
    rp2040-pizero/           pico_port.c (HDMI, sonido, USB), CMakeLists.txt (RP2040 y RP2350), rp2350/
tools/
  regen.sh <id>              mete las ROMs de roms/<id>/ en rom_data.c (y regenera las tablas de Musashi);
                             --stub <id>: rom_data.c sin ROMs (como está en git)
  gen_rom_stub.py            los stubs de las recreativas
  gen_roms_*.py              un generador por familia; --stub deja un rom_data.c vacío
  setup_pico.sh              baja Pico SDK 2.1.1, PicoDVI y Pico-PIO-USB a deps/pico
  pico_host.c, pico_hot.py, pico_ram.py, conv_z8002.py, z8002_hot.py, gen_m68k_tables.c
roms/<id>/                   las ROMs de cada máquina (no se suben a git)
```

El núcleo de cada máquina no depende de la plataforma. El CYD, el escritorio, la RP2040/RP2350 y Android usan el mismo núcleo; la web reutiliza el frontend `*_sdl.c` sobre `ports/web`.

`consoles.mk` lo leen el `Makefile`, el de la web, `main/CMakeLists.txt`, el CMake de las placas (vía `consoles.cmake`), los `build_*.sh` y `build_apk.sh`. Para añadir una consola basta con añadirla al manifiesto.

## Compilar

Todo compila tal como está en el repositorio: las máquinas leen entonces sus ROMs al arrancar (de `roms/`, la microSD o la carpeta de la app). `tools/regen.sh <id>` las mete dentro del programa, y `ROMS=external` compila sin ninguna aunque estén generadas (ver [ROMs y BIOS](#roms-y-bios)).

### Escritorio (Linux)

```
make GAME=robocop            # → build/sdl/robocop; "make all" compila todas
./build/sdl/robocop --scale 3
./build/sdl/ql juego.mdv     # los ordenadores y las consolas aceptan un fichero en la línea de órdenes
```

Opciones comunes:

| Opción | Efecto |
|---|---|
| `--mute` / `-m` | sin sonido |
| `--scale N` / `-s N` | ventana de N veces la imagen (1-10; por defecto 2) |
| `--roms DIR` | busca las ROMs que no lleva dentro en `DIR/<set>/` y `DIR/<set>.zip` (antes que en `$CMESYS_ROMS`, `./roms` y `roms/` junto al programa) |
| `--help` | lista de opciones |

Para pruebas sin ventana: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_PORT_FRAMES=1200 SDL_PORT_SHOT=1200 ./build/sdl/<id>` guarda el frame 1200 en `shot.ppm`. `SDL_PORT_VERBOSE=1` dice el tamaño de ventana y si hay sonido.

### Windows (MinGW)

En [MSYS2](https://www.msys2.org/), shell MinGW64:

```
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 make
./build_windows.sh              # todas → build/win/*.exe
./build_windows.sh robocop ql   # algunas (o build_windows.bat desde cmd.exe)
ROMS=external ./build_windows.sh   # sin ROMs dentro → build/win-noroms/
```

Desde Linux o macOS se cruza con `mingw-w64` y el SDL2 MinGW de [libsdl.org](https://libsdl.org/download-2.0.php): `SDL2DIR` apunta al prefijo `x86_64-w64-mingw32` del zip. Copia `SDL2.dll` junto al `.exe`.

### Web (Emscripten)

```
source "$EMSDK/emsdk_env.sh"
./build_web.sh                        # o: make -C main/ports/web GAME=robocop
python3 -m http.server --directory build/web 8080
```

`http://localhost:8080/` abre el índice. En la URL, `?frames=n` corta tras n frames. Los ordenadores, la Lynx y las consolas cargan ficheros con la barra «Upload» o arrastrándolos. Las ROMs que la página no lleva dentro las descarga del servidor al arrancar: `roms/<set>.zip` o `roms/<set>/<fichero>` junto al `.html` (en `build/web/roms/`). `ROMS=external ./build_web.sh` compila sin ninguna (`build/web-noroms/`).

### Android

Sin Android Studio ni NDK (clang, lld, aapt, dx y el `android.jar` del SDK de Debian). Detalles en [`main/ports/android/README.md`](main/ports/android/README.md).

```
main/ports/android/build_apk.sh             # todos → dist/<máquina>_android.apk (arm64-v8a y x86_64)
main/ports/android/build_apk.sh lynx snes   # algunos
main/ports/android/build_arcade.sh          # → dist/arcade_android.apk (las 11 recreativas)
ROMS=external main/ports/android/build_apk.sh c64   # sin ROMs dentro del APK
```

Las ROMs que el APK no lleva las busca la app en su carpeta `Android/data/<app>/files/roms/<set>/` (sueltas o el `.zip` de MAME; se llega por USB). Si faltan, la app dice cuáles y ofrece **Importar ROMs…**, que también está en el menú de los ordenadores: copia los ficheros elegidos a esa carpeta.

- **Ordenadores** (`ql`, `zxspec`, `cpc`, `c64`, `msx`, `amiga`, `apple2`, `apple2gs`, `macplus`, `macii`) y **Lynx**: una app por máquina con la interfaz de Hataroid (menú de iconos, ajustes, *Abrir con*) y el teclado en pantalla de dsp-cpp. Los Apple tienen el suyo dibujado por `tools/gen_apple_vkb.py`: el del ][ / ][+ y el del //e (la app `apple2` cambia de uno a otro con el modelo), el ADB del IIGS, el M0110A del Mac Plus y el Apple Extended Keyboard del Mac II. En el Amiga, el IIGS y los Mac la imagen hace de trackpad.
- **Consolas**: un APK por consola, con sus ficheros generados por `tools/gen_consoles_android.py` y un mando en pantalla dibujado por `tools/gen_consoles_vkb.py`: la cruceta o el joystick, sus botones, START / SELECT, el teclado numérico de la ColecoVision y del SCV, la cruz Y de la WonderSwan y el teclado del PV-2000.
- **cMESYS Arcade**: las 11 recreativas en una app con lista de juegos como MAME para Android, joystick en pantalla, botones, START y monedero.

### ESP32 CYD

Con ESP-IDF 5.3:

```
. $IDF_PATH/export.sh
idf.py -B build/robocop -DGAME=robocop build
LCD_SPI_MHZ=80 idf.py -B build/robocop-80 -DGAME=robocop build
./build_all.sh                                   # todas, a 40 y 80 MHz → dist/
esptool.py --chip esp32 -b 921600 write_flash 0x0 dist/<id>_cyd_st7789_spi40mhz_full.bin
```

- `spi80mhz` da más fps si el panel lo aguanta; si ves basura, usa el de 40 MHz.
- `*_app_0x10000.bin` es solo la aplicación, para escribir en 0x10000.
- Prueba sin pantalla en QEMU, que imprime un hash de la imagen cada 300 frames, comparable con el del PC: `CYD_HEADLESS_TEST=1 idf.py -B build/q-<id> -DGAME=<id> build`.

**Primer arranque**:

1. Calibra el táctil: toca las 3 cruces y, en apaisado, una más.
2. Aparece el menú de ajustes durante 6 s, o hasta tocar PLAY: invertir colores, RGB/BGR, girar 180°, volumen 0-4 y recalibrar. Cada máquina guarda sus ajustes en NVS.
3. Mantén BOOT al encender para volver al menú. Tras un cuelgue, el menú muestra el motivo y la traza.

**ROMs**: las que el firmware no lleva dentro (o todas, con `ROMS=external ./build_all.sh`) se leen de la microSD al arrancar: `roms/<set>/`, `roms/<set>.zip` o la raíz de la tarjeta. Si faltan, la pantalla dice cuáles. Solo caben en RAM las de las máquinas pequeñas (BIOS, ordenadores, recreativas de pocas decenas de KB): el CYD no tiene PSRAM.

**Conexiones**: Nunchuk de Wii en CN1 (SDA GPIO22, SCL GPIO27, 3,3 V; se puede conectar en caliente), microSD FAT32 (CS GPIO5), puerto serie a 115200 baudios. Las funciones del 68000 más usadas de cada juego van en IRAM, elegidas en su `.lf`, sin tocar Musashi.

### RP2040-PiZero

[Waveshare RP2040-PiZero](https://www.waveshare.com/wiki/RP2040-PiZero): RP2040 a 252 MHz, 264 KB de RAM, 16 MB de flash, HDMI.

```
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi cmake python3
tools/setup_pico.sh                # una vez
./build_pico.sh                    # recreativas, QL, Spectrum y consolas → dist/<id>_rp2040_pizero.uf2
./build_pico.sh robocop ql         # algunas
```

Para flashear, mantén BOOT al enchufar el USB-C; aparece la unidad `RPI-RP2` y se copia el `.uf2`. Las ROMs que el firmware no lleva (o todas, con `ROMS=external ./build_pico.sh` → `<id>_rp2040_pizero_noroms.uf2`) se leen de la microSD al arrancar (`roms/<set>/`, `roms/<set>.zip`); si faltan, el HDMI dice cuáles.

| Función | Conexión | Notas |
|---|---|---|
| HDMI | D0 26/27, D1 24/25, D2 22/23, CLK 28/29 | 640x480 a 60 Hz desde 320x240. Sin frame buffer: el núcleo 1 pide la imagen por franjas de 4 líneas y las codifica en TMDS. Si una franja no llega a tiempo sale una línea roja |
| Mandos | USB-C nativo | teclado USB y/o mando HID, también por un hub; alimentación con cable OTG / Y |
| Sonido | GPIO4 | PWM de 10 bits, mono; pon un filtro RC (p. ej. 1 kΩ + 10 nF) |
| microSD | SPI0: SCK 18, MOSI 19, MISO 20, CS 21 | FAT16/FAT32 |
| Log | UART0 TX GPIO0 | 115200 baudios; fps cada 5 s |

Con 264 KB de RAM, cada máquina tiene que caber entera: OutRun y Puzzle Bobble necesitan casi 180 KB solo de RAM de placa. `tools/pico_ram.py` dice cuánta queda. El código más usado va en RAM porque la caché de la flash es de 16 KB: el bucle de Musashi, los manejadores calientes de cada juego (sacados de su `.lf` por `tools/pico_hot.py`) y, cuando caben, las CPU y chips de sonido.

Velocidad estimada en el simulador rp2040js a 252 MHz, sin esperas de flash ni el reparto del bus con el HDMI, así que en la placa será algo menor:

| Máquina | Velocidad real | Estimada |
|---|---|---|
| Return of the Invaders | 60,6 fps | sobra (unas 2 veces) |
| Track & Field | 60 fps | ~63 fps |
| Puzzle Bobble | 60 fps | ~80 fps en la demo |
| Gauntlet | 59,9 fps | ~58 fps |
| Road Runner | 59,9 fps | ~53 fps |
| Indiana Jones | 59,9 fps | ~51 fps |
| Sinclair QL | 50,1 fps | ~41 fps |
| Snow Bros | 60 fps | ~38 fps |
| OutRun | 60,1 fps | ~28 fps |
| RoboCop | 57,4 fps | ~26 fps |
| Pole Position / II | 60,6 fps | ~17 fps |

**Pruebas sin la placa**:

- `make GAME=<id> PORT=picohost` compila el frontend `<id>_pico.c` para el PC sobre `tools/pico_host.c`. Por ejemplo, `PICO_FRAMES=1200 PICO_SHOT=600,1200 PICO_KEYS=1@300:10 build/picohost/<id>` guarda `shot_600.ppm` y `shot_1200.ppm`.
- `PICO_HEAP_KB=n` limita el heap a n KB, como en las placas.
- `PICO_SD=<carpeta>` hace de microSD; `PICO_PRESS=66@150,40@155` pulsa F9 y Enter (usos HID) en esos frames.
- `-DPICO_EMU_TEST=ON` en CMake hace un firmware para [rp2040js](https://github.com/wokwi/rp2040js) que escribe los frames por la UART.

### RP2350-PiZero

[Waveshare RP2350-PiZero](https://www.waveshare.com/wiki/RP2350-PiZero): dos Cortex-M33 a 252 MHz, 520 KB de RAM, hueco para 8 MB de PSRAM, 16 MB de flash, HDMI. Usa los mismos frontends `<id>_pico.c` que la RP2040 (`-DPIZERO=2350`). Lo propio de la placa (pines, PSRAM, PIO-USB) viene del puerto `rp2350-pizero` de dsp-cpp.

```
./build_pico2.sh                   # todas → dist/<id>_rp2350_pizero.uf2
./build_pico2.sh lynx amiga        # algunas
USB=native ./build_pico2.sh        # teclado en el USB-C nativo → <id>_rp2350_pizero_usbc.uf2
ROMS=external ./build_pico2.sh     # sin ROMs dentro → <id>_rp2350_pizero_noroms.uf2 (de la microSD)
```

Para flashear, mantén BOOT al enchufar el USB-C nativo; aparece la unidad `RP2350`.

| Función | Conexión | Notas |
|---|---|---|
| HDMI | D0 36/37, D1 34/35, D2 32/33, CLK 38/39 | 640x480 a 60 Hz con imagen completa de 320x240 en SRAM, y otra para doble búfer si cabe. Una máquina lenta enseña menos imágenes por segundo, nunca líneas rojas |
| Mandos | PIO-USB: D+ 28, D- 29 | teclado, mando y ratón USB, también por un hub; incluye el arreglo de la errata RP2350-E9 |
| Sonido | por el HDMI | 44,1 kHz en las *data islands* (pico_lib, MIT) |
| microSD | SPI1: SCK 30, MOSI 31, MISO 40, CS 43 | FAT16/FAT32; los ordenadores y las consolas cargan con el menú F9 (el Amiga, F11) |
| PSRAM | QMI CS1 = GPIO47 | la placa trae el hueco; si hay una, el heap sigue en ella cuando se llena la SRAM. **La necesitan el Amiga** (1 MB de RAM y 880 KB por disco) **y las consolas grandes** |
| Log | UART0 TX GPIO0 | 115200 baudios |

## Controles

### Escritorio, web y RP2040/RP2350 con teclado USB

Comunes a las recreativas: `5`/`6` moneda, `1`/`2` start, cursores, `Z`/Ctrl botón 1, `X`/Alt botón 2, `C`/Espacio botón 3, F3 reset, P pausa, ESC salir. En las placas, un mando USB usa la cruceta, A/B/X como botones 1/2/3, SELECT moneda, START start y SELECT+START reset.

| Recreativa | Botones |
|---|---|
| Indy | Z látigo, X salto; EEPROM en `indytemp.eep` |
| Road Runner | joystick a desviación máxima, Z / X salto; EEPROM en `roadrunn.eep` |
| Gauntlet | jugador 1; Z disparo, X magia (`1` también es magia); EEPROM en `gauntlet.eep` |
| OutRun | izquierda / derecha volante, arriba o Z acelerar, abajo o X frenar, C marcha; RAM con batería en `outrun.nv` |
| Snow Bros | Z disparo, X salto |
| Track & Field | C = RUN 1, Z = RUN 2 (altérnalos), X salto |
| Return of the Invaders | izquierda / derecha, Z disparo; imagen vertical (224x288) |
| Pole Position / II | como OutRun; la carrera empieza sola tras la moneda |
| RoboCop | cursores (arriba / abajo apuntan), Z ataque, X salto |
| Puzzle Bobble | izquierda / derecha apuntan, Z dispara; EEPROM en `pbobble.eep` |

En los **ordenadores** el teclado del PC es el de la máquina, así que las teclas comunes no se aplican. F9 abre el menú de ficheros (zenity/kdialog o el diálogo de Windows, arrastrar y soltar, barra «Upload» en la web). F12 es TURBO: sin límite de velocidad ni sonido.

| Ordenador | Teclas |
|---|---|
| Sinclair QL | Alt = ALT, Retroceso = CTRL+IZQUIERDA, Supr = CTRL+DERECHA, `` ` `` = £. F1 monta o desmonta el cartucho, F9 menú, F6 es el F1 del QL (F2-F5 los suyos), F12 reinicia, F11 cambia la vista en las placas. `QL_MOUNT_AT=n` pulsa F1 en el frame n |
| Spectrum y clones | Ctrl = Symbol Shift, cursores = Caps+5/6/7/8, Retroceso = DELETE, teclado numérico = Kempston. F1 o F6 cinta play/pausa, F3 reset, F5 botón mágico (Pentagon / Scorpion), F12 TURBO |
| CPC | Alt = COPY, Supr = CLR, teclado numérico = f0-f9. F6 cinta, F3 reset, F8 cursores como joystick, F10 monitor verde, F12 TURBO; `--464` / `--664` en el escritorio. Disco: `CAT` y `RUN"nombre`; cinta: `\|TAPE` y `RUN"` |
| C64 | Esc = RUN/STOP, Tab / Ctrl = CTRL, Alt izq. = C=, Inicio = CLR/HOME, AvPág = RESTORE. Joystick en el teclado numérico (8 4 6 2, 0/5 disparo). F11 cinta, F10 reset, F8 puerto 1/2, F12 TURBO. `LOAD"*",8,1` y `RUN` |
| MSX / MSX2 | por posición. F1-F5 los del MSX, F6 = SELECT, Fin = STOP, Alt izq. = GRAPH, Alt dcho. = CODE. F10 reset, F11 saca el cartucho, F8 cursores como joystick, F12 TURBO |
| Apple ][ / //e | Retroceso = ←, Supr = DELETE, Alt izq. / dcho. = Open / Solid Apple. Joystick en el teclado numérico (8 4 6 2, 0 / 5 botones). F9 menú (disco en la unidad 1, que arranca), F10 RESET (CTRL-RESET), F11 apagar y encender, F12 TURBO |
| Apple IIGS | Alt = Option, Windows = ⌘; el ratón es el suyo. F9 menú (discos SmartPort: el primero arranca), F10 CTRL-RESET, F11 apagar y encender, Pausa = RESET, F12 TURBO |
| ZX Spectrum Next | Mayúsculas = CAPS SHIFT, Ctrl = SYMBOL SHIFT; Retroceso DELETE, ` EDIT, Esc BREAK, Tab EXTEND MODE, Bloq Mayús CAPS LOCK, Insert GRAPH, Inicio TRUE VIDEO, Fin INV VIDEO, cursores, ; " , . Joystick Kempston en el teclado numérico. F9 menú (imagen de tarjeta SD o .nex), F7 botón NMI, F8 botón DRIVE, F10 RESET, F11 apagar y encender, F12 TURBO |
| SAM Coupé | Mayúsculas = SHIFT, Ctrl izquierdo = CNTRL, Alt / Ctrl derecho = SYMBOL; Esc, Tab, Bloq Mayús CAPS, Retroceso DELETE, Inicio EDIT, Fin INV, F1-F10 = F1-F9 y F0, cursores, ; : " , . - + =. Joystick 1 (teclas 6-0) en el teclado numérico. F11 menú (disco en la unidad 1 / 2, cinta, BOOT, reproducir / parar la cinta, sacar, reset), F12 TURBO. F9 en BASIC arranca el disco; un disco dado en la línea de órdenes arranca solo |
| Exelvision EXL 100 / EXELTEL | Las teclas de MAME para la máquina: letras y números, Mayúsculas = SHIFT, Ctrl = CTL, Alt izquierdo = FCT, Bloq Mayús = LOCK, Retroceso, Esc, Tab, Inicio = ↖, cursores; - = «# )», = «_ -», ' «+ =», ; «% @», , «? ,», . «. ;», / «/ :», [ «~ ^», ] «< [», \\ «> ]», Alt Gr «* \\». SHIFT, CTL y FCT se mandan antes que la tecla, como en el real. Mando = flechas y ESPACIO. F9 menú (cartucho o casete, reproducir / parar el casete, sacarlos, reset), F12 TURBO |
| Macintosh Plus / II | Alt = Option, Windows (y Ctrl en el Plus) = ⌘; el ratón es el suyo. F9 menú (disquete o disco SCSI, por el tamaño; F11 en el Mac II, cuyo F1-F10 son los suyos), F12 TURBO |
| Amiga 500 | por posición (distribución española). Windows izq. / RePág = Amiga izq., Windows dcha. / AvPág = Amiga dcha., Fin = HELP. Joystick en el teclado numérico, disparo 0 / 5 / Ctrl dcho. F11 menú (DF0 / DF1, expulsar, reset), F12 TURBO. Un clic captura el ratón; el botón central lo suelta. `--kick fichero.rom` arranca con otro Kickstart |

**Consolas** (las 19 de la API común): cursores, Z X C V (o Ctrl / Alt) botones 1-4, ENTER = START, Retroceso o Mayús dcha. = SELECT; segundo mando en el teclado numérico. F9 menú (abrir / sacar cartucho), F3 reset, F12 TURBO. ColecoVision, SCV y WonderSwan leen además sus teclas del teclado del PC; el PV-2000 usa el teclado entero (botones con Ctrl / Alt).

**Lynx**: cursores = cruceta, Z / Ctrl = A, X / Alt = B, 1 / 2 = OPTION 1 / 2, Enter / P = PAUSE. F9 menú, F3 reset, F12 TURBO. Con mando USB en la RP2350: cruceta y A/B, SELECT = OPTION 1, START = PAUSE.

### CYD

En todas las máquinas BOOT o C+Z del Nunchuk echan moneda y pulsan START. Las recreativas reparten la pantalla en zonas táctiles invisibles que se muestran 2,5 s al empezar.

| Recreativa | Zonas táctiles (filas de arriba abajo) | Nunchuk |
|---|---|---|
| Indy | MONEDA+START · ARRIBA · SALTO / IZQ. · LÁTIGO · DCHA. / LÁTIGO · ABAJO · SALTO | Z látigo, C salto |
| Road Runner | MONEDA+START · ARRIBA · SALTO / IZQ. · SALTO · DCHA. / SALTO · ABAJO · SALTO | stick analógico real; Z / C salto |
| Gauntlet | MONEDA+START · ARRIBA · MAGIA / IZQ. · DISPARO · DCHA. / DISPARO · ABAJO · MAGIA | Z disparo, C magia |
| OutRun | MONEDA+START · MARCHA · ACELERAR / IZQ.+ACEL. · ACELERAR · DCHA.+ACEL. / IZQ. · FRENO · DCHA. | X volante, Z acelerar, C frenar, arriba marcha |
| Pole Position / II | como OutRun, con MONEDA arriba a la izquierda | igual que OutRun |
| RoboCop | MONEDA+START · ARRIBA · SALTO / IZQ. · ATAQUE · DCHA. / IZQ.+ATAQUE · ABAJO · DCHA.+ATAQUE | Z ataque, C salto |
| Puzzle Bobble | MONEDA+START · DISPARAR · DCHA. / IZQ. · DISPARAR · DCHA. / IZQ. · DISPARAR · DCHA. | Z o C dispara |
| Return of the Invaders | vertical: barra inferior MONEDA · IZQ.+DISPARO · DISPARO · DCHA.+DISPARO; sobre la imagen, tercios izq. / disparo / dcha. | Z disparo |
| Snow Bros | columnas laterales: moneda, start, nieve / arriba, abajo, saltar; mitades de la imagen = izquierda / derecha | Z nieve, C saltar |
| Track & Field | mitad izquierda CORRER (alterna RUN 1 / RUN 2), mitad derecha SALTAR (mantén para el ángulo) | agita el stick; Z correr, C salto |

Por el puerto serie: `a d w s` mover, `j` / `k` botones, `g` marcha, `5` moneda, `1` start.

**Ordenadores y Lynx**: mantener MEDIA abre el menú de la microSD (FAT32, raíz o `mdv/` / `tape/`, nombres largos); una pulsación corta es cinta play/pausa, o PAUSE en la Lynx. HELP muestra las zonas. El menú también teclea las órdenes de carga (`|TAPE` + `RUN"` en el CPC, `LOAD"*",8,1` + `RUN` en el C64…). Lo que se escribe por el puerto serie llega al teclado de la máquina.

| | fila 1 | fila 2 | fila 3 |
|---|---|---|---|
| QL | MDV · F1 · F2 · VIEW | ← ↑ ↓ → | ESC · ESPACIO · ENTER · F3 |
| CPC | MEDIA · 1 · 2 · HELP | ← ↑ ↓ → | COPY · ESPACIO · ENTER · FUEGO |
| C64 | MEDIA · F1 · RUN/STOP · HELP | ← ↑ ↓ → | ESPACIO · RETURN · 1 · FUEGO |
| MSX | MEDIA · F1 · STOP · HELP | ← ↑ ↓ → | ESPACIO · RETURN · ESC · FUEGO |
| Lynx | MEDIA · OPTION 1 · OPTION 2 · HELP | ← ↑ ↓ → | PAUSE · B · A · A |
| Apple II | MEDIA (toque: CTRL-RESET) · Y · N · HELP | ← ↑ ↓ → | ESC · ESPACIO · RETURN · BOTÓN 0 |

En el QL, MDV monta el cartucho, reinicia y pulsa F1, y QDOS ejecuta `mdv1_boot`; mantenerlo abre el menú de cartuchos. VIEW alterna entre la imagen entera (512x256 reducida promediando) y 1:1. En los Spectrum el menú incluye «TURBO on/off»; por el puerto serie, F12 o Ctrl-T = TURBO y F5 = botón mágico.

## ROMs y BIOS

**Las ROMs y BIOS no se suben nunca a git.** Cada máquina puede tenerlas de dos formas, y las combina:

1. **Dentro del programa**: `tools/regen.sh <id>` lee `roms/<id>/` y las escribe en su `rom_data.c` (y, en las máquinas con 68000, las tablas de Musashi). Los generadores comprueban tamaños y CRC.
2. **Al arrancar, desde el almacenamiento**: lo que su `rom_data.c` no tiene lo busca su `rom_load.c` con `main/rom/romfs.c`. Hace la misma carga y la misma decodificación que el generador, así que la máquina recibe exactamente los mismos datos.

En cada carpeta de búsqueda `D` y para cada nombre de set `S` de la máquina, se busca primero el fichero suelto, después dentro del `.zip` de MAME y por último en cualquier `.zip` de la carpeta del set:

```
D/S/<fichero>      sueltos (el nombre tal cual, en minúsculas o en mayúsculas)
D/S.zip            el .zip de MAME tal cual (por nombre o por CRC: da igual cómo se llamen dentro)
D/S/*.zip          un .zip dentro de la carpeta del set
```

Los sets son los nombres de MAME. Una máquina puede buscar en más de uno:

- Indy y Road Runner, también en `atarisy1`.
- Pole Position, en `namco51` … `namco54`; Pole Position II, además en `polepos`.
- Los Spectrum: `spectrum`, `spec128`, `pent1024`, `scorpio`.
- El Amiga, en `a500`; el 32X, en `32x`.

Las carpetas `D` dependen de la plataforma:

| Plataforma | Dónde |
|---|---|
| Escritorio y Windows | `--roms DIR`, `$CMESYS_ROMS` (varias, separadas por `:` o `;`), `./roms` y `roms/` junto al programa |
| Web | `roms/` junto a la página; se descarga del servidor al arrancar |
| Android | `Android/data/<app>/files/roms` y la carpeta interna de la app (menú **Importar ROMs…**) |
| CYD, RP2040 y RP2350 | `roms/` y la raíz de la microSD (FAT); en la RP2350, en la PSRAM si la hay |

Si falta una ROM obligatoria, la máquina dice cuál es y dónde la ha buscado:

- en el escritorio, en un cuadro de diálogo;
- en las placas, en pantalla;
- en Android, en un aviso con el botón para importarla.

Las BIOS opcionales de las consolas, y la de la Lynx, se usan solo si están.

Para compilar **sin ninguna ROM dentro**, aunque estén generadas, existe `ROMS=external`. Así el binario se puede dar a otros, porque no lleva las ROMs:

| Destino | Orden | Resultado |
|---|---|---|
| Escritorio | `make GAME=<id> ROMS=external` | `build/sdl-noroms/` |
| Windows | `ROMS=external ./build_windows.sh` | `build/win-noroms/` |
| Web | `ROMS=external ./build_web.sh` | `build/web-noroms/` |
| CYD | `ROMS=external ./build_all.sh` | ficheros `..._noroms_...` |
| RP2040 / RP2350 | `ROMS=external ./build_pico.sh` / `./build_pico2.sh` | ficheros `..._noroms.uf2` |
| Android | `ROMS=external main/ports/android/build_apk.sh` / `build_arcade.sh` | APK sin ROMs |

Por debajo, los `rom_data.c` se compilan con `-DCMESYS_ROMS_EXTERNAL`, que deja fuera sus tablas.

En git, cada `rom_data.c` es un stub sin ROMs. Después de generarlos, se vuelve a ese estado con:

```
tools/regen.sh --stub <id>
```

`tools/gen_rom_stub.py` escribe los stubs de las recreativas; los de los ordenadores y consolas los escriben los `--stub` de sus generadores.

### Recreativas (sets de MAME)

| `GAME` | Set | Notas |
|---|---|---|
| `indytemp`, `roadrunn` | `indytemp`, `roadrunn` (rev 2) | más la BIOS `atarisy1` en `roms/atarisy1/` |
| `gauntlet` | `gauntlet` (rev 14) | las PROM no hacen falta |
| `outrun` | `outrun` (rev B) | |
| `retofinv` | `retofinv` | incluye la MC68705P5 y las PROM |
| `polepos`, `polepos2` | `polepos` (World), `polepos2` | más `51xx.bin` … `54xx.bin` de `namco51.zip` … `namco54.zip` |
| `robocop` | `robocop` (World, rev 4) | las PROM no hacen falta |
| `pbobble` | `pbobble` (Taito B, japonesa) | no sirve el de Neo Geo |

### Ordenadores

| `GAME` | Ficheros en `roms/<GAME>/` |
|---|---|
| `ql` | set `ql` de MAME (`ql.js 0000.ic33`, `ql.js 8000.ic34`, `ipc8049.ic24`) y, opcional, el primer `*.mdv` como cartucho del firmware |
| `spectrum48` | `spectrum.zip`: `spectrum.rom` |
| `spectrum128` | `spec128.zip`: `128-0.rom`, `128-1.rom` |
| `plus3` | `plus3.zip`: `plus3-0.rom` … `plus3-3.rom` (v4.0) |
| `pentagon` | `pentagon.zip`: `128p-0.rom`, `128p-1.rom`, `trd503.rom` |
| `pentagon1024` | `pent1024.zip`: lo del Pentagon más `gluk63r.rom` |
| `scorpion` | `scorpio.zip`: `scorp294.rom` (o `scorpion.rom`, `scorp0..3.rom`) |
| `cpc6128` | set `cpc6128` de MAME: `cpc6128.rom` y `cpcados.rom` |
| `c64` | set `c64` de MAME (kernal, BASIC, caracteres) y el DOS de la 1541 (`325302-01.uab4` y `901229-0x.uab5`); sin la 1541, los `.d64` cargan su primer PRG directamente |
| `msx1` | `MSX.ROM`, `DISK.ROM` opcional ([RetroPieBIOS](https://github.com/archtaurus/RetroPieBIOS/tree/master/BIOS)) |
| `msx2` | `MSX2.ROM`, `MSX2EXT.ROM`, `DISK.ROM` opcional |
| `amiga` | set `a500` de MAME: `315093-02.u2` (Kickstart 1.3) |
| `apple2`, `apple2p`, `apple2e`, `apple2ee` | el set de MAME de cada uno (ROM del sistema y de caracteres); la PROM de la Disk II (`341-0027-a.p5`) para los discos |
| `apple2gs` | set `apple2gs` de MAME: `341-0728`, `341-0748` y la ROM de vídeo |
| `macplus` | set `macplus` de MAME: las dos ROM de 64 KB (par e impar) |
| `macii` | set `macii` de MAME: la ROM de 256 KB y la de la Display Card 8*24 |
| `specnext` | set `tbblue` de MAME: `boot-30204.bin` (la ROM de arranque). El sistema (TBBLUE.FW, NextZXOS) va en la imagen de la tarjeta SD |
| `samcoupe` | set `samcoupe` de MAME: `rom30.z5` (ROM 3.0; vale cualquier otra versión del set, 32 KB) |
| `exl100` | set `exl100` de MAME: `exl100in.bin` (TMS7020) y `exl100_7041.bin` (la CPU de E/S) |
| `exeltel` | set `exeltel` de MAME: `exeltel_7040.bin`, `exeltel_7042.bin` (su BAD_DUMP se corrige al cargarla, como en dsp-cpp) y `exeltel14.bin` (la ROM de sistema de 64 KB) |

Cada modelo Spectrum lee solo su directorio y genera su propio `rom_data_<modelo>.c`, así que regenerar uno no pisa otro.

### Consolas

Las BIOS están en la tabla de [Consolas](#consolas-maindriversconsoles), y se buscan como las demás ROMs (por ejemplo, `roms/gba/gba_bios.bin` o `roms/psx/scph1001.bin`). Las consolas que no la necesitan llevan un `rom_data.c` vacío. La GBA y la PlayStation aceptan también la BIOS como primer fichero que se abre. La Lynx funciona sin BIOS con el pequeño cargador de dsp-cpp, pero entonces solo arranca cartuchos sin cifrar.

## Recreativas: cómo funcionan

Todas se han comparado con MAME con las mismas entradas, y en QEMU el firmware del CYD da las mismas huellas de imagen que el PC. Varias usan **atajos con el mismo resultado**: bucles de espera o de sondeo que se calculan en C de golpe, con imagen y audio idénticos bit a bit con y sin ellos.

### Atari System 1: Indy y Road Runner

Un solo núcleo (`atarisy1/`), elegido el juego por sus ROMs. Road Runner añade su programa en posiciones absolutas, el SLAPSTIC 108 y 3 MB de gráficos. Tiene la **INT3** que marcan los objetos móviles con 0xFFFF para reutilizar sprites a media pantalla, tomados de la RAM por franjas como las actualizaciones parciales de MAME. El joystick es analógico (ADC 6/7). La paleta IRGB es la de MAME; con la de dsp-cpp los túneles salían grises. Imagen idéntica a MAME con un frame de desfase.

### Gauntlet

68010 tras un SLAPSTIC 104, 6502 con YM2151, POKEY y TMS5220C, EEPROM 2804. De MAME vienen los espejos de direcciones, el pen 1 de los objetos móviles como sombra, las listas SLIP (`atari_mo_draw_slip`), la IRQ del 6502 en las líneas 32/96/160/224 y los dos LS259. Los ciclos que Musashi corre de más se descuentan del tramo siguiente; sin eso el 68010 iba un 3 % rápido. Imagen idéntica a MAME en demo y partida.

### OutRun

Dos 68000 con un solo Musashi, por turnos en tramos de 4 líneas, Z80 con YM2151 y Sega PCM, mapeador 315-5195, carretera y sprites con zoom. La placa de sonido va aparte (`or_sound.c`), con eventos con marca de tiempo; en el CYD corre entera en el núcleo 0. Las 37 páginas de programa más usadas se copian a RAM. Imagen idéntica a MAME sin desfase; audio con el mismo espectro (±10 %).

### Return of the Invaders

Tres Z80, un MC68705P5 (`machine/taito68705` con `cpu/m6805`), dos SN76489A, imagen ROT90 de 288x224. Imagen idéntica a MAME.

### Pole Position y Pole Position II

Un Z80 y dos Z8002 a 3,072 MHz, el bus Namco 06xx con cuatro MCU MB88 (entradas, voz, volante, ruido), WSG de 8 voces y motor con tres filtros analógicos. Las CPU corren en porciones de 16 ciclos y las MCU se ponen al día en cada flanco del 06xx; sus bucles de espera se cuentan sin ejecutarlos. `cpu/z8002` se generó con `tools/conv_z8002.py`. Es la máquina más pesada del árbol: en el CYD puede no llegar a 60 fps.

### RoboCop

68000 con tres BAC06 y sprites MXC06. Un HuC6280 ejecuta la protección a través de una RAM de doble puerto y se pone al día cada vez que el 68000 toca el buzón. La placa de sonido (6502, YM2203, YM3812, OKI M6295) va aparte, como en OutRun. 108 capturas idénticas a MAME; audio con el mismo espectro (±5 %).

### Puzzle Bobble

68000, Z80 y YM2610B; vídeo TC0180VCU. El frame buffer de sprites se guarda como lista resuelta en vez de 2 x 128 KB, con el mismo resultado. Imagen idéntica a MAME en los 800 primeros frames; después la partida diverge porque el generador aleatorio depende del tiempo de CPU.

### Snow Bros y Track & Field

Vienen de `zxtiny/zxm`, con el YM3812 de `emu8950` y el VLM5030 respectivamente.

## Ordenadores: notas

Los frontends comunes de los ordenadores están en `main/drivers/computers/common/`:

- `comp_sdl.h`: menú F9, diálogo de ficheros, arrastrar y soltar, barra web.
- `comp_pico.h`: menú F9 de la microSD en la RP2350.
- `comp_cyd.h`: zonas táctiles, menú de la SD y «mecanógrafo» de órdenes.
- `comp_sd.h`: ficheros que se quedan en la tarjeta y se leen por páginas de `zx_pbuf`.

### Sinclair QL

68008 a 7,5 MHz, ZX8301 (vídeo), ZX8302 (reloj, enlace con el IPC, microdrives) e IPC 8749. El reparto de tiempo es el de dsp-cpp: el 68008 corre en tramos de 128 ciclos y tras cada uno avanzan el IPC, el enlace serie y el microdrive. Montar un cartucho reinicia la máquina y pulsa F1 en el frame 110. En el CYD la RAM va en 4 bloques de 32 KB y solo se redibujan las franjas escritas. Pantallas de inicio idénticas a MAME; Psion Chess carga del microdrive en unos 32 s.

### ZX Spectrum y clones

Seis modelos sobre el Z80 de dsp-cpp (`cpu/z80dsp`):

| | 48K | 128K / +3 | Pentagon / Pentagon 1024 / Scorpion |
|---|---|---|---|
| Reloj, T/línea × líneas | 3,5 MHz, 224 × 312 | 3,5469 MHz, 228 × 311 | 3,5 MHz, 224 × 320 |
| Contención | memoria, puertos y T internos (Fuse) | tabla del 128K; el +3 sin puertos | ninguna |
| Extras | ULA+ | ULA+, Interface 2; +3: #1FFD y uPD765 | Beta 128 / TR-DOS, #DFFD (1 MB) o #1FFD, Gluk, botón mágico |

| Tipo | Extensiones | Modelos |
|---|---|---|
| Cinta | `.tap`, `.tzx`, `.cdt`, `.csw`, `.pzx` | todos |
| Snapshot | `.sna`, `.z80` | todos |
| Grabación | `.rzx` | todos |
| Disco TR-DOS | `.trd`, `.scl` | Pentagon, Pentagon 1024, Scorpion |
| Disco +3 | `.dsk` y `.dsk` extendido (solo lectura) | +3 |
| Cartucho | `.rom`, `.bin`, `.if2` | 128K, +3 |

El borde se guarda por T-state y cada línea se dibuja al salir el haz. El 48K, el 128K y el +3 dan **la misma imagen y el mismo sonido frame a frame** que dsp-cpp con las mismas ROMs y cintas. La cinta arranca sola cuando la ROM entra en LD-BYTES. En las placas, cintas y discos se quedan en la microSD y se leen por páginas de 256 bytes. En la RP2040, y en el CYD si no hay memoria, los 128 KB altos del Scorpion se guardan dispersos; el Pentagon 1024 tiene 128 KB en las placas.

### Amstrad CPC 6128 y Commodore 64

| | CPC 6128 | C64 |
|---|---|---|
| Ficheros | `.dsk` / `.edsk`, cintas `.cdt` `.tzx` `.csw`, `.sna` | `.d64` / `.g64` (1541 real), `.tap`, `.prg`, `.t64` |
| Imagen | 400×312 (320×240 en placas) | 384×270 (320×240 en placas) |

El C64 hace **carga rápida**: un `LOAD` del dispositivo 8 de un fichero del `.d64` se resuelve de golpe, como las *traps* de VICE. La 1541 sigue emulada para todo lo demás y se «duerme» cuando está ociosa. En las placas se enciende con el primer disco; sin disco, `LOAD"*",8` da DEVICE NOT PRESENT. El CPC guarda la RAM en páginas de 16 KB y la imagen como registros de línea.

### MSX y MSX2

Un solo núcleo (`msx/msx_core.c`) con `video/tms9918`, `video/v9938` (con el motor de comandos), `machine/msx_cart` (ROM simple y más de veinte megaROM, detectados como en openMSX) y `machine/msx_disk` (`.dsk` de 360 / 720 KB). La cinta (`.cas` y bloques MSX de `.tsx` / `.tzx`) se sirve a través de las llamadas de la BIOS, como en fMSX, y la orden de carga se teclea sola. El MSX2 tiene un mapper de 256 KB en el escritorio y de 64 KB en las placas. En la RP2350 escribe directamente en la imagen del HDMI, y un megaROM que no cabe se lee de la microSD según se usa.

### Commodore Amiga 500

68000 a 7,09 MHz, 512 KB de chip RAM y 512 KB de slow RAM. `amiga_chipset.c` contiene Agnus, Denise y Paula: copper, blitter, bitplanos, dual playfield, HAM, EHB, sprites, DMA de disco y audio. Discos `.adf` en DF0 y DF1, solo lectura.

Sobre dsp-cpp se añadieron:

- el teclado, por la línea serie de la CIA-A con *handshake*;
- el DMA de disco con WORDSYNC, necesario para los cargadores propios;
- HPOS real, que pide el Kickstart 3.1.

Probado con Shadow of the Beast y North & South. En la RP2350 la chip RAM y los discos van en la PSRAM.

### Apple II, IIGS y Macintosh

Migrados de dsp-cpp. El **Apple II** es un solo núcleo para los cuatro modelos: 6502 / 65C02, texto de 40 y 80 columnas, baja, alta y doble alta resolución, el codificador del teclado de cada modelo, el joystick y la Disk II en el slot 6 (`machine/diskii`: `.dsk .do .po .nib`, con escritura). El **IIGS** (ROM 3): 65C816, 4 MB (8 en el escritorio), Mega II, Super Hi-Res, el Ensoniq 5503 (`sound/es5503`), ADB, el reloj y cuatro discos SmartPort (`.po .2mg .hdv`, leídos y escritos en su sitio con `machine/blkfile`). El **Mac Plus**: 68000 (Musashi), 4 MB, la VIA, el SCC, la IWM con la disquetera de 800K y la SuperDrive de 1,44 MB (`machine/iwm`, `mac_dsk`), el reloj y el SCSI (`machine/ncr5380`). El **Mac II**: 68020 con FPU (Musashi 4 en `cpu/musashi`), 8 MB, la Display Card 8*24 a 640 x 480, el ASC (`sound/asc`), ADB y SCSI.

Las imágenes son idénticas píxel a píxel a las de dsp-cpp en el arranque, DOS 3.3, ProDOS y GS/OS (Apple II e IIGS) y en System 6.0.8 hasta el Finder (Mac Plus desde el disquete de 1,44 MB; Mac II desde la misma imagen como disco SCSI, en color).

### ZX Spectrum Next

Migrado de dsp-cpp (que sigue el driver `specnext` de MAME): Z80N a 3,5 / 7 / 14 / 28 MHz (el Z80 de `cpu/z80dsp` con las instrucciones del Z80N), 2 MB de SRAM tras la MMU, la ROM de arranque y el modo de configuración, DivMMC con su automapeo, Multiface, los registros Next, la ULA (Timex, ULANext, ULA+), LoRes, Layer 2 (256x192, 320x256, 640x256), el tilemap de 40 / 80 columnas, 128 sprites con anclas y relativos, el copper, el zxnDMA (`machine/zxn_dma`), la CTC, el controlador de interrupciones IM2, tres AY, cuatro DAC y una tarjeta SD por SPI (`machine/sdcard_spi`). Arranca como el real: la ROM de arranque carga TBBLUE.FW de la imagen de la tarjeta (`.img .mmc .hdf`, por ejemplo la de CSpect) y el firmware, NextZXOS; lo que escribe el Next se guarda en la imagen. Los programas `.nex` se cargan cuando NextZXOS ha arrancado. La imagen es la zona VGA de 320x256 a 640 puntos de ancho (los modos de alta resolución conservan cada punto); en la RP2350 se ve a 320x240.

La primera vez con una tarjeta nueva el firmware pide el modo de vídeo (ENTER y luego Y). Idéntico píxel a píxel a dsp-cpp en el arranque; llega al menú de NextZXOS 2.10.

### SAM Coupé

Migrado de dsp-cpp: Z80 a 6 MHz (`cpu/z80dsp`), 384 T-estados por línea y 312 líneas (50,08 Hz), 512 KB de RAM en páginas de 16 KB con el paginado del ASIC (LMPR / HMPR / VMPR), los cuatro modos de pantalla, la CLUT de 16 de 128 colores que la interrupción de línea cambia línea a línea, el borde T-estado a T-estado, el teclado de 9 columnas, el SAA1099 (`sound/saa1099`, en coma fija), dos unidades de disco con el WD1772 (`machine/wd1772`, `machine/sam_disk`: MGT / SAD, SDF y (E)DSK) y las cintas `.tap .tzx` en la línea EAR (`machine/sam_tape`, que reescribe las cabeceras del Spectrum como las del SAM). BOOT pulsa RETURN y F9, como haría uno (la ROM toma la primera tecla tras encender solo para quitar su pantalla de inicio). En escritorio lo que el SAM escribe en un disco MGT se guarda en su fichero al sacarlo; en Android y en la RP2350 se queda en memoria. Se diferencia de dsp-cpp en que WRITE SECTOR ($A0-$BF) sí escribe (dsp-cpp lo tomaba por READ SECTOR). Idéntico píxel a píxel a dsp-cpp al arrancar The Abbey of Crime y el disco 48K (Tapper), en BASIC (modos 1-4, PALETTE, CIRCLE) y al cargar de cinta; en la RP2350 se ve a 320x240.

### Exelvision EXL 100 y EXELTEL

Migrados de dsp-cpp (que sigue el driver `exelv` de MAME): el TMS7020 (EXL 100) o TMS7040 (EXELTEL) a 2,4576 MHz (`cpu/tms7000`, con LVDP en lugar de SWAP R), la CPU de E/S TMS7041 / 7042 con el sintetizador de voz TMS5220 y el teclado de infrarrojos (los bits con sus tiempos, SHIFT / CTL / FCT mandados antes que la tecla), el vídeo TMS3556 (`video/tms3556`: texto, mapa de bits y mixto, 320x250 en 8 colores), los cartuchos (`.bin .rom`, o un `.zip`) en $0200-$7FFF, la ROM de sistema del EXELTEL paginada con ellos, y los casetes `.k7` y `.wav` en carga rápida; en escritorio, lo que graba un SAVE se añade a `<casete>-save.k7`. Idéntico píxel a píxel a dsp-cpp, y con el mismo sonido muestra a muestra, con el cartucho de Tennis y en los menús del EXELTEL. En la RP2350 se ve a 320x240 (se omite una de cada 25 líneas).

## Consolas: notas

Las 19 consolas de la API común están en `main/drivers/consoles/<id>/`. Usan `common/cons.h` (`cons_init`, `cons_load`, `cons_run_frame`, `cons_frame` en RGB565, `cons_pad`, `cons_key`) y un único frontend por plataforma:

| Plataforma | Frontend |
|---|---|
| Escritorio y web | `common/cons_sdl.c` |
| RP2040 / RP2350 | `common/cons_pico.c` |
| CYD | `common/cons_cyd.c` |
| Android | `jni/consoles/m_cons.c` |

Cada núcleo se ha comparado con dsp-cpp con programas de prueba propios, con **0 píxeles distintos**. Cada CPU nueva se comparó con el original C++ instrucción a instrucción: más de un millón de instrucciones aleatorias con ciclos, registros y memoria idénticos. Entre las CPU nuevas están:

- `lr35902`, `w65c816`, `spc700`, `sh2`, `arm7tdmi`, `necv30`, `upd7801` y `psx`;
- `m6809dsp`: la Vectrex usa el 6809 de dsp-cpp porque sus ciclos difieren de los de `mc6809` y torcían el texto.

La PlayStation sigue a ProjectPSX (MIT). La imagen de disco es un `.bin`.

### Atari Lynx

`main/drivers/consoles/lynx/`, migrada de dsp-cpp `atari_lynx.cpp` + `lynx_mikey` + `lynx_suzy` con el 65C02 de `cpu/m6502/m65xx`. No usa la API común. Tiene sus propios frontends (`lynx_sdl.c`, `lynx_pico.c`, `lynx_cyd.c`), construidos sobre los comunes de los ordenadores (`computers/common/comp_*.h`), y en Android usa `jni/computers/m_lynx.c` con su app al estilo de las de ordenador. Corre en escritorio, web, Android, CYD y RP2350, pero no en la RP2040.

- Ficheros: `.lnx` con cabecera, y `.lyx` / `.o` (BS93) con carga rápida.
- Imagen: 160×102, ampliada ×2.
- Velocidad: los temporizadores y canales de Mikey se cuentan en ticks enteros y solo se avanzan cuando uno vence o cuando la CPU toca Mikey o Suzy. Eso supone unas 2,8 veces menos trabajo por imagen; en QEMU el CYD pasa de 43-46 fps a 60 fps con un 60 % del núcleo.
- Probada en el escritorio: la ROM de arranque real descifra y arranca un `.lnx` hecho con cc65 que dibuja con Suzy, y también un `.o` BS93.

## Estado y pruebas

- **Escritorio** (Linux y Windows): todas las máquinas compilan; es donde se han probado y comparado con MAME y dsp-cpp.
- **Android**: todos los APK compilan (los 6 de ordenador, el de la Lynx, los 19 de consola y el de recreativas).
- **CYD**: todas compilan; las recreativas, el QL y los Spectrum se han comprobado en QEMU. Las consolas grandes avisan de que no caben.
- **RP2040 / RP2350**: todas compilan y los frontends se han probado en el PC con `PORT=picohost`. **Nada está probado en una placa real**; el simulador rp2040js no emula el RP2350.
- **Web**: usa los mismos `*_sdl.c` que el escritorio, pero no se ha compilado en este entorno (falta `emcc`).
