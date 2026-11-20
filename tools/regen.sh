#!/bin/sh
# Regenerates a game's generated sources (ROMs as C arrays, Musashi tables):
#   tools/regen.sh <machine>         the ROMs of roms/<set>/ built into its rom_data.c
#   tools/regen.sh --stub <machine>  its rom_data.c without ROMs (as in git): the
#                                    machine then reads them from storage at run time
#                                    (roms/<set>/, roms/<set>.zip, the microSD; main/rom/romfs.h)
# (A build can also leave the ROMs out without touching rom_data.c: ROMS=external.)
#   tools/regen.sh indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble | retofinv | robocop | polepos | polepos2 | ql
#                  | spectrum48 | spectrum128 | plus3 | pentagon | pentagon1024 | scorpion | cpc6128 | c64 | lynx | msx1 | msx2 | amiga
#                  | apple2 | apple2p | apple2e | apple2ee | apple2gs | macplus | macii | specnext | samcoupe | exl100 | exeltel
# ROMs: roms/indytemp or roms/roadrunn + roms/atarisy1, roms/snowbros, roms/trackfld, roms/pbobble, roms/gauntlet, roms/outrun, roms/retofinv, roms/robocop, roms/polepos, roms/polepos2 (unzipped; Pole Position
# also with the Namco 51xx-54xx MCU dumps),
# roms/ql (the QL set unzipped + the .mdv cartridge to build in),
# roms/<spectrum-model>/ exclusively (one directory and one rom_data_<model>.c per model),
# roms/cpc6128, roms/c64, roms/lynx, roms/amiga, roms/apple2* (one per model), roms/apple2gs, roms/macplus,
# roms/macii (unzipped MAME sets, their .zip, or the usual dumps).
set -e
STUB=
if [ "$1" = "--stub" ]; then STUB=1; shift; fi
G=$1
M=main
D=$M/drivers/arcades/$G
case $G in
ql) D=$M/drivers/computers/$G ;;
spectrum48|spectrum128|plus3|pentagon|pentagon1024|scorpion) D=$M/drivers/computers/zxspec ;;
cpc6128) D=$M/drivers/computers/cpc ;;
c64|amiga) D=$M/drivers/computers/$G ;;
lynx) D=$M/drivers/consoles/lynx ;;
msx1|msx2) D=$M/drivers/computers/msx ;;
apple2|apple2p|apple2e|apple2ee) D=$M/drivers/computers/apple2 ;;
exl100|exeltel) D=$M/drivers/computers/exelv ;;
apple2gs|macplus|macii|specnext|samcoupe) D=$M/drivers/computers/$G ;;
esac
if [ -n "$STUB" ]; then
    case $G in
    ql) python3 tools/gen_roms_ql.py --stub $D/rom_data.c ;;
    spectrum48|spectrum128|plus3|pentagon|pentagon1024|scorpion)
        python3 tools/gen_roms_zxspec.py --stub $D/rom_data_$G.c $G ;;
    msx1|msx2|apple2|apple2p|apple2e|apple2ee|exl100|exeltel) python3 tools/gen_roms_computers.py --stub $G $D/rom_data_$G.c ;;
    cpc6128|c64|lynx|amiga|apple2gs|macplus|macii|specnext|samcoupe) python3 tools/gen_roms_computers.py --stub $G $D/rom_data.c ;;
    coleco|pv2000|vectrex|scv|sega32x|gba|psx)
        python3 tools/gen_roms_computers.py --stub $G $M/drivers/consoles/$G/rom_data.c ;;
    *) python3 tools/gen_rom_stub.py $G $D/rom_data.c ;;
    esac
    exit 0
fi
T=${TMPDIR:-/tmp}/arcade_regen
mkdir -p $T
AS1=$M/drivers/arcades/atarisy1
CFLAGS="-O1 -w -I$D -I$AS1 -I$M/cpu -I$M/cpu/m68k -I$M/cpu/m6502 -I$M/cpu/mc6809 -I$M/sound -I$M/machine -I$M/video"
M68K="$M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c"
case $G in
indytemp|roadrunn)
    cc $CFLAGS -DAS1_HOOK_PC -o $T/gen_roms tools/gen_roms_atarisy1.c $AS1/atarisy1_roms.c $AS1/atarisy1_core.c \
       $M/sound/ym2151.c $M/sound/pokey.c $M/sound/tms5220.c $M/machine/via6522.c $M/machine/slapstic.c \
       $M/video/atari_mo.c $M/rom/romfs.c $M/machine/zx_inflate.c $M68K -lm
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
spectrum48|spectrum128|plus3|pentagon|pentagon1024|scorpion)
    ROMDIR=roms/$G
    if [ ! -d "$ROMDIR" ]; then
        echo "error: $ROMDIR/ missing — each Spectrum model reads only its own directory" >&2
        echo "  place that model's ROMs in $ROMDIR/ then re-run: $0 $G" >&2
        echo "  (empty stubs: python3 tools/gen_roms_zxspec.py --stub $D/rom_data_$G.c $G)" >&2
        exit 1
    fi
    python3 tools/gen_roms_zxspec.py "$ROMDIR" "$D/rom_data_$G.c" "$G"
    exit 0 ;;
cpc6128|c64|lynx)
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data.c
    exit 0 ;;
amiga)
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data.c
    CYC=0 ;;
msx1|msx2|apple2|apple2p|apple2e|apple2ee|exl100|exeltel)
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data_$G.c
    exit 0 ;;
apple2gs|macii|specnext|samcoupe)
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data.c
    exit 0 ;;
macplus)
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data.c
    CYC=0 ;;
coleco|pv2000|vectrex|scv|sega32x|gba|psx)
    D=$M/drivers/consoles/$G
    python3 tools/gen_roms_computers.py $G roms/$G $D/rom_data.c
    exit 0 ;;
trackfld)
    cc -O2 -o $T/gen_roms tools/gen_roms_trackfld.c
    $T/gen_roms roms/trackfld $D/rom_data.c
    exit 0 ;;
*)  echo "usage: $0 indytemp|roadrunn|gauntlet|outrun|snowbros|trackfld|pbobble|retofinv|robocop|polepos|polepos2|ql|spectrum48|spectrum128|plus3|pentagon|pentagon1024|scorpion|cpc6128|c64|lynx|msx1|msx2|amiga|apple2|apple2p|apple2e|apple2ee|apple2gs|macplus|macii|specnext|samcoupe|exl100|exeltel|coleco|pv2000|vectrex|scv|sega32x|gba|psx"; exit 1 ;;
esac
# Musashi opcode tables in flash, for this game's CPU type
cc -O1 -w -I$D -I$M/cpu/m68k -DCYC_CPU=$CYC -o $T/gen_m68k tools/gen_m68k_tables.c $M/cpu/m68k/m68kcpu.c \
   $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c tools/m68k_stub.c
$T/gen_m68k $M/cpu/m68k/m68kops.c $D/m68k_tables.c
