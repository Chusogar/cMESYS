# Sinclair QL para Android

El driver QL de cMESYS (`main/drivers/computers/ql`: 68008 de Musashi, ZX8301,
ZX8302, IPC 8749, dos microdrives) en una app de Android. La interfaz sigue a
[Hataroid](https://github.com/squalldc/hataroid):

- **Menú inferior** de iconos en dos filas, que se abre con *Atrás*, con el
  icono de la esquina, con la tecla *MENÚ* del teclado o con *START* de un
  mando, y que detiene el QL mientras está abierto: Microdrive, Reset, Pausa,
  Ajustes, Salir, Ayuda / Teclado, Velocidad (turbo), Pantalla, Teclas QL,
  Recientes, Expulsar. Las opciones con varias elecciones abren una lista con
  barra de título, como los submenús de Hataroid.
- **Teclado del QL en pantalla** (multitáctil; SHIFT, CTRL y ALT fijos para
  la siguiente tecla, o bloqueados con dos toques), superpuesto en horizontal
  y debajo de la imagen en vertical.
- **Ajustes** (`PreferenceActivity`): ROM del sistema (JS, JM, Minerva o una
  de 48 KB elegida), escalado, suavizado, luces de los microdrives, fps,
  teclado, vibración, mando (joystick 1 = cursores + ESPACIO, joystick 2 =
  F1–F4 + F5) y sonido.
- **Iconos** propios de 32×32 al estilo de los de Hataroid (escritorio GEM),
  dibujados por `tools/make_icons.py` (hoja de contacto: `icons.png`).

Los cartuchos `.mdv` (QLAY, 174930 bytes) se abren con el selector de archivos
del sistema y quedan en *Recientes*. Con `roms/ql/*.mdv` la app incluye el
primero como cartucho de ejemplo.

## Compilar

Sin Android Studio, Gradle ni NDK:

```
sudo apt install clang lld openjdk-17-jdk-headless android-sdk-platform-23 \
                 aapt dalvik-exchange apksigner zipalign python3
main/ports/android/build_apk.sh       # → dist/ql_android.apk
```

- El núcleo (`jni/ql_jni.c` + los fuentes del QL) se compila con clang para
  `arm64-v8a` y `x86_64` con las cabeceras mínimas de `jni/libc` (lo que el
  núcleo usa de la libc de Android, bionic) y se enlaza contra un `libc.so`
  de resguardo con esas funciones (`malloc`, `free`, `memcpy`, `memset`,
  `setjmp`, `longjmp`), que el sistema resuelve al cargar la app.
  Páginas de 16 KB (Android 15+).
- Las ROM salen de `roms/ql` (como `tools/gen_roms_ql.py`) y van solo en el
  APK, nunca en git.
- Firma: `$QL_KEYSTORE` o `build/android/debug.keystore` (se crea la primera
  vez; consérvala para instalar actualizaciones encima).

## Archivos

| | |
|---|---|
| `jni/ql_jni.c` | llamadas de `QLNative`: frames, imagen ARGB, sonido, teclas, cartuchos |
| `jni/libc/` | cabeceras mínimas de bionic para compilar sin el NDK |
| `app/src/.../QLActivity.java` | pantalla, teclado, menú, cartuchos, teclado físico y mandos |
| `app/src/.../Emulator.java` | hilo de emulación: 50,08 Hz al ritmo del sonido, turbo, pausa |
| `app/src/.../QLView.java` | imagen 512×256 escalada (4:3, cuadrada, completa, entera) |
| `app/src/.../QLKeyboardView.java` | teclado del QL en pantalla |
| `app/src/.../SoftMenu.java` | menú a lo Hataroid (barra de iconos y listas) |
| `app/src/.../SettingsActivity.java` | ajustes |
| `tools/make_icons.py` | iconos y lanzador |
