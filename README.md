# Arcades para ESP32 CYD (ST7789) y escritorio (SDL2)

Siete máquinas en un único árbol de fuentes, cada una compilable como firmware para la placa ESP32-2432S028 ("Cheap Yellow Display", variante ST7789 240x320 por SPI) o como programa de escritorio con SDL2:

| Juego | `GAME` | CPUs | Sonido | Origen del driver |
|---|---|---|---|---|
| Indiana Jones and the Temple of Doom (Atari System 1, 1985) | `indytemp` | 68010 + 6502 | YM2151, POKEY, TMS5220 | dsp-cpp `atari_system1.cpp` → `atarisy1/atarisy1_core.c` |
| Road Runner (Atari System 1, 1985) | `roadrunn` | 68010 + 6502 | YM2151, POKEY, TMS5220 | dsp-cpp `atari_system1.cpp` → `atarisy1/atarisy1_core.c` (el mismo núcleo que Indy) |
| Gauntlet (Atari, 1985) | `gauntlet` | 68010 + 6502 | YM2151, POKEY, TMS5220C | dsp-cpp `gauntlet.cpp` → `gauntlet/gauntlet_core.c` (detalles verificados con MAME `gauntlet.cpp`) |
| OutRun (Sega, 1986) | `outrun` | 2 x 68000 + Z80 | YM2151, Sega PCM (315-5218) | dsp-cpp `outrun.cpp` → `outrun/or_core.c` (detalles verificados con MAME `segaorun.cpp`) |
| Snow Bros (Toaplan, 1990) | `snowbros` | 68000 + Z80 | YM3812 | `zxtiny/zxm/snowbros.c` |
| Track & Field (Konami, 1983) | `trackfld` | Konami-1 (6809) + Z80 | SN76489, VLM5030, DAC | `zxtiny/zxm/trackfld.c` |
| Puzzle Bobble (Taito B System, 1994) | `pbobble` | 68000 + Z80 | YM2610B (FM + ADPCM-A) | nuevo, según MAME `taito_b.cpp`; chips de dsp-cpp (`ym2610`/`fmopn`, `tc0140syt`, `eeprom93c46`) pasados a C |

Cada firmware lleva un solo juego. Las ROMs van dentro y cada juego configura el 68000 a su manera. No caben todos juntos en los 4 MB de flash (la partición de la aplicación ocupa 0x3F0000 bytes: Puzzle Bobble lleva 2,6 MB de ROMs).

## Estructura

```
CMakeLists.txt            proyecto ESP-IDF (idf.py -DGAME=<juego> build)
Makefile                  escritorio SDL2 (make GAME=<juego>)
build_all.sh              los 14 firmwares (7 juegos x SPI 40/80 MHz) -> dist/, y los 7 de escritorio
partitions.csv, sdkconfig.defaults
main/
  CMakeLists.txt          elige las fuentes del juego
  cpu/
    m68k/                 Musashi (68000/68010); m68kconf.h incluye el m68k_game_conf.h del juego
    m6502/                6502 (port de dsp-cpp)
    mc6809/               6809 / Konami-1
    z80/                  Z80
  sound/                  ym2151, pokey (+pokey_poly17.inc), tms5220, sn76489, vlm5030, emu8950/ (YM3812),
                          ym2610 (YM2610/YM2610B), segapcm (Sega 315-5218)
  machine/                via6522, slapstic, tc0140syt (buzón Taito 68000 <-> Z80), eeprom93c46,
                          sega_315_5195 (mapeador de memoria de Sega)
  video/                  atari_mo (objetos móviles Atari, con las SLIP de Gauntlet), tc0180vcu (vídeo del Taito B System)
  drivers/arcades/
    atarisy1/             núcleo Atari System 1 compartido por indytemp y roadrunn:
                          atarisy1_core.c/.h (máquina), atarisy1_roms.c/.h (carga desde directorios)
    indytemp/             indytemp_cyd.c (frontend CYD), indytemp_sdl.c (frontend SDL2),
                          rom_data.c (ROMs), m68k_game_conf.h, m68k_tables.c, indytemp.lf (IRAM)
    roadrunn/             roadrunn_cyd.c, roadrunn_sdl.c, rom_data.c, m68k_game_conf.h,
                          m68k_tables.c, roadrunn.lf
    gauntlet/             gauntlet_core.c/.h, gauntlet_cyd.c, gauntlet_sdl.c, rom_data.c,
                          m68k_game_conf.h, m68k_tables.c, gauntlet.lf
    outrun/               or_core.c/.h (máquina), or_video.c (imagen), or_sound.c/.h (placa de
                          sonido), outrun_cyd.c, outrun_sdl.c, rom_data.c, m68k_game_conf.h,
                          m68k_tables.c, outrun.lf
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
roms/                     indytemp, roadrunn, atarisy1 (BIOS de Indy y Road Runner), gauntlet, outrun, snowbros, trackfld, pbobble (descomprimidas)
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

### Controles: Road Runner

Táctil en 3x3 zonas sobre la imagen (se muestran 2,5 s al empezar):

| | | |
|---|---|---|
| MONEDA + START | ARRIBA | SALTO |
| IZQUIERDA | SALTO | DERECHA |
| SALTO | ABAJO | SALTO |

- El joystick del juego es analógico: el táctil y las teclas serie dan la desviación máxima; el **Nunchuk** da la posición real del stick (velocidad variable). Z y C = salto (los dos botones saltan; el izquierdo es también START).
- Serie: `a d w s` mover, `j` / `k` salto, `5` moneda, `1` start.
- Las puntuaciones (EEPROM) se guardan en NVS.

### Controles: Gauntlet

Un jugador (el jugador 1, el Guerrero). Táctil en 3x3 zonas sobre la imagen (se muestran 2,5 s al empezar):

| | | |
|---|---|---|
| MONEDA + START | ARRIBA | MAGIA |
| IZQUIERDA | DISPARO | DERECHA |
| DISPARO | ABAJO | MAGIA |

- Mantener DISPARO dispara; al soltarlo el personaje se mueve. MAGIA usa una poción y también sirve para entrar en la partida tras echar la moneda (es lo que hace MONEDA + START).
- **Nunchuk**: stick = joystick de 8 direcciones, Z = disparo, C = magia, C+Z = moneda + start.
- Serie: `a d w s` mover, `j` disparo, `k` magia, `5` moneda, `1` start.
- La imagen de 336 columnas se recorta 8 por cada lado (320x240). Los ajustes y las puntuaciones (EEPROM 2804) se guardan en NVS.

### Controles: OutRun

Táctil en 3x3 zonas sobre la imagen (se muestran 2,5 s al empezar):

| | | |
|---|---|---|
| MONEDA + START | MARCHA | ACELERAR |
| IZQ. + ACELERAR | ACELERAR | DCHA. + ACELERAR |
| IZQUIERDA | FRENO | DERECHA |

- El volante es analógico: el táctil y las teclas serie lo giran del todo; el **Nunchuk** da la posición real del stick (X = volante). Z = acelerar, C = frenar, stick arriba = cambiar de marcha (baja/alta), C+Z = moneda + start.
- Serie: `a` / `d` volante, `w` acelerar, `s` frenar, `g` marcha, `5` moneda, `1` start.
- La imagen es de 320x224: se centra en la pantalla con 8 líneas negras arriba y abajo.
- En el CYD no se guardan las puntuaciones (la RAM con batería del juego son 32 KB, demasiados para NVS).

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
idf.py -B build/indytemp -DGAME=indytemp build       # o roadrunn / gauntlet / outrun / snowbros / trackfld / pbobble
LCD_SPI_MHZ=80 idf.py -B build/indytemp-80 -DGAME=indytemp build
./build_all.sh                                       # todo -> dist/
```

Prueba sin pantalla en QEMU (imprime un hash de la imagen cada 300 frames, comparable con la versión de PC): `CYD_HEADLESS_TEST=1 idf.py -B build/q-<juego> -DGAME=<juego> build`.

## Escritorio (SDL2)

```
make GAME=indytemp        # o roadrunn / gauntlet / outrun / snowbros / trackfld / pbobble; "make all" para todos -> build/sdl/<juego>
./build/sdl/indytemp      # ROMs incluidas; Indy acepta también: ./build/sdl/indytemp <dir indytemp> <dir atarisy1>
./build/sdl/roadrunn      # igual: ./build/sdl/roadrunn <dir roadrunn> <dir atarisy1>
./build/sdl/gauntlet      # ROMs incluidas
./build/sdl/outrun        # ROMs incluidas
```

Teclas comunes: `5`/`6` moneda, `1`/`2` start, cursores, `Z`/Ctrl = botón 1, `X`/Alt = botón 2, `C`/Espacio = botón 3, F3 reset, P pausa, ESC salir.
- **Indy**: Z látigo, X salto. La EEPROM se guarda en `indytemp.eep`.
- **Road Runner**: cursores = joystick (desviación máxima), Z / X salto. La EEPROM se guarda en `roadrunn.eep`.
- **Gauntlet**: jugador 1; Z disparo, X magia (`1` también es magia, para entrar en la partida). La EEPROM se guarda en `gauntlet.eep`.
- **OutRun**: izquierda / derecha volante, arriba o Z acelerar, abajo o X frenar, C / Espacio cambiar de marcha. La RAM con batería (puntuaciones) se guarda en `outrun.nv`.
- **Snow Bros**: Z disparo, X salto.
- **Track & Field**: C = RUN 1, Z = RUN 2 (altérnalos), X = salto.
- **Puzzle Bobble**: cursores izquierda/derecha apuntan, Z dispara. La EEPROM se guarda en `pbobble.eep`.

Para pruebas: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_PORT_FRAMES=1200 SDL_PORT_SHOT=1200 ./build/sdl/<juego>` guarda el frame 1200 en `shot.ppm`.

## ROMs

Para regenerar las ROMs compiladas (`rom_data.c`) a partir de `roms/`: `tools/regen.sh <juego>`. En Indy, Road Runner, Gauntlet, OutRun, Snow Bros y Puzzle Bobble también regenera las tablas de Musashi.

Road Runner usa el set de MAME `roadrunn` (rev 2: `136040-228.11c` ... `136040-231.13a` y el resto de `136040-*`) en `roms/roadrunn/` y la BIOS `atarisy1` en `roms/atarisy1/`, como Indy.

Gauntlet usa el set de MAME `gauntlet` (rev 14): `136037-1307.9a`, `1308.9b`, `205.10a`, `206.10b`, `1409.7a`, `1410.7b`, `120.16r`, `119.16s`, `104.6p` y `111.1a` ... `118.2mn` en `roms/gauntlet/` (`tools/gen_roms_gauntlet.py` comprueba tamaños y CRC; las PROM no hacen falta).

OutRun usa el set de MAME `outrun` (rev B): `epr-10380b.133`, `epr-10381b.132`, `epr-10382b.118`, `epr-10383b.117`, `epr-10327a.76` ... `epr-10330a.57`, `epr-10187.88`, los `opr-*` (tiles, carretera, PCM) y los `mpr-10371.9` ... `mpr-10378.16` (sprites) en `roms/outrun/` (`tools/gen_roms_outrun.py` comprueba tamaños y CRC).

Puzzle Bobble usa el set de MAME `pbobble` (Taito B System, versión japonesa): `pb-ic18.ic18`, `pb-ic2.ic2`, `pb-ic27.ic27`, `pb-ic14.ic14`, `pb-ic9.ic9`, `pb-ic15.ic15` en `roms/pbobble/` (el script comprueba tamaños y CRC). No sirve el Puzzle Bobble de Neo Geo.

## Atari System 1: Indy y Road Runner

Los dos juegos usan el mismo núcleo (`drivers/arcades/atarisy1/`, port en C del `atari_system1.cpp` de dsp-cpp, elegido el juego con el `game` de sus ROMs). Lo propio de Road Runner, como en dsp-cpp y MAME:

- Programa en sus posiciones absolutas ($10000-$7FFFF), SLAPSTIC 108, 3 MB de gráficos (bancos 1-6).
- **INT3**: la interrupción de línea marcada por los objetos móviles con 0xFFFF (lista enlazada del banco actual), que el juego usa para reutilizar sprites a media pantalla. Igual que las actualizaciones parciales de MAME, los objetos de cada franja de líneas se toman de la RAM tal como está cuando esa franja se termina (cambios de banco, de prioridad o de un objeto del banco activo, este último con el +2 de MAME).
- Joystick analógico en los canales 6/7 del ADC (X invertido).
- Atajos con el mismo resultado (imagen y audio bit a bit con y sin ellos): el bucle de espera del 68010 en `$1482C` (avanza la semilla aleatoria en `$400C5E`) y el bucle de sondeo del 6502 en `$80EC` (el mismo código que el de Indy en `$40F1`).
- Paleta IRGB como el `palette_device::IRGB_4444` de MAME (en dsp-cpp, con una tabla de intensidad antigua, los colores de poca intensidad salían demasiado claros: el interior de los túneles se veía gris). También cambia, para bien, algunos tonos oscuros de Indy.
- Verificado contra MAME en la demo: imagen idéntica con un frame de desfase (el mismo que en Indy) salvo el parpadeo de un texto; audio con las mismas notas. En QEMU el firmware da las mismas huellas de imagen que el PC.

## Gauntlet: cómo funciona

- **Hardware** (MAME `gauntlet.cpp`): 68010 a 7,16 MHz tras un SLAPSTIC 104, 6502 a 1,79 MHz con YM2151, POKEY y TMS5220C, EEPROM 2804 de 512 bytes (se desbloquea para una escritura). Vídeo: plano de 64x64 tiles de 8x8 (512x512, scroll X/Y y banco de tiles), objetos móviles enlazados en listas SLIP por franjas de 8 líneas, y texto de 64x31 encima.
- Del driver de dsp-cpp se toman la carga de ROMs, el mapa de memoria y el reparto de tiempo (4 tramos por línea entre 68010 y 6502). De MAME se toman los detalles que dsp-cpp simplifica: los espejos de direcciones, el pen 1 de los objetos móviles como **sombra** (oscurece el plano en vez de pintarlo), el bit 15 del plano como volteo X, las SLIP, la IRQ del 6502 en las líneas 32/96/160/224 (se borra solo con su acuse), los dos LS259 (reset de la CPU de sonido, reset del YM2151, /WS y /RS del TMS5220 y el "squeak") y el mezclador de volúmenes.
- `video/atari_mo` tiene ahora `atari_mo_draw_slip` (el modo SLIP del `atarimo.cpp` de MAME), con recorte por línea para dibujar por franjas.
- Los ciclos que Musashi corre de más al final de cada tramo se descuentan del siguiente (sin eso el 68010 iba un 3 % rápido y la demo se adelantaba 9 frames respecto a MAME).
- **Atajos con el mismo resultado** (imagen y audio bit a bit con y sin ellos, en demo y en partida):
  - Bucle de espera del vblank del 68010 en `$40C22` (`tst.w (A2) / bne / clr.w (A3) / bra`) y el retardo `subq.l #1,D0 / bne` en `$47F0` (preparación de la EEPROM al arrancar).
  - Bucle de sondeo del 6502 en `$4104`: sus 12 instrucciones se hacen en C (mismas lecturas, escrituras, registros, flags y ciclos) y pueden pararse en cualquiera, porque el 6502 corre un cuarto de línea cada vez; se devuelve el control al núcleo justo antes de la instrucción que saldría del bucle.
- **Verificación contra MAME**: imagen idéntica píxel a píxel en la demo y en partida con las mismas entradas (2 frames de desfase en el arranque). El audio tiene la misma forma: la música (YM2151) y los efectos (POKEY) suenan unas 1,5 veces más altos que en MAME y la voz (TMS5220) al mismo nivel, para que no sature. En QEMU el firmware da las mismas huellas de imagen que el PC.

## OutRun: cómo funciona

- **Hardware** (MAME `segaorun.cpp`): dos 68000 a 10 MHz (principal y secundario: el secundario calcula la carretera), Z80 a 4 MHz con YM2151 y el PCM 315-5218 (16 voces), mapeador de memoria 315-5195, i8255, ADC0804 (volante, acelerador, freno). Vídeo: dos planos de tiles estilo System 16B (16 páginas, scroll por fila y por columna, páginas alternativas), capa de texto, generador de carretera y sprites con zoom; 320x224 a 60,05 Hz.
- Del driver de dsp-cpp se toman la estructura, la carga de ROMs y el reparto entre CPUs. De MAME se toman los detalles: el mapeador como componente propio (`machine/sega_315_5195`, regiones configurables por el programa), las prioridades de la carretera, las sombras de los sprites (pen 0xA), la paleta con sombra/brillo por redes de resistencias, el doble búfer de sprites y de carretera, las IRQ2 en el hblank de las líneas 65/129/193, el motor de la cabina (su posición se lee en el ADC y en el i8255) y el ADC con su tiempo de conversión.
- **Dos 68000 con un solo Musashi**: cada CPU guarda su contexto y corren por turnos en tramos de 4 líneas (las líneas con interrupciones o latches cortan el tramo).
- **La placa de sonido va aparte** (`or_sound.c`): el 68000 principal solo le escribe (el latch del mapeador y el reset del Z80), así que recibe esos eventos con marca de tiempo y se pone al día cuando se le pide. En el escritorio corre en el mismo hilo; en el CYD corre entero en el núcleo 0, con el mismo resultado.
- **Atajos con el mismo resultado** (imagen y audio bit a bit con y sin ellos, en demo y en partida): las esperas del principal en `$7DDA` y `$7964` y del secundario en `$1182` (esperan a que cambie una palabra de RAM que en ese tramo nadie puede tocar), y el sondeo del temporizador del YM2151 por el Z80 en `$0A61` (se hace en C instrucción a instrucción y se para donde haga falta).
- En el CYD, las 37 páginas de 1 KB de programa más usadas (el secundario corre de 5) se copian a RAM, parte en IRAM (se leen siempre de 32 en 32 bits).
- **Verificación contra MAME**: imagen idéntica píxel a píxel en la demo y en partida con las mismas entradas (volante, acelerador, moneda, start), sin desfase de frames; solo cambia el indicador de marcha que MAME dibuja como decorado (no es parte de la imagen del juego). Audio con el mismo espectro y nivel (±10 %). En QEMU el firmware da las mismas huellas de imagen que el PC y emula unos 60 fps; no está probado en una placa real (allí el acceso a flash será más lento, y con el LCD a 40 MHz la pantalla no pasa de unos 30 fps: mejor el binario de 80 MHz).

## Puzzle Bobble: cómo funciona

- **Hardware** (MAME `taito_b.cpp`, máquina `pbobble`): 68000 a 12 MHz, Z80 a 8 MHz, YM2610B a 8 MHz, vídeo TC0180VCU (dos planos de 64x64 tiles de 16x16 con scroll por bloques, texto de 64x32 de 8x8 y sprites con zoom en un frame buffer), entradas TC0640FIO, buzón TC0140SYT, EEPROM 93C46 y control de volumen MB87078.
- **Planificación**: el 68000 corre por tramos entre los eventos de interrupción (INTH = IRQ3 al empezar el vblank, INTL = IRQ5 8 líneas después); el Z80 le alcanza al final de cada tramo y también cada vez que el 68000 toca el buzón (MAME sincroniza ahí). Los temporizadores del YM2610B y los fines de muestra ADPCM-A se cuentan en ciclos del Z80, exactos a la instrucción.
- **Frame buffer de sprites**: con el borrado activado (como lo usa el juego) cada página solo contiene los sprites dibujados en el último vblank, así que en vez de 2 x 128 KB se guarda la lista de sprites resuelta y se dibuja al componer (mismo resultado, mismo retraso de un frame que el hardware).
- **Atajos con el mismo resultado** (comprobado frame a frame, imagen y audio bit a bit, con y sin ellos):
  - Bucle de espera del 68000 en `$1706`: entre interrupciones el juego hace avanzar su generador aleatorio sin parar (de ahí salen los colores de las burbujas). Las pasadas enteras se calculan en C con el mismo resultado (semilla, D0, flags y ciclos).
  - Bucle principal del Z80 en `$0152` sin órdenes pendientes: se saltan pasadas enteras hasta el siguiente evento del YM2610B.
- **Sonido**: YM2610B sintetizado a partir de las escrituras de registros con marca de tiempo (27 778 Hz en el CYD, 55 555 Hz en el escritorio), FM de 6 canales con LFO, ADPCM-A como ymfm de MAME; el volumen del MB87078 se aplica como en MAME.
- **Verificación contra MAME** (mismas entradas): imagen idéntica en los 800 primeros frames salvo un frame de desfase en la demo (el 68000 de MAME cuenta ciclos algo distinto); como el generador aleatorio depende del tiempo de CPU, la partida diverge después, como es de esperar. El audio coincide en espectro y nivel.

