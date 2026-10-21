# Track & Field para ESP32 CYD (ST7789)

Port de `zxtiny/zxm/trackfld.c` + `trackfld_audio.c` a la placa ESP32-2432S028 ("Cheap Yellow Display"), variante con pantalla ST7789 240x320 por SPI. Las ROMs de tu zip van dentro del binario.

## Flashear

Binario completo, en la dirección **0x0**:

```
esptool.py --chip esp32 -b 921600 write_flash 0x0 trackfld_cyd_st7789_spi40mhz_full.bin
```

También sirve https://espressif.github.io/esptool-js/ cargando el `_full.bin` en 0x0.

- `spi40mhz`: SPI de la pantalla a 40 MHz (la opción segura).
- `spi80mhz`: SPI a 80 MHz. Si la imagen sale con basura, usa la de 40.
- `_app_0x10000.bin`: solo la aplicación.

## Cambios v2 (congelación de imagen)

- **Audio por DAC en modo asíncrono**: la versión anterior usaba `dac_continuous_write()` en modo síncrono. Ese modo puede quedarse bloqueado para siempre si el DMA termina la cadena justo mientras se le añade un buffer, y entonces la emulación se paraba y la imagen se quedaba congelada.
  - Ahora el DMA gira en bucle y una interrupción rellena cada buffer desde una cola circular (`main/dac_audio.c`). Si no hay datos, saca silencio.
  - Si el DAC dejara de consumir, a los 0,2 s se pasa a marcar el ritmo con un temporizador: el juego sigue, sin sonido, y el monitor serie lo indica ("DAC stopped consuming").
- **Pantalla**: las esperas por fin de transferencia SPI tienen ahora un tiempo límite de 250 ms y nunca bloquean para siempre.
- Comprobado en QEMU: con el DAC sin funcionar (QEMU no lo emula) el juego avanza a 60 fps, y la imagen coincide con la del PC frame a frame.

## Primer arranque

Igual que Snow Bros:

1. Calibración táctil: tres cruces en vertical y un toque más en apaisado.
2. Menú con barras de color de prueba (invertir colores, RGB/BGR, girar 180°, volumen).
3. Mantén pulsado **BOOT** al encender para volver a calibrar y abrir el menú sin límite de tiempo.

## Controles (en apaisado)

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

- **Correr**: cada toque nuevo en una zona de correr pulsa el **otro** botón de carrera (RUN 1 / RUN 2). Así, tocando repetidamente en el mismo sitio, el juego recibe la alternancia que necesita. El táctil resistivo no puede leer dos dedos a la vez.
- **Saltar / lanzar**: mantén pulsado para ajustar el ángulo, como en la máquina.
- **BOOT**: mete una moneda y pulsa START.
- **Mando Nunchuk de Wii** (mismo cableado que Galagino en la CYD): SDA = **GPIO22**, SCL = **GPIO27**, VCC = **3,3 V**, GND = GND, a través del conector CN1 de la placa. No hacen falta resistencias pull-up. Se puede conectar y desconectar en caliente.
  - **C + Z a la vez** = moneda y START (como el botón BOOT).
  - **Stick izquierda / derecha = RUN 1 / RUN 2**: para correr, mueve el stick de lado a lado como en la máquina.
  - **Z**: cada pulsación pulsa el otro botón de carrera.
  - **C = SALTO / LANZAMIENTO** (mantén pulsado para ajustar el ángulo).
- **Teclado por el puerto serie** (115200 baudios), igual que en el escritorio: `c` RUN 1, `z` RUN 2, `x` SALTO, `5` moneda, `1` start.

## Arquitectura

- **Núcleo 1**:
  - 6809 con opcodes cifrados Konami-1 (núcleo de Sean Conner del repo, en IRAM y sin `setjmp` por instrucción).
  - Z80 de sonido (jgz80), SN76489, VLM5030 (voz) y DAC.
  - Audio a 44,1 kHz por el DAC del GPIO26. El `dac_continuous_write` bloqueante marca el ritmo a 60 fps.
- **Núcleo 0**: tiles 64×32 con scroll por fila y sprites, en un framebuffer de 8 bits; envío por SPI DMA. Si la pantalla no llega, se saltan frames.
- **ROMs**: los gráficos se decodifican en el PC (`tools/gen_roms.c`, con las mismas funciones que `trackfld.c`) y quedan en flash. La ROM de programa se copia a RAM.

### Igual que `trackfld.c`

Mapa de memoria y E/S, decodificación Konami-1, 16 tramos por frame con IRQ de vblank, el glue del sonido (latch SN, DAC, control del VLM por offset de dirección, temporizador en 0x8000), los modelos SN76489 y VLM5030, la paleta, las PROMs y el orden de dibujo.

### Diferencias

- **Audio**: el Z80 de sonido se ejecuta intercalado muestra a muestra con la generación de audio (735 muestras por frame). Así las escrituras al SN76489, al DAC y al VLM5030 caen en su muestra. En el escritorio el audio lo generaba el callback de SDL de forma asíncrona, en bloques de 1024 muestras.
- **Fallos de CPU**: si el 6809 encuentra un fallo, se reinicia la máquina en lugar de salir del programa (en las pruebas no se produjo ninguno).
- **Bucles de espera**:
  - **6809**: `LDX <$1A / LDD ,X / ASLA / BCS` en 0x6160. Es el 93% de sus instrucciones.
  - **Z80**: `LD A,(8000h) / AND 2 / JR` en 0x01A9 y 0x01B0. Es el 57% de sus instrucciones.
  - Se saltan vuelta a vuelta con el mismo número de ciclos, y solo si la vuelta entera cabe en el tramo.
  - Verificado en el PC: el audio y los fotogramas salen **idénticos bit a bit** con y sin el salto (3000 frames).

## Compilar

ESP-IDF v5.3.x:

```
cc -O2 -o gen_roms tools/gen_roms.c && ./gen_roms roms main/rom_data.c   # ya generado
idf.py set-target esp32 && idf.py build
LCD_SPI_MHZ=80 idf.py -B build80 build
```

En QEMU (`-icount`, unos 250 MIPS) la emulación usa ~26% del núcleo 1 y la pantalla va a 60 fps. El monitor serie muestra cada 5 s los fps emulados, la carga de CPU, los fps en pantalla y los fallos.

Prueba en el PC (capturas PPM y audio RAW de 44,1 kHz): `host/host_test.c`.
