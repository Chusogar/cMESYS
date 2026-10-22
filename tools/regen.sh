#!/bin/sh
# Regenerates a game's generated sources (ROMs as C arrays, Musashi tables):
#   tools/regen.sh indytemp | snowbros | trackfld
# ROMs: roms/indytemp + roms/atarisy1, roms/snowbros, roms/trackfld (unzipped).
set -e
G=$1
M=main
D=$M/drivers/arcades/$G
T=${TMPDIR:-/tmp}/arcade_regen
mkdir -p $T
CFLAGS="-O1 -w -I$D -I$M/cpu -I$M/cpu/m68k -I$M/cpu/m6502 -I$M/cpu/mc6809 -I$M/sound -I$M/machine -I$M/video"
M68K="$M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c"
case $G in
indytemp)
    cc $CFLAGS -DINDY_HOOK_PC -o $T/gen_roms tools/gen_roms_indytemp.c $D/indytemp_roms.c $D/indytemp_core.c \
       $M/sound/ym2151.c $M/sound/pokey.c $M/sound/tms5220.c $M/machine/via6522.c $M/machine/slapstic.c \
       $M/video/atari_mo.c $M68K -lm
    $T/gen_roms roms/indytemp roms/atarisy1 $D/rom_data.c
    CYC=1 ;;
snowbros)
    python3 tools/gen_roms_snowbros.py roms/snowbros $D/rom_data.c
    CYC=0 ;;
trackfld)
    cc -O2 -o $T/gen_roms tools/gen_roms_trackfld.c
    $T/gen_roms roms/trackfld $D/rom_data.c
    exit 0 ;;
*)  echo "usage: $0 indytemp|snowbros|trackfld"; exit 1 ;;
esac
# Musashi opcode tables in flash, for this game's CPU type
cc -O1 -w -I$D -I$M/cpu/m68k -DCYC_CPU=$CYC -o $T/gen_m68k tools/gen_m68k_tables.c $M/cpu/m68k/m68kcpu.c \
   $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c tools/m68k_stub.c
$T/gen_m68k $M/cpu/m68k/m68kops.c $D/m68k_tables.c
