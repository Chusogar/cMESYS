#!/bin/sh
# Regenerates a game's generated sources (ROMs as C arrays, Musashi tables):
#   tools/regen.sh indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble | retofinv | robocop | polepos | polepos2 | ql
# ROMs: roms/indytemp or roms/roadrunn + roms/atarisy1, roms/snowbros, roms/trackfld, roms/pbobble, roms/gauntlet, roms/outrun, roms/retofinv, roms/robocop, roms/polepos, roms/polepos2 (unzipped,
# with the Namco 51xx-54xx MCU dumps),
# roms/ql (the QL set unzipped + the .mdv cartridge to build in).
set -e
G=$1
M=main
D=$M/drivers/arcades/$G
[ "$G" = ql ] && D=$M/drivers/computers/$G
T=${TMPDIR:-/tmp}/arcade_regen
mkdir -p $T
AS1=$M/drivers/arcades/atarisy1
CFLAGS="-O1 -w -I$D -I$AS1 -I$M/cpu -I$M/cpu/m68k -I$M/cpu/m6502 -I$M/cpu/mc6809 -I$M/sound -I$M/machine -I$M/video"
M68K="$M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c"
case $G in
indytemp|roadrunn)
    cc $CFLAGS -DAS1_HOOK_PC -o $T/gen_roms tools/gen_roms_atarisy1.c $AS1/atarisy1_roms.c $AS1/atarisy1_core.c \
       $M/sound/ym2151.c $M/sound/pokey.c $M/sound/tms5220.c $M/machine/via6522.c $M/machine/slapstic.c \
       $M/video/atari_mo.c $M68K -lm
    $T/gen_roms $G roms/$G roms/atarisy1 $D/rom_data.c
    CYC=1 ;;
snowbros)
    python3 tools/gen_roms_snowbros.py roms/snowbros $D/rom_data.c
    CYC=0 ;;
gauntlet)
    python3 tools/gen_roms_gauntlet.py roms/gauntlet $D/rom_data.c
    CYC=1 ;;
outrun)
    python3 tools/gen_roms_outrun.py roms/outrun $D/rom_data.c
    CYC=0 ;;
pbobble)
    python3 tools/gen_roms_pbobble.py roms/pbobble $D/rom_data.c
    CYC=0 ;;
retofinv)
    python3 tools/gen_roms_retofinv.py roms/retofinv $D/rom_data.c
    exit 0 ;;
polepos|polepos2)
    python3 tools/gen_roms_polepos.py $G roms/$G $D/rom_data.c
    exit 0 ;;
robocop)
    python3 tools/gen_roms_robocop.py roms/robocop $D/rom_data.c
    CYC=0 ;;
ql)
    python3 tools/gen_roms_ql.py roms/ql $D/rom_data.c
    CYC=0 ;;
trackfld)
    cc -O2 -o $T/gen_roms tools/gen_roms_trackfld.c
    $T/gen_roms roms/trackfld $D/rom_data.c
    exit 0 ;;
*)  echo "usage: $0 indytemp|roadrunn|gauntlet|outrun|snowbros|trackfld|pbobble|retofinv|robocop|polepos|polepos2|ql"; exit 1 ;;
esac
# Musashi opcode tables in flash, for this game's CPU type
cc -O1 -w -I$D -I$M/cpu/m68k -DCYC_CPU=$CYC -o $T/gen_m68k tools/gen_m68k_tables.c $M/cpu/m68k/m68kcpu.c \
   $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c tools/m68k_stub.c
$T/gen_m68k $M/cpu/m68k/m68kops.c $D/m68k_tables.c
