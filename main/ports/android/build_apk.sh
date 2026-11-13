#!/usr/bin/env bash
# build_apk.sh - cMESYS's computers for Android: one APK per machine.
#
#   main/ports/android/build_apk.sh [machine ...]       (from anywhere)
#
# machines: ql zxspec cpc c64 msx lynx (default: all of them), each one as
# dist/<machine>_android.apk (package com.chusogar.cmesys.<machine>).
#
# Needs no Android Studio, Gradle or NDK: clang + lld for the core (arm64-v8a
# and x86_64, against the minimal libc headers of jni/libc and a stub of
# Android's libc), and Debian/Ubuntu's Android SDK packages for the rest:
#   apt install clang lld openjdk-17-jdk-headless android-sdk-platform-23 \
#               aapt dalvik-exchange apksigner zipalign python3 python3-pil
# The ROMs come from roms/<set> (tools/android_roms.py, as the generators of
# tools/): they go into the APK, never into git.
#
# Signing: $CMESYS_KEYSTORE (or $QL_KEYSTORE; default
# build/android/debug.keystore, made on the first run; keep it to install
# updates over an installed copy).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/main/ports/android"
OUTBASE="$ROOT/build/android"
M="$ROOT/main"
C="$M/drivers/computers"
SDK="${ANDROID_SDK:-/usr/lib/android-sdk}"
JAR="${ANDROID_JAR:-$(ls "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)}"
[ -n "$JAR" ] && [ -f "$JAR" ] || JAR="$SDK/platforms/android-23/android.jar"
BT="$SDK/build-tools/debian"
DX="${DX:-$BT/dx}"
[ -x "$DX" ] || DX="$(command -v dalvik-exchange || command -v dx)"
KEYSTORE="${CMESYS_KEYSTORE:-${QL_KEYSTORE:-$OUTBASE/debug.keystore}}"

for tool in clang ld.lld aapt javac keytool zipalign apksigner python3; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done
[ -f "$JAR" ] || { echo "missing android.jar (apt install android-sdk-platform-23)" >&2; exit 1; }

MACHINES="${*:-ql zxspec cpc c64 msx lynx}"
RES_INC="$(clang -print-resource-dir)/include"
JDK_INC="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")/include"
mkdir -p "$OUTBASE" "$ROOT/dist"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias cmesys \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=cMESYS QL" >/dev/null 2>&1
fi

# The core's sources of each machine (as the Makefile's)
sources() {
    case $1 in
        ql) echo "$C/ql/ql_core.c $C/ql/m68k_tables.c $M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c
                  $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c $M/cpu/mcs48/mcs48.c
                  $M/machine/zx8302.c $M/machine/ql_mdv.c $M/video/zx8301.c" ;;
        zxspec) echo "$C/zxspec/zxspec_core.c $C/zxspec/zx48.c $C/zxspec/zx128.c $C/zxspec/zxclone.c
                  $M/cpu/z80dsp/z80dsp.c $M/machine/zx_tape.c $M/machine/zx_snap.c $M/machine/zx_rzx.c
                  $M/machine/zx_inflate.c $M/machine/zx_pbuf.c $M/machine/zx_beta.c $M/machine/zx_nec765.c
                  $M/sound/ay8910.c" ;;
        cpc) echo "$C/cpc/cpc_core.c $M/cpu/z80dsp/z80dsp.c $M/sound/ay8910.c $M/machine/zx_nec765.c
                  $M/machine/zx_tape.c $M/machine/zx_pbuf.c $M/machine/zx_inflate.c" ;;
        c64) echo "$C/c64/c64_core.c $M/cpu/m6502/m65xx.c $M/video/mos6569.c $M/sound/sid6581.c
                  $M/machine/mos6526.c $M/machine/c1541.c $M/machine/via6522.c $M/machine/zx_pbuf.c" ;;
        msx) echo "$C/msx/msx_core.c $M/cpu/z80dsp/z80dsp.c $M/video/tms9918.c $M/video/v9938.c
                  $M/sound/ay8910.c $M/machine/msx_cart.c $M/machine/msx_disk.c $M/machine/zx_pbuf.c" ;;
        lynx) echo "$C/lynx/lynx_core.c $M/machine/zx_pbuf.c" ;;
        *) echo "unknown machine: $1 (ql zxspec cpc c64 msx lynx)" >&2; exit 1 ;;
    esac
}

jni_file() {
    case $1 in zxspec) echo m_zx.c ;; *) echo "m_$1.c" ;; esac
}

build() {
    local mach=$1
    local OUT="$OUTBASE/$mach" MD="$HERE/machines/$mach" PKG="com.chusogar.cmesys.$mach"
    local APK="$ROOT/dist/${mach}_android.apk"
    echo "=== $mach -> $APK"
    rm -rf "$OUT"
    mkdir -p "$OUT/apk" "$OUT/classes" "$OUT/gen" "$OUT/lib" "$OUT/assets"

    # --- 1. the core: libcore.so -------------------------------------------------
    local SRCS="$HERE/jni/core_jni.c $HERE/jni/$(jni_file "$mach") $(sources "$mach")"
    for abi in arm64-v8a x86_64; do
        local triple
        case $abi in
            arm64-v8a) triple=aarch64-linux-android21 ;;
            x86_64)    triple=x86_64-linux-android21 ;;
        esac
        local obj="$OUT/obj/$abi" objs=""
        mkdir -p "$obj" "$OUT/lib/$abi"
        for f in $SRCS; do
            local o="$obj/$(basename "$f" .c).o"
            clang --target=$triple -O2 -fPIC -ffreestanding -nostdinc -fno-stack-protector \
                -ffunction-sections -fdata-sections -fvisibility=hidden \
                -isystem "$RES_INC" -isystem "$HERE/jni/libc" -isystem "$JDK_INC" -isystem "$JDK_INC/linux" \
                -DM68K_FLASH_TABLES -I"$HERE/jni" -I"$C/$mach" -I"$M/cpu" -I"$M/cpu/m68k" -I"$M/cpu/mcs48" \
                -I"$M/cpu/m6502" -I"$M/cpu/z80" -I"$M/sound" -I"$M/machine" -I"$M/video" -w -c "$f" -o "$o"
            objs="$objs $o"
        done
        # Android's libc as a stub with the functions the core calls (resolved by
        # the real one when the app loads the library)
        local syms
        syms="$(ld.lld -r $objs -o "$obj/all.o" && llvm-nm -u "$obj/all.o" 2>/dev/null | awk '{print $2}' || nm -u "$obj/all.o" | awk '{print $2}')"
        {
            echo ".text"
            for s in $syms; do printf '.globl %s\n.type %s,%%function\n%s:\n ret\n' "$s" "$s" "$s"; done
        } > "$obj/libc_stub.s"
        clang --target=$triple -c "$obj/libc_stub.s" -o "$obj/libc_stub.o"
        ld.lld -shared -soname libc.so -o "$obj/libc.so" "$obj/libc_stub.o"
        ld.lld -shared -soname libcore.so --gc-sections -z noexecstack -z relro -z now \
            -z max-page-size=16384 --hash-style=both --build-id --no-undefined \
            -o "$OUT/lib/$abi/libcore.so" $objs "$obj/libc.so"
        llvm-strip --strip-unneeded "$OUT/lib/$abi/libcore.so" 2>/dev/null || true
    done
    echo "libcore.so: needs $(echo $syms | tr '\n' ' ')"

    # --- 2. assets: the ROMs, the keyboard, the help -----------------------------------
    python3 "$HERE/tools/android_roms.py" "$mach" "$ROOT/roms" "$OUT/assets"
    cp -r "$MD/assets/." "$OUT/assets/"

    # --- 3. resources, Java, dex -------------------------------------------------------
    sed "s/@PACKAGE@/$PKG/" "$HERE/app/AndroidManifest.xml.in" > "$OUT/AndroidManifest.xml"
    mkdir -p "$OUT/src/com/chusogar/cmesys/app"
    printf 'package com.chusogar.cmesys.app;\n\n/** Written by build_apk.sh: the machine of this APK. */\nfinal class MachineId {\n    static final String ID = "%s";\n\n    private MachineId() {}\n}\n' \
        "$mach" > "$OUT/src/com/chusogar/cmesys/app/MachineId.java"
    aapt package -f -m -J "$OUT/gen" -M "$OUT/AndroidManifest.xml" -S "$MD/res" -S "$HERE/app/res" \
        --auto-add-overlay --custom-package com.chusogar.cmesys.app -I "$JAR" \
        -A "$OUT/assets" --min-sdk-version 21 --target-sdk-version 33 -F "$OUT/apk/base.apk"
    javac -nowarn -Xlint:-options --release 8 -encoding UTF-8 -classpath "$JAR" -d "$OUT/classes" \
        $(find "$HERE/app/src" "$OUT/src" "$OUT/gen" -name '*.java')
    "$DX" --dex --min-sdk-version=21 --output="$OUT/apk/classes.dex" "$OUT/classes"

    # --- 4. the APK ---------------------------------------------------------------------
    cp "$OUT/apk/base.apk" "$OUT/apk/unsigned.apk"
    (cd "$OUT/apk" && zip -q -j unsigned.apk classes.dex)
    (cd "$OUT" && zip -q -r apk/unsigned.apk lib)
    zipalign -f -p 4 "$OUT/apk/unsigned.apk" "$OUT/apk/aligned.apk"
    apksigner sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --ks-key-alias cmesys \
        --min-sdk-version 21 --v4-signing-enabled false --out "$APK" "$OUT/apk/aligned.apk"
    apksigner verify "$APK"
    echo "$APK ($(stat -c %s "$APK") bytes)"
}

for mach in $MACHINES; do
    build "$mach"
done
