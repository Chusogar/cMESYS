# arcades.mk - the arcade games on the common frontends (common/arc.h) and
# their sources (relative to main/), one ARC_SRC_<id> line each: read by the
# Makefile, main/ports/web/Makefile, the CYD's and the RP2040 / RP2350's
# CMakeLists.txt and main/ports/android/build_arcade.sh. Every game also gets
# ARC_COMMON and its frontend (common/arc_sdl.c, arc_pico.c, arc_cyd.c).
# ARC_DEFS_<id>: its defines (without -D), when it has any (ARC_VARIANT: the
# game of a core with several). ARC_RAM_<id>: the objects the RP2040 / RP2350
# run from RAM (each line written out: CMake reads them as they are). Their ROMs come from storage (romfs: roms/<set>/ or MAME's
# <set>.zip), not built in.
# ARC_BIG: those that need more than the 4-8 MB of the VGA32 / RP2350 (desktop,
# web and Android only)
ARC_BIG := pirates
ARCADES := bombjack calorie mrdo centiped milliped arkanoid minivadr commando gng blktiger bionicc atetris bagman ambush blueprnt btime shaolins mikie galaxian galaga pirates blockout bublbobl renegade bankp citycon brkthru appoooh tehkanwc tigerh wboy
ARC_COMMON := drivers/arcades/common/arc_util.c rom/romfs.c machine/zx_inflate.c
ARC_SRC_bombjack := drivers/arcades/bombjack/bombjack.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/sega_decrypt.c
ARC_SRC_calorie := drivers/arcades/bombjack/bombjack.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/sega_decrypt.c
ARC_DEFS_calorie := ARC_VARIANT=1
ARC_SRC_mrdo := drivers/arcades/mrdo/mrdo.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_centiped := drivers/arcades/centipede/centipede.c cpu/m6502/m65xx.c sound/pokey.c
ARC_SRC_milliped := drivers/arcades/centipede/centipede.c cpu/m6502/m65xx.c sound/pokey.c
ARC_DEFS_milliped := ARC_VARIANT=1
ARC_SRC_arkanoid := drivers/arcades/arkanoid/arkanoid.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/taito68705dsp.c cpu/m6805dsp/m6805dsp.c
ARC_SRC_minivadr := drivers/arcades/minivadr/minivadr.c cpu/z80dsp/z80dsp.c
ARC_SRC_commando := drivers/arcades/commando/commando.c cpu/z80dsp/z80dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_gng := drivers/arcades/gng/gng.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_blktiger := drivers/arcades/blktiger/blktiger.c cpu/z80dsp/z80dsp.c cpu/mcs51dsp/mcs51dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_atetris := drivers/arcades/atetris/atetris.c cpu/m6502/m65xx.c machine/slapstic.c sound/pokey.c
ARC_SRC_bagman := drivers/arcades/bagman/bagman.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_ambush := drivers/arcades/ambush/ambush.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_blueprnt := drivers/arcades/blueprnt/blueprnt.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_btime := drivers/arcades/btime/btime.c cpu/m6502/m65xx.c sound/ay8910.c
ARC_SRC_shaolins := drivers/arcades/shaolins/shaolins.c cpu/m6809dsp/m6809dsp.c sound/sn76496.c
ARC_SRC_mikie := drivers/arcades/mikie/mikie.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_galaxian := drivers/arcades/galaxian/galaxian.c cpu/z80dsp/z80dsp.c
ARC_SRC_galaga := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c
ARC_SRC_pirates := drivers/arcades/pirates/pirates.c drivers/arcades/common/m68k_tables.c cpu/m68k/m68kcpu.c cpu/m68k/m68kops.c cpu/m68k/m68kopac.c cpu/m68k/m68kopdm.c cpu/m68k/m68kopnz.c machine/eeprom93c46.c sound/okim6295dsp.c
ARC_DEFS_pirates := M68K_FLASH_TABLES
ARC_SRC_blockout := drivers/arcades/blockout/blockout.c drivers/arcades/common/m68k_tables.c cpu/m68k/m68kcpu.c cpu/m68k/m68kops.c cpu/m68k/m68kopac.c cpu/m68k/m68kopdm.c cpu/m68k/m68kopnz.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/okim6295dsp.c
ARC_DEFS_blockout := M68K_FLASH_TABLES
ARC_SRC_bublbobl := drivers/arcades/bublbobl/bublbobl.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/ym3812dsp.c
ARC_SRC_renegade := drivers/arcades/renegade/renegade.c cpu/m6502/m65xx.c cpu/m6809dsp/m6809dsp.c cpu/m6805dsp/m6805dsp.c machine/taito68705dsp.c sound/ym3812dsp.c
ARC_SRC_bankp := drivers/arcades/bankp/bankp.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_citycon := drivers/arcades/citycon/citycon.c cpu/m6809dsp/m6809dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_brkthru := drivers/arcades/brkthru/brkthru.c cpu/m6809dsp/m6809dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/ym3812dsp.c
ARC_SRC_appoooh := drivers/arcades/appoooh/appoooh.c cpu/z80dsp/z80dsp.c sound/sn76496.c sound/msm5205dsp.c
ARC_SRC_tehkanwc := drivers/arcades/tehkanwc/tehkanwc.c cpu/z80dsp/z80dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_SRC_tigerh := drivers/arcades/tigerh/tigerh.c cpu/z80dsp/z80dsp.c cpu/m6805dsp/m6805dsp.c machine/taito68705dsp.c sound/ay8910.c
ARC_SRC_wboy := drivers/arcades/wboy/wboy.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c sound/sn76496.c
ARC_SRC_bionicc := drivers/arcades/bionicc/bionicc.c drivers/arcades/common/m68k_tables.c cpu/m68k/m68kcpu.c cpu/m68k/m68kops.c cpu/m68k/m68kopac.c cpu/m68k/m68kopdm.c cpu/m68k/m68kopnz.c cpu/z80dsp/z80dsp.c cpu/mcs51dsp/mcs51dsp.c sound/ym2151.c
ARC_DEFS_bionicc := M68K_FLASH_TABLES
ARC_RAM_bombjack := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_calorie := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_mrdo := z80dsp.c.o mrdo.c.o sn76496.c.o
ARC_RAM_centiped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_milliped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_arkanoid := z80dsp.c.o arkanoid.c.o ay8910.c.o m6805dsp.c.o taito68705dsp.c.o
ARC_RAM_minivadr := z80dsp.c.o minivadr.c.o
ARC_RAM_commando := z80dsp.c.o commando.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_gng := m6809dsp.c.o z80dsp.c.o gng.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_blktiger := z80dsp.c.o mcs51dsp.c.o blktiger.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_bionicc := m68kcpu.c.o z80dsp.c.o mcs51dsp.c.o bionicc.c.o ym2151.c.o
ARC_RAM_atetris := m65xx.c.o atetris.c.o pokey.c.o slapstic.c.o
ARC_RAM_bagman := z80dsp.c.o bagman.c.o ay8910.c.o
ARC_RAM_ambush := z80dsp.c.o ambush.c.o ay8910.c.o
ARC_RAM_blueprnt := z80dsp.c.o blueprnt.c.o ay8910.c.o
ARC_RAM_btime := m65xx.c.o btime.c.o ay8910.c.o
ARC_RAM_shaolins := m6809dsp.c.o shaolins.c.o sn76496.c.o
ARC_RAM_mikie := m6809dsp.c.o z80dsp.c.o mikie.c.o sn76496.c.o
ARC_RAM_galaxian := z80dsp.c.o galaxian.c.o
ARC_RAM_galaga := z80dsp.c.o galaga.c.o
ARC_RAM_pirates := m68kcpu.c.o pirates.c.o okim6295dsp.c.o
ARC_RAM_blockout := m68kcpu.c.o z80dsp.c.o blockout.c.o ym2151.c.o okim6295dsp.c.o
ARC_RAM_bublbobl := z80dsp.c.o hd63701dsp.c.o bublbobl.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o ym3812dsp.c.o
ARC_RAM_renegade := m65xx.c.o m6809dsp.c.o m6805dsp.c.o taito68705dsp.c.o renegade.c.o ym3812dsp.c.o
ARC_RAM_bankp := z80dsp.c.o bankp.c.o sn76496.c.o
ARC_RAM_citycon := m6809dsp.c.o citycon.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_brkthru := m6809dsp.c.o brkthru.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o ym3812dsp.c.o
ARC_RAM_appoooh := z80dsp.c.o appoooh.c.o sn76496.c.o msm5205dsp.c.o
ARC_RAM_tehkanwc := z80dsp.c.o tehkanwc.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_tigerh := z80dsp.c.o m6805dsp.c.o taito68705dsp.c.o tigerh.c.o ay8910.c.o
ARC_RAM_wboy := z80dsp.c.o wboy.c.o z80piodsp.c.o sn76496.c.o
