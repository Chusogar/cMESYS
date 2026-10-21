# cMESYS — Track & Field

Port de `zxtiny/zxm/trackfld.c` + `trackfld_audio.c` a un único repositorio con núcleo compartido y dos backends:

| Plataforma | Carpeta | Descripción |
|---|---|---|
| **ESP32 CYD** | `platforms/ESP32_CYD` | Cheap Yellow Display (ST7789 240×320 SPI) |
| **SDL2** | `platforms/SDL2` | Aplicación de escritorio (Linux / macOS / Windows) |

El emulador común vive en `common/` (6809 Konami-1, Z80 de sonido, SN76489, VLM5030, renderer).

```
common/                 núcleo (tf_core, CPUs, chips, ROMs embebidas)
roms/                   set original de ROMs (entrada de tools/gen_roms)
tools/gen_roms.c        genera common/rom_data.c
host/host_test.c        prueba headless (PPM + audio RAW)
platforms/ESP32_CYD/    proyecto ESP-IDF
platforms/SDL2/         frontend SDL2 + CMake
scripts/build_cyd.sh
scripts/build_sdl2.sh
```

## Compilar SDL2 (escritorio)

Dependencias: CMake, compilador C, SDL2 (`libsdl2-dev` en Debian/Ubuntu).

```bash
./scripts/build_sdl2.sh
./platforms/SDL2/build/cmesys
```

Controles (igual que zxtiny / teclado serie de la CYD):

- **C** / **Z** — RUN 1 / RUN 2  
- **X** — salto / lanzamiento  
- **5** — moneda, **1** — start  
- **F1** — turbo, **Esc** — salir  

Prueba headless (capturas PPM y audio RAW 44,1 kHz):

```bash
mkdir -p out && ./platforms/SDL2/build/cmesys_host_test 3000
```

## Compilar ESP32 CYD

ESP-IDF v5.3.x (con `idf.py` en el PATH):

```bash
. $IDF_PATH/export.sh
./scripts/build_cyd.sh
idf.py -B platforms/ESP32_CYD/build -p /dev/ttyUSB0 flash monitor
```

SPI de pantalla a 80 MHz (opcional):

```bash
LCD_SPI_MHZ=80 ./scripts/build_cyd.sh
```

Regenerar ROMs embebidas tras cambiar ficheros en `roms/`:

```bash
cc -O2 -o gen_roms tools/gen_roms.c && ./gen_roms roms common/rom_data.c
```

## Flashear (CYD)

Binario completo en la dirección **0x0**:

```
esptool.py --chip esp32 -b 921600 write_flash 0x0 trackfld_cyd_st7789_spi40mhz_full.bin
```

También sirve https://espressif.github.io/esptool-js/ cargando el `_full.bin` en 0x0.

- `spi40mhz`: SPI de la pantalla a 40 MHz (opción segura).
- `spi80mhz`: SPI a 80 MHz. Si la imagen sale con basura, usa la de 40.
- `_app_0x10000.bin`: solo la aplicación.

## Primer arranque (CYD)

1. Calibración táctil: tres cruces en vertical y un toque más en apaisado.
2. Menú con barras de color de prueba (invertir colores, RGB/BGR, girar 180°, volumen).
3. Mantén pulsado **BOOT** al encender para volver a calibrar y abrir el menú sin límite de tiempo.

## Controles CYD (apaisado)

```
+-----+------------------------------------+-----+
| CN  |                                    |     |
+-----+   mitad izquierda = CORRER          |  J  |
| ST  |   mitad derecha   = SALTAR          |  M  |
+-----+                                    |  P  |
|  R  |        imagen del juego            |     |
|  U  |          256 x 224                 |     |
|  N  |                                    |     |
+-----+------------------------------------+-----+
```

- **Correr**: cada toque nuevo en una zona de correr pulsa el otro botón de carrera (RUN 1 / RUN 2).
- **Saltar / lanzar**: mantén pulsado para ajustar el ángulo.
- **BOOT**: moneda + START.
- **Nunchuk Wii** (SDA=GPIO22, SCL=GPIO27): C+Z = moneda/START; stick L/R = RUN; Z = alternar carrera; C = salto.
- **Teclado serie** (115200): `c` RUN1, `z` RUN2, `x` SALTO, `5` moneda, `1` start.

## Arquitectura

- **Núcleo común (`common/tf_core`)**: mapa de memoria, Konami-1, 16 tramos/frame + IRQ vblank, glue de sonido, paleta y dibujo — alineado con `trackfld.c` de zxtiny.
- **ESP32 CYD**: núcleo 1 = emulación + DAC GPIO26; núcleo 0 = tiles/sprites + SPI DMA + táctil/Nunchuk.
- **SDL2**: un hilo; `tf_run_frame` genera vídeo y 735 muestras/frame; audio por cola SDL; ritmo a 60 fps (o turbo con F1).

### Diferencias menores respecto al escritorio original de zxtiny

- Audio del Z80 intercalado muestra a muestra (como en CYD), no en callback asíncrono de 1024 muestras.
- Fallos de CPU reinician la máquina en lugar de abortar.
