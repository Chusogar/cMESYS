# Los ordenadores de cMESYS para Android

Los drivers de ordenadores de cMESYS (`main/drivers/computers`) como apps de
Android, una por máquina, todas con la misma interfaz (la del QL, que sigue a
[Hataroid](https://github.com/squalldc/hataroid)) y con el teclado en
pantalla de dsp-cpp:

| APK | Máquina | Teclado en pantalla |
|---|---|---|
| `dist/ql_android.apk` | Sinclair QL (ROM JS, JM, Minerva o propia) | la foto del QL de dsp-cpp (`tools/ql_keyboard.jpg`) |
| `dist/zxspec_android.apk` | ZX Spectrum 48K, 128K, +3, Pentagon, Pentagon 1024, Scorpion | el 48K de dsp-cpp (`gen_zx48_vkb.py`) |
| `dist/cpc_android.apk` | Amstrad CPC 6128 | el CPC 464 español de dsp-cpp (`gen_cpc464_vkb.py`) |
| `dist/c64_android.apk` | Commodore 64 con la 1541 | el de dsp-cpp (`gen_c64_vkb.py`), con los PETSCII de la ROM |
| `dist/msx_android.apk` | MSX1 y MSX2 (con la ROM de disco) | dibujado aquí al estilo de dsp-cpp (`tools/gen_msx_vkb.py`) |
| `dist/lynx_android.apk` | Atari Lynx | los mandos de la consola al estilo de dsp-cpp (`tools/gen_lynx_vkb.py`) |

dsp-cpp no tiene teclado de MSX ni mandos de Lynx: los dibujan
`gen_msx_vkb.py` y `gen_lynx_vkb.py` con el mismo `vkb_common.py` de dsp-cpp
(copiado en `tools/`) y el mismo estilo (teclas esculpidas, sin logotipos).

## La interfaz (igual en todas)

- **Menú inferior** de iconos en dos filas, que se abre con *Atrás*, con el
  icono de la esquina, con el botón *MENÚ* del teclado o con *START* de un
  mando, y que detiene la máquina mientras está abierto: Medios (Microdrive en
  el QL, Cartucho en la Lynx), Reset, Pausa, Ajustes, Salir, Ayuda / Teclado,
  Velocidad (turbo), Pantalla, Teclas, Recientes, Expulsar. Las opciones con
  varias elecciones abren una lista con barra de título, como los submenús de
  Hataroid. El menú de medios de cada máquina abre sus archivos, mueve la
  cinta y **teclea** lo que haga falta para cargar (`LOAD ""`, `CAT`,
  `RUN"`, `|TAPE`, `LOAD"*",8,1`, `LOAD"$",8` + `LIST`…; tras un RETURN
  con más texto detrás espera 2 s emulados, para que el `LOAD` acabe).
- **Teclado en pantalla** (`KeyboardView`): la imagen de
  `machines/<máquina>/assets/keyboard.png` (o la foto del QL, recortada y sin
  el fondo rojo, como hace dsp-cpp) con las teclas de `keyboard.txt`; la tecla
  pulsada se hunde como en dsp-cpp (en el C64, en su hueco). Multitáctil; los
  modificadores se quedan pulsados para la tecla siguiente o se bloquean con
  dos toques; el SHIFT LOCK del C64 se queda bajado; RESTORE es la NMI. MENÚ y
  ▼ (ocultar) van en una parte libre de cada imagen. Debajo de la imagen (o
  superpuesto en horizontal, en Ajustes).
- **Ajustes**: los de cada máquina (modelo o ROM, mando, carga rápida del
  C64…) y los comunes (escalado, suavizado, luces, fps, teclado, vibración,
  sonido).
- **Teclados físicos y mandos**: cada máquina mapea las teclas del PC y los
  botones del mando (joystick Kempston / Sinclair / cursores, puerto 1 o 2,
  dos botones en CPC y MSX, A / B / OPTION / PAUSE en la Lynx).
- **Abrir con**: un archivo se puede abrir con la app desde un gestor de
  archivos.
- **Iconos** de 32×32 al estilo de los de Hataroid (escritorio GEM) y un
  lanzador por máquina, dibujados por `tools/make_icons.py` (hojas de
  contacto: `icons.png`, `launchers.png`). Ayuda de cada app:
  `tools/make_help.py`.

## Compilar

Sin Android Studio, Gradle ni NDK:

```
sudo apt install clang lld openjdk-17-jdk-headless android-sdk-platform-23 \
                 aapt dalvik-exchange apksigner zipalign python3 python3-pil
main/ports/android/build_apk.sh                 # las seis
main/ports/android/build_apk.sh zxspec c64      # algunas
```

- El núcleo de cada máquina (sus fuentes, como en el `Makefile`, más
  `jni/core_jni.c` y su `jni/m_<máquina>.c`) se compila con clang para
  `arm64-v8a` y `x86_64` con las cabeceras mínimas de `jni/libc` (lo que los
  núcleos usan de la libc de Android, bionic) y se enlaza contra un `libc.so`
  de resguardo con esas funciones, que el sistema resuelve al cargar la app.
  Páginas de 16 KB (Android 15+).
- Las ROM salen de `roms/<juego de ROMs>` (`tools/android_roms.py`, con los
  generadores de `tools/`) y van solo en el APK, nunca en git. El Spectrum
  lleva los seis modelos que encuentre; el MSX, MSX1 y MSX2.
- Paquetes `com.chusogar.cmesys.<máquina>`; el del QL es el de antes, así que
  se actualiza encima. Firma: `$CMESYS_KEYSTORE` (o `$QL_KEYSTORE`) o
  `build/android/debug.keystore` (se crea la primera vez; consérvala para
  instalar actualizaciones encima).

## Archivos

| | |
|---|---|
| `jni/core.h`, `jni/core_jni.c` | lo que da cada máquina y las llamadas de `Core` (frames, imagen ARGB, sonido, teclas, joystick, medios) |
| `jni/m_ql.c` … `m_lynx.c` | cada máquina sobre su núcleo de `main/drivers/computers` |
| `jni/libc/` | cabeceras mínimas de bionic para compilar sin el NDK |
| `app/src/.../MachineActivity.java` | pantalla, teclado, menú, medios, teclado físico y mandos |
| `app/src/.../Machine.java` y `QL`, `Spectrum`, `Cpc`, `C64`, `Msx`, `Lynx` | lo propio de cada máquina: ROM, modelos, menú de medios, teclas, mando, textos que se teclean |
| `app/src/.../Emulator.java` | hilo de emulación al ritmo del sonido, cola de teclas, turbo, pausa |
| `app/src/.../ScreenView.java` | imagen escalada (monitor, cuadrada, completa, entera) y luces |
| `app/src/.../KeyboardView.java` | teclado en pantalla (imágenes y teclas de dsp-cpp) |
| `app/src/.../SoftMenu.java` | menú a lo Hataroid (barra de iconos y listas) |
| `machines/<máquina>/` | recursos de cada APK: nombre, ajustes, lanzador, teclado, ayuda |
| `tools/import_dsp_keyboards.py` | los teclados de dsp-cpp (`*_vkb_data.inc`) → `keyboard.png` + `keyboard.txt` |
| `tools/gen_msx_vkb.py`, `gen_lynx_vkb.py` | los teclados de MSX y Lynx |
| `tools/android_roms.py` | las ROM de cada máquina para el APK |
| `tools/make_icons.py`, `make_help.py` | iconos, lanzadores y ayuda |
