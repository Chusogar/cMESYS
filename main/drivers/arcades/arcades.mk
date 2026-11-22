# arcades.mk - the arcade games on the common frontends (common/arc.h) and
# their sources (relative to main/), one ARC_SRC_<id> line each: read by the
# Makefile, main/ports/web/Makefile, the CYD's and the RP2040 / RP2350's
# CMakeLists.txt and main/ports/android/build_arcade.sh. Every game also gets
# ARC_COMMON and its frontend (common/arc_sdl.c, arc_pico.c, arc_cyd.c).
# ARC_DEFS_<id>: its defines (without -D), when it has any (ARC_VARIANT: the
# game of a core with several). ARC_RAM_<id>: the objects the RP2040 / RP2350
# run from RAM (each line written out: CMake reads them as they are). Their ROMs come from storage (romfs: roms/<set>/ or MAME's
# <set>.zip), not built in.
ARCADES := bombjack calorie mrdo centiped milliped arkanoid minivadr
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
ARC_RAM_bombjack := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_calorie := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_mrdo := z80dsp.c.o mrdo.c.o sn76496.c.o
ARC_RAM_centiped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_milliped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_arkanoid := z80dsp.c.o arkanoid.c.o ay8910.c.o m6805dsp.c.o taito68705dsp.c.o
ARC_RAM_minivadr := z80dsp.c.o minivadr.c.o
