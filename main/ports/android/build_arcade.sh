#!/usr/bin/env bash
# build_arcade.sh - cMESYS's arcade games for Android: one app (cMESYS
# Arcade, dist/arcade_android.apk) with the list of games, each game a
# library (libgame_<id>.so) of its desktop frontend (<game>_sdl.c) on
# jni/arcade_port.c, the Android side of main/ports/sdl2/sdl_port.h.
#
#   main/ports/android/build_arcade.sh [game ...]     (default: all of them)
#
# Needs no Android Studio, Gradle or NDK (see build_apk.sh). The ROMs are
# the generated rom_data.c of each game (tools/gen_roms_<game>.py, as for
# the desktop): compiled into its library, inside the APK, never into git.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/main/ports/android"
OUT="$ROOT/build/android/arcade"
M="$ROOT/main"
A="$M/drivers/arcades"
SDK="${ANDROID_SDK:-/usr/lib/android-sdk}"
JAR="${ANDROID_JAR:-$(ls "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)}"
[ -n "$JAR" ] && [ -f "$JAR" ] || JAR="$SDK/platforms/android-23/android.jar"
DX="${DX:-$SDK/build-tools/debian/dx}"
[ -x "$DX" ] || DX="$(command -v dalvik-exchange || command -v dx)"
KEYSTORE="${CMESYS_KEYSTORE:-${QL_KEYSTORE:-$ROOT/build/android/debug.keystore}}"
APK="$ROOT/dist/arcade_android.apk"

for tool in clang ld.lld aapt javac keytool zipalign apksigner python3; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

GAMES="${*:-gauntlet indytemp outrun pbobble polepos polepos2 retofinv roadrunn robocop snowbros trackfld}"
RES_INC="$(clang -print-resource-dir)/include"
JDK_INC="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")/include"
M68K="$M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c"
# Android's libm (the rest of the symbols are libc's)
LIBM="acos asin atan atan2 atan2f atanf ceil ceilf cos cosf cosh exp exp2 exp2f expf fabs fabsf floor floorf fmod
      fmodf frexp hypot ldexp ldexpf llround log log10 log10f log2 log2f logf lrint lrintf lround lroundf modf pow
      powf rint round roundf sin sinf sinh sqrt sqrtf tan tanf tanh tanhf trunc truncf"

mkdir -p "$OUT" "$ROOT/dist"
rm -rf "$OUT/lib" "$OUT/apk" "$OUT/classes" "$OUT/gen"
mkdir -p "$OUT/lib" "$OUT/apk" "$OUT/classes" "$OUT/gen"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias cmesys \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=cMESYS QL" >/dev/null 2>&1
fi

# The sources of each game (as the Makefile's) and its defines
sources() {
    local D="$A/$1"
    case $1 in
        indytemp|roadrunn)
            echo "$D/$1_sdl.c $A/atarisy1/atarisy1_core.c $A/atarisy1/atarisy1_roms.c $D/rom_data.c $M68K
                  $M/sound/ym2151.c $M/sound/pokey.c $M/sound/tms5220.c $M/machine/via6522.c $M/machine/slapstic.c
                  $M/video/atari_mo.c" ;;
        gauntlet)
            echo "$D/gauntlet_sdl.c $D/gauntlet_core.c $D/rom_data.c $M68K $M/sound/ym2151.c $M/sound/pokey.c
                  $M/sound/tms5220.c $M/machine/slapstic.c $M/video/atari_mo.c" ;;
        outrun)
            echo "$D/outrun_sdl.c $D/or_core.c $D/or_video.c $D/or_sound.c $D/rom_data.c $M68K $M/cpu/z80/z80.c
                  $M/sound/ym2151.c $M/sound/segapcm.c $M/machine/sega_315_5195.c" ;;
        snowbros)
            echo "$D/snowbros_sdl.c $D/sb_core.c $D/rom_data.c $M68K $M/cpu/z80/z80.c $M/sound/emu8950/emu8950.c
                  $M/sound/emu8950/emuadpcm.c" ;;
        trackfld)
            echo "$D/trackfld_sdl.c $D/tf_core.c $D/rom_data.c $M/cpu/mc6809/mc6809.c $M/cpu/z80/z80.c
                  $M/sound/sn76489.c $M/sound/vlm5030.c" ;;
        pbobble)
            echo "$D/pbobble_sdl.c $D/pb_core.c $D/rom_data.c $M68K $M/cpu/z80/z80.c $M/sound/ym2610.c
                  $M/machine/tc0140syt.c $M/machine/eeprom93c46.c $M/video/tc0180vcu.c" ;;
        retofinv)
            echo "$D/retofinv_sdl.c $D/rv_core.c $D/rv_video.c $D/rom_data.c $M/cpu/z80/z80.c $M/cpu/m6805/m6805.c
                  $M/machine/taito68705.c $M/sound/sn76489.c" ;;
        robocop)
            echo "$D/robocop_sdl.c $D/rc_core.c $D/rc_video.c $D/rc_sound.c $D/rom_data.c $M68K $M/cpu/h6280/h6280.c
                  $M/sound/ym2610.c $M/sound/okim6295.c $M/sound/emu8950/emu8950.c $M/sound/emu8950/emuadpcm.c" ;;
        polepos|polepos2)
            echo "$D/$1_sdl.c $A/polepos/pp_core.c $A/polepos/pp_video.c $D/rom_data.c $M/cpu/z80/z80.c
                  $M/cpu/z8002/z8002.c $M/cpu/z8002/z8002_tab.c $M/cpu/mb88xx/mb88xx.c $M/machine/namco5x.c
                  $M/sound/polepos_snd.c" ;;
        *) echo "unknown game: $1" >&2; exit 1 ;;
    esac
}

defines() {
    case $1 in
        indytemp|roadrunn) echo "-DAS1_HOOK_PC" ;;
        trackfld) echo "-DMC6809_NO_SETJMP" ;;
        *) echo "" ;;
    esac
}

# --- 1. one library per game --------------------------------------------------------
for game in $GAMES; do
    if [ "$(stat -c %s "$A/$game/rom_data.c")" -lt 4096 ]; then
        echo "$game: rom_data.c has no ROMs: generate it first (tools/gen_roms_*, as for the desktop)" >&2
        exit 1
    fi
    for abi in arm64-v8a x86_64; do
        case $abi in
            arm64-v8a) triple=aarch64-linux-android21 ;;
            x86_64)    triple=x86_64-linux-android21 ;;
        esac
        obj="$OUT/obj/$game/$abi"
        mkdir -p "$obj" "$OUT/lib/$abi"
        objs=""
        for f in "$HERE/jni/arcade_port.c" "$HERE/jni/libc_extra.c" $(sources "$game"); do
            o="$obj/$(basename "$f" .c).o"
            # (the ROMs' object is kept between builds: it is the slow one)
            if [ "$(basename "$f")" != rom_data.c ] || [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
                # (the frontend's main becomes cmesys_game_main, run by arcade_port.c)
                clang --target=$triple -O2 -fPIC -ffreestanding -nostdinc -fno-stack-protector \
                    -ffunction-sections -fdata-sections -fvisibility=hidden $(defines "$game") \
                    -Dmain=cmesys_game_main -isystem "$RES_INC" -isystem "$HERE/jni/libc" \
                    -isystem "$JDK_INC" -isystem "$JDK_INC/linux" -I"$A/$game" -I"$A/atarisy1" -I"$A/polepos" \
                    -I"$M/ports/sdl2" -I"$M/cpu" -I"$M/cpu/m68k" -I"$M/cpu/m6502" -I"$M/cpu/mc6809" -I"$M/cpu/mcs48" \
                    -I"$M/cpu/z80" -I"$M/sound" -I"$M/machine" -I"$M/video" -w -c "$f" -o "$o"
            fi
            objs="$objs $o"
        done
        # Android's libc and libm as stubs with the functions the game calls
        # (resolved by the real ones when the app loads the library)
        ld.lld -r $objs -o "$obj/all.o"
        undef="$(llvm-nm -u "$obj/all.o" | awk '{print $2}')"
        : > "$obj/libc_stub.s"; : > "$obj/libm_stub.s"
        for s in $undef; do
            if echo " $(echo $LIBM) " | grep -q " $s "; then dst="$obj/libm_stub.s"; else dst="$obj/libc_stub.s"; fi
            printf '.text\n.globl %s\n.type %s,%%function\n%s:\n ret\n' "$s" "$s" "$s" >> "$dst"
        done
        clang --target=$triple -c "$obj/libc_stub.s" -o "$obj/libc_stub.o"
        clang --target=$triple -c "$obj/libm_stub.s" -o "$obj/libm_stub.o"
        ld.lld -shared -soname libc.so -o "$obj/libc.so" "$obj/libc_stub.o"
        ld.lld -shared -soname libm.so -o "$obj/libm.so" "$obj/libm_stub.o"
        ld.lld -shared -soname "libgame_$game.so" --gc-sections -z noexecstack -z relro -z now \
            -z max-page-size=16384 --hash-style=both --build-id --no-undefined \
            -o "$OUT/lib/$abi/libgame_$game.so" $objs "$obj/libc.so" "$obj/libm.so"
        llvm-strip --strip-unneeded "$OUT/lib/$abi/libgame_$game.so" 2>/dev/null || true
    done
    echo "libgame_$game.so: $(stat -c %s "$OUT/lib/arm64-v8a/libgame_$game.so") bytes (arm64)"
done

# --- 2. resources, Java, dex ---------------------------------------------------------------
rm -rf "$OUT/assets"
mkdir -p "$OUT/assets"
cp -r "$HERE/arcade/assets/." "$OUT/assets/"
aapt package -f -m -J "$OUT/gen" -M "$HERE/arcade/AndroidManifest.xml" -S "$HERE/arcade/res" -S "$HERE/app/res" \
    --auto-add-overlay --custom-package com.chusogar.cmesys.app -I "$JAR" \
    -A "$OUT/assets" --min-sdk-version 21 --target-sdk-version 33 -F "$OUT/apk/base.apk"
javac -nowarn -Xlint:-options --release 8 -encoding UTF-8 -classpath "$JAR" -d "$OUT/classes" \
    $(find "$HERE/arcade/src" "$OUT/gen" -name '*.java') \
    "$HERE/app/src/com/chusogar/cmesys/app/SoftMenu.java" "$HERE/app/src/com/chusogar/cmesys/app/HelpActivity.java"
"$DX" --dex --min-sdk-version=21 --output="$OUT/apk/classes.dex" "$OUT/classes"

# --- 3. the APK -------------------------------------------------------------------------------
cp "$OUT/apk/base.apk" "$OUT/apk/unsigned.apk"
(cd "$OUT/apk" && zip -q -j unsigned.apk classes.dex)
(cd "$OUT" && zip -q -r apk/unsigned.apk lib)
zipalign -f -p 4 "$OUT/apk/unsigned.apk" "$OUT/apk/aligned.apk"
apksigner sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --ks-key-alias cmesys \
    --min-sdk-version 21 --v4-signing-enabled false --out "$APK" "$OUT/apk/aligned.apk"
apksigner verify "$APK"
echo "$APK ($(stat -c %s "$APK") bytes)"
