# dspc.mk - the computers on the dspc interface (drivers/computers/common/
# dspc.h): migrated from dsp-cpp with its Machine API, one core each and the
# common frontends (dspc_sdl.c, dspc_pico.c, Android m_dspc.c). Included by
# the Makefiles, build_windows.sh, build_pico2.sh and the Android build.
#   DSPC_SRC_<game>: the core and its parts (relative to main/)
#   DSPC_DEFS_<game>: its -D (DSPC_MODEL)
#   DSPC_APP_<game>: the Android app it is in (one per core)
DSPC_COMMON :=
DSPC_GAMES := zx81 zx80
DSPC_SRC_zx81 := drivers/computers/zx81/zx81_core.c cpu/z80dsp/z80dsp.c
DSPC_DEFS_zx81 := DSPC_MODEL=0
DSPC_APP_zx81 := zx81
DSPC_SRC_zx80 := drivers/computers/zx81/zx81_core.c cpu/z80dsp/z80dsp.c
DSPC_DEFS_zx80 := DSPC_MODEL=1
DSPC_APP_zx80 := zx81
DSPC_GAMES += vic20 vic20n
DSPC_SRC_vic20 := drivers/computers/vic20/vic20_core.c cpu/m6502/m65xx.c machine/via6522.c sound/mos6560d.c
DSPC_DEFS_vic20 := DSPC_MODEL=0
DSPC_APP_vic20 := vic20
DSPC_SRC_vic20n := $(DSPC_SRC_vic20)
DSPC_DEFS_vic20n := DSPC_MODEL=1
DSPC_APP_vic20n := vic20
DSPC_GAMES += plus4 c16
DSPC_SRC_plus4 := drivers/computers/plus4/plus4_core.c cpu/m6502/m65xx.c sound/mos7360d.c
DSPC_DEFS_plus4 := DSPC_MODEL=0
DSPC_APP_plus4 := plus4
DSPC_SRC_c16 := $(DSPC_SRC_plus4)
DSPC_DEFS_c16 := DSPC_MODEL=1
DSPC_APP_c16 := plus4
DSPC_GAMES += c128
DSPC_SRC_c128 := drivers/computers/c128/c128_core.c cpu/m6502/m65xx.c video/mos6569.c machine/mos6526.c sound/sid6581.c
DSPC_DEFS_c128 :=
DSPC_APP_c128 := c128
DSPC_GAMES += a800 a800xl a800xe
DSPC_ATARI8 := drivers/computers/atari8/atari8_core.c cpu/m6502/m65xx.c video/antic_gtia.c sound/pokey.c
DSPC_SRC_a800 := $(DSPC_ATARI8)
DSPC_DEFS_a800 := DSPC_MODEL=0
DSPC_APP_a800 := a800
DSPC_SRC_a800xl := $(DSPC_ATARI8)
DSPC_DEFS_a800xl := DSPC_MODEL=1
DSPC_APP_a800xl := a800xl
DSPC_SRC_a800xe := $(DSPC_ATARI8)
DSPC_DEFS_a800xe := DSPC_MODEL=2
DSPC_APP_a800xe := a800xe
DSPC_GAMES += pcw8256 pcw8512
DSPC_PCW := drivers/computers/pcw/pcw_core.c cpu/z80dsp/z80dsp.c machine/zx_nec765.c machine/zx_pbuf.c sound/ay8910.c
DSPC_SRC_pcw8256 := $(DSPC_PCW)
DSPC_DEFS_pcw8256 := DSPC_MODEL=0
DSPC_APP_pcw8256 := pcw8256
DSPC_SRC_pcw8512 := $(DSPC_PCW)
DSPC_DEFS_pcw8512 := DSPC_MODEL=1
DSPC_APP_pcw8512 := pcw8512
