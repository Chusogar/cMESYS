# consoles.mk - the consoles of main/drivers/consoles and their sources
# (relative to main/), one CONS_SRC_<id> line each: read by the Makefile,
# main/ports/web/Makefile, the CYD's and the RP2040 / RP2350's CMakeLists.txt
# and main/ports/android/build_apk.sh. Every console also gets CONS_COMMON
# and its frontend (common/cons_sdl.c, cons_pico.c, cons_cyd.c, or the
# Android port's m_cons.c).
CONSOLES := sg1000
CONS_COMMON := drivers/consoles/common/cons_util.c machine/zx_inflate.c
CONS_SRC_sg1000 := drivers/consoles/sg1000/sg1000_core.c cpu/z80dsp/z80dsp.c video/tms9918.c sound/sn76496.c
