#!/usr/bin/env bash
# build_apk.sh - cMESYS's computers and consoles for Android: one APK per machine.
#
#   main/ports/android/build_apk.sh [machine ...]       (from anywhere)
#
# machines: ql zxspec cpc c64 msx lynx amiga atarist apple2 apple2gs macplus macii specnext samcoupe exelv and the consoles of
# main/drivers/consoles/consoles.mk (default: all of them), each one as
# dist/<machine>_android.apk (package com.chusogar.cmesys.<machine>).
#
# Needs no Android Studio, Gradle or NDK: clang + lld for the core (arm64-v8a
# and x86_64, against the minimal libc headers of jni/libc and a stub of
# Android's libc), and Debian/Ubuntu's Android SDK packages for the rest:
#   apt install clang lld openjdk-17-jdk-headless android-sdk-platform-23 \
#               aapt dalvik-exchange apksigner zipalign python3 python3-pil
# The ROMs come from roms/<set> (tools/android_roms.py, as the generators of
# tools/): they go into the APK, never into git. Those not there (or all, with
# ROMS=external) the app reads from its roms folder,
# Android/data/<app>/files/roms/<set>/ (files or MAME's .zip), where its
# "Importar ROMs…" menu entry copies them.
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

CONSOLES="$(sed -n 's/^CONSOLES := //p' "$M/drivers/consoles/consoles.mk")"
MACHINES="${*:-ql zxspec cpc c64 msx lynx amiga atarist apple2 apple2gs macplus macii specnext samcoupe exelv $CONSOLES}"
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
        lynx) echo "$M/drivers/consoles/lynx/lynx_core.c $M/machine/zx_pbuf.c" ;;
        apple2) echo "$C/apple2/apple2_core.c $M/cpu/m6502/m65xx.c $M/machine/diskii.c $M/machine/zx_pbuf.c" ;;
        apple2gs) echo "$C/apple2gs/apple2gs_core.c $M/cpu/w65c816/w65c816.c $M/sound/es5503.c $M/machine/blkfile.c" ;;
        macplus) echo "$C/macplus/macplus_core.c $C/macplus/m68k_tables.c $M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c
                  $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c $M/machine/via6522.c
                  $M/machine/iwm.c $M/machine/mac_dsk.c $M/machine/mac_rtc.c $M/machine/ncr5380.c
                  $M/machine/mac_dcmp.c $M/machine/blkfile.c" ;;
        macii) echo "$C/macii/macii_core.c $M/cpu/musashi/m68kcpu.c $M/cpu/musashi/m68kops.c
                  $M/cpu/musashi/softfloat/softfloat.c $M/machine/via6522.c $M/machine/iwm.c $M/machine/mac_dsk.c
                  $M/machine/ncr5380.c $M/sound/asc.c $M/machine/blkfile.c" ;;
        specnext) echo "$C/specnext/specnext_core.c $M/cpu/z80dsp/z80dsp.c $M/sound/ay8910.c $M/machine/sdcard_spi.c
                  $M/machine/zxn_dma.c $M/machine/blkfile.c" ;;
        exelv) echo "$C/exelv/exelv_core.c $C/exelv/exelv_tape.c $M/cpu/tms7000/tms7000.c $M/video/tms3556.c
                  $M/sound/tms5220.c $M/drivers/consoles/common/cons_util.c" ;;
        samcoupe) echo "$C/samcoupe/samcoupe_core.c $M/cpu/z80dsp/z80dsp.c $M/sound/saa1099.c $M/machine/wd1772.c
                  $M/machine/sam_disk.c $M/machine/sam_tape.c" ;;
        atarist) echo "$C/atarist/st_core.c $C/atarist/st_mfp.c $C/atarist/st_floppy.c $C/atarist/m68k_tables.c
                  $M/sound/ay8910.c $M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c
                  $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c" ;;
        amiga) echo "$C/amiga/amiga_core.c $C/amiga/amiga_chipset.c $C/amiga/m68k_tables.c $M/cpu/m68k/m68kcpu.c
                  $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c" ;;
        *)
            if is_console "$1"; then
                local v
                for v in $(sed -n -e 's/^CONS_COMMON := //p' -e "s/^CONS_SRC_$1 := //p" "$M/drivers/consoles/consoles.mk"); do
                    echo "$M/$v"
                done
            else
                echo "unknown machine: $1 (ql zxspec cpc c64 msx lynx amiga atarist apple2 apple2gs macplus macii specnext samcoupe exelv $CONSOLES)" >&2; exit 1
            fi ;;
    esac
}

is_console() { case " $CONSOLES " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# machines/computers/<id> or machines/consoles/<id>; its glue in jni/computers or jni/consoles
group() { if is_console "$1"; then echo consoles; else echo computers; fi; }

jni_file() {
    if is_console "$1"; then echo consoles/m_cons.c; return; fi
    case $1 in zxspec) echo computers/m_zx.c ;; *) echo "computers/m_$1.c" ;; esac
}

build() {
    local mach=$1
    local OUT="$OUTBASE/$mach" MD="$HERE/machines/$(group "$mach")/$mach" PKG="com.chusogar.cmesys.$mach"
    local APK="$ROOT/dist/${mach}_android.apk"
    echo "=== $mach -> $APK"
    rm -rf "$OUT"
    mkdir -p "$OUT/apk" "$OUT/classes" "$OUT/gen" "$OUT/lib" "$OUT/assets"

    # --- 1. the core: libcore.so -------------------------------------------------
    local SRCS="$HERE/jni/core_jni.c $HERE/jni/libc_extra.c $HERE/jni/$(jni_file "$mach") $(sources "$mach")"
    # the ROMs the APK's assets do not have: from the app's roms folders (romfs);
    # rom_data.c is built without its arrays (the assets are the built-in ROMs)
    local rd=""
    case $mach in
        zxspec) rd="$C/zxspec/rom_data_spectrum48.c $C/zxspec/rom_load.c" ;;
        msx) rd="$C/msx/rom_data_msx1.c $C/msx/rom_load.c" ;;
        lynx) rd="$M/drivers/consoles/lynx/rom_data.c $M/drivers/consoles/lynx/rom_load.c" ;;
        apple2) rd="$C/apple2/rom_data_apple2.c $C/apple2/rom_load.c" ;;
        exelv) rd="$C/exelv/rom_data_exl100.c $C/exelv/rom_load.c" ;;
        ql|cpc|c64|amiga|atarist|apple2gs|macplus|macii|specnext|samcoupe) rd="$C/$mach/rom_data.c $C/$mach/rom_load.c" ;;
    esac
    SRCS="$(echo $SRCS $rd $M/rom/romfs.c $M/machine/zx_inflate.c | tr ' ' '\n' | awk 'NF && !seen[$0]++' | tr '\n' ' ')"
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
                -DM68K_FLASH_TABLES -DCMESYS_ROMS_EXTERNAL -I"$HERE/jni" -I"$C/$mach" -I"$M/drivers/consoles/$mach" -I"$M/drivers/consoles/common" -I"$M/cpu" -I"$M/cpu/m68k" -I"$M/cpu/mcs48" \
                -I"$M/cpu/m6502" -I"$M/cpu/w65c816" -I"$M/cpu/z80" -I"$M/sound" -I"$M/machine" -I"$M/video" -w -c "$f" -o "$o"
            objs="$objs $o"
        done
        # Android's libc as a stub with the functions the core calls (resolved by
        # the real one when the app loads the library)
        local syms
        syms="$(ld.lld -r $objs -o "$obj/all.o" && llvm-nm -u "$obj/all.o" 2>/dev/null | awk '{print $2}' || nm -u "$obj/all.o" | awk '{print $2}')"
        # (and libm's, those of jni/libc/math.h: Musashi 4's FPU)
        local mathsyms csyms="" msyms="" libs="$obj/libc.so"
        mathsyms=" $(grep -o '[a-z0-9]*(' "$HERE/jni/libc/math.h" | tr -d '(' | tr '\n' ' ') "
        for s in $syms; do
            case "$mathsyms" in *" $s "*) msyms="$msyms $s" ;; *) csyms="$csyms $s" ;; esac
        done
        stub() {   # stub <soname> <symbols...>
            local so=$1; shift
            { echo ".text"; for s in "$@"; do printf '.globl %s\n.type %s,%%function\n%s:\n ret\n' "$s" "$s" "$s"; done; } > "$obj/$so.s"
            clang --target=$triple -c "$obj/$so.s" -o "$obj/$so.o"
            ld.lld -shared -soname "$so" -o "$obj/$so" "$obj/$so.o"
        }
        stub libc.so $csyms
        if [ -n "$msyms" ]; then stub libm.so $msyms; libs="$libs $obj/libm.so"; fi
        ld.lld -shared -soname libcore.so --gc-sections -z noexecstack -z relro -z now \
            -z max-page-size=16384 --hash-style=both --build-id --no-undefined \
            -o "$OUT/lib/$abi/libcore.so" $objs $libs
        llvm-strip --strip-unneeded "$OUT/lib/$abi/libcore.so" 2>/dev/null || true
    done
    echo "libcore.so: needs $(echo $syms | tr '\n' ' ')"

    # --- 2. assets: the ROMs, the keyboard, the help -----------------------------------
    # (ROMS=external: none in the APK; the app finds them in its roms folder or imports them)
    if [ "${ROMS:-}" != external ] && ! is_console "$mach"; then
        python3 "$HERE/tools/android_roms.py" "$mach" "$ROOT/roms" "$OUT/assets"
    fi
    cp -r "$MD/assets/." "$OUT/assets/"

    # --- 3. resources, Java, dex -------------------------------------------------------
    sed "s/@PACKAGE@/$PKG/" "$HERE/app/AndroidManifest.xml.in" > "$OUT/AndroidManifest.xml"
    mkdir -p "$OUT/src/com/chusogar/cmesys/app"
    printf 'package com.chusogar.cmesys.app;\n\n/** Written by build_apk.sh: the machine of this APK. */\nfinal class MachineId {\n    static final String ID = "%s";\n\n    private MachineId() {}\n}\n' \
        "$mach" > "$OUT/src/com/chusogar/cmesys/app/MachineId.java"
    python3 "$HERE/tools/console_info.py" "$MD/console.txt" > "$OUT/src/com/chusogar/cmesys/app/ConsoleInfo.java"
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
