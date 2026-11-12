#!/usr/bin/env bash
# build_apk.sh - the Sinclair QL for Android (cMESYS's QL driver) -> dist/ql_android.apk
#
#   main/ports/android/build_apk.sh            (from the repository root)
#
# Needs no Android Studio, Gradle or NDK: clang + lld for the core (arm64-v8a
# and x86_64, against the minimal libc headers of jni/libc and a stub of
# Android's libc), and Debian/Ubuntu's Android SDK packages for the rest:
#   apt install clang lld openjdk-17-jdk-headless android-sdk-platform-23 \
#               aapt dalvik-exchange apksigner zipalign python3
# The ROMs come from roms/ql (as tools/gen_roms_ql.py): ql.js / ql.jm system
# ROMs, ipc8049.ic24, minerva.rom if there, and the first *.mdv as the built-in
# cartridge. They go into the APK, never into git.
#
# Signing: $QL_KEYSTORE (default build/android/debug.keystore, made on the
# first run; keep it to install updates over an installed copy).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/main/ports/android"
OUT="$ROOT/build/android"
M="$ROOT/main"
ROMS="$ROOT/roms/ql"
SDK="${ANDROID_SDK:-/usr/lib/android-sdk}"
JAR="${ANDROID_JAR:-$(ls "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)}"
[ -n "$JAR" ] && [ -f "$JAR" ] || JAR="$SDK/platforms/android-23/android.jar"
BT="$SDK/build-tools/debian"
DX="${DX:-$BT/dx}"
[ -x "$DX" ] || DX="$(command -v dalvik-exchange || command -v dx)"
KEYSTORE="${QL_KEYSTORE:-$OUT/debug.keystore}"
APK="$ROOT/dist/ql_android.apk"

for tool in clang ld.lld aapt javac keytool zipalign apksigner python3; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done
[ -f "$JAR" ] || { echo "missing android.jar (apt install android-sdk-platform-23)" >&2; exit 1; }

rm -rf "$OUT/apk" "$OUT/classes" "$OUT/gen" "$OUT/lib" "$OUT/assets"
mkdir -p "$OUT/apk" "$OUT/classes" "$OUT/gen" "$OUT/lib" "$OUT/assets/roms" "$ROOT/dist"

# --- 1. the core: libqlcore.so -------------------------------------------------
SRCS="$HERE/jni/ql_jni.c $M/drivers/computers/ql/ql_core.c $M/drivers/computers/ql/m68k_tables.c
      $M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c
      $M/cpu/mcs48/mcs48.c $M/machine/zx8302.c $M/machine/ql_mdv.c $M/video/zx8301.c"
RES_INC="$(clang -print-resource-dir)/include"
JDK_INC="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")/include"
for abi in arm64-v8a x86_64; do
    case $abi in
        arm64-v8a) triple=aarch64-linux-android21 ;;
        x86_64)    triple=x86_64-linux-android21 ;;
    esac
    obj="$OUT/obj/$abi"
    mkdir -p "$obj" "$OUT/lib/$abi"
    objs=""
    for f in $SRCS; do
        o="$obj/$(basename "$f" .c).o"
        clang --target=$triple -O2 -fPIC -ffreestanding -nostdinc -fno-stack-protector \
            -ffunction-sections -fdata-sections -fvisibility=hidden \
            -isystem "$RES_INC" -isystem "$HERE/jni/libc" -isystem "$JDK_INC" -isystem "$JDK_INC/linux" \
            -DM68K_FLASH_TABLES -I"$M/drivers/computers/ql" -I"$M/cpu" -I"$M/cpu/m68k" -I"$M/cpu/mcs48" \
            -I"$M/machine" -I"$M/video" -w -c "$f" -o "$o"
        objs="$objs $o"
    done
    # Android's libc as a stub with the functions the core calls (resolved by
    # the real one when the app loads the library)
    syms="$(ld.lld -r $objs -o "$obj/all.o" && llvm-nm -u "$obj/all.o" 2>/dev/null | awk '{print $2}' || nm -u "$obj/all.o" | awk '{print $2}')"
    {
        echo ".text"
        for s in $syms; do printf '.globl %s\n.type %s,%%function\n%s:\n ret\n' "$s" "$s" "$s"; done
    } > "$obj/libc_stub.s"
    clang --target=$triple -c "$obj/libc_stub.s" -o "$obj/libc_stub.o"
    ld.lld -shared -soname libc.so -o "$obj/libc.so" "$obj/libc_stub.o"
    ld.lld -shared -soname libqlcore.so --gc-sections -z noexecstack -z relro -z now \
        -z max-page-size=16384 --hash-style=both --build-id \
        -o "$OUT/lib/$abi/libqlcore.so" $objs "$obj/libc.so"
    llvm-strip --strip-unneeded "$OUT/lib/$abi/libqlcore.so" 2>/dev/null || true
    echo "libqlcore.so ($abi): needs $(echo $syms | tr '\n' ' ')"
done

# --- 2. assets: the ROMs ---------------------------------------------------------
python3 - "$ROMS" "$OUT/assets" <<'PY'
import glob, os, sys, zlib
d, out = sys.argv[1], sys.argv[2]
def read(name, size):
    p = os.path.join(d, name)
    if not os.path.exists(p): return None
    b = open(p, "rb").read()
    return b if len(b) == size else None
js = [read("ql.js 0000.ic33", 0x8000), read("ql.js 8000.ic34", 0x4000)]
ipc = read("ipc8049.ic24", 0x800)
if None in js or ipc is None:
    sys.exit("roms/ql needs 'ql.js 0000.ic33', 'ql.js 8000.ic34' and 'ipc8049.ic24' (MAME set ql)")
open(os.path.join(out, "roms/js.rom"), "wb").write(js[0] + js[1])
open(os.path.join(out, "roms/ipc.rom"), "wb").write(ipc)
jm = [read("ql.jm 0000.ic33", 0x8000), read("ql.jm 8000.ic34", 0x4000)]
if None not in jm: open(os.path.join(out, "roms/jm.rom"), "wb").write(jm[0] + jm[1])
mi = read("minerva.rom", 0xc000)
if mi: open(os.path.join(out, "roms/minerva.rom"), "wb").write(mi)
mdvs = sorted(glob.glob(os.path.join(d, "*.mdv")))
if mdvs and os.path.getsize(mdvs[0]) == 255 * 686:
    open(os.path.join(out, "cartridge.mdv"), "wb").write(open(mdvs[0], "rb").read())
    open(os.path.join(out, "cartridge.txt"), "w").write(os.path.splitext(os.path.basename(mdvs[0]))[0])
print("ROMs:", ", ".join(sorted(os.listdir(os.path.join(out, "roms")))),
      "| cartridge:", os.path.basename(mdvs[0]) if mdvs else "none")
PY
cp "$HERE"/app/assets/* "$OUT/assets/"

# --- 3. resources, Java, dex -------------------------------------------------------
aapt package -f -m -J "$OUT/gen" -M "$HERE/app/AndroidManifest.xml" -S "$HERE/app/res" -I "$JAR" \
    -A "$OUT/assets" --min-sdk-version 21 --target-sdk-version 33 -F "$OUT/apk/base.apk"
javac -nowarn -Xlint:-options --release 8 -encoding UTF-8 -classpath "$JAR" -d "$OUT/classes" \
    $(find "$HERE/app/src" "$OUT/gen" -name '*.java')
"$DX" --dex --min-sdk-version=21 --output="$OUT/apk/classes.dex" "$OUT/classes"

# --- 4. the APK ---------------------------------------------------------------------
cp "$OUT/apk/base.apk" "$OUT/apk/unsigned.apk"
(cd "$OUT/apk" && zip -q -j unsigned.apk classes.dex)
(cd "$OUT" && zip -q -r apk/unsigned.apk lib)
zipalign -f -p 4 "$OUT/apk/unsigned.apk" "$OUT/apk/aligned.apk"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias cmesys \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=cMESYS QL" >/dev/null 2>&1
fi
apksigner sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --ks-key-alias cmesys \
    --min-sdk-version 21 --v4-signing-enabled false --out "$APK" "$OUT/apk/aligned.apk"
apksigner verify "$APK"
echo "$APK ($(stat -c %s "$APK") bytes)"
