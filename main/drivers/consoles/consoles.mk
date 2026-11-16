# consoles.mk - the consoles of main/drivers/consoles and their sources
# (relative to main/), one CONS_SRC_<id> line each: read by the Makefile,
# main/ports/web/Makefile, the CYD's and the RP2040 / RP2350's CMakeLists.txt
# and main/ports/android/build_apk.sh. Every console also gets CONS_COMMON
# and its frontend (common/cons_sdl.c, cons_pico.c, cons_cyd.c, or the
# Android port's m_cons.c).
CONSOLES := sg1000 coleco pv2000 pv1000 sms gamegear nes a2600 a7800 vectrex
CONS_COMMON := drivers/consoles/common/cons_util.c machine/zx_inflate.c
CONS_SRC_sg1000 := drivers/consoles/sg1000/sg1000_core.c cpu/z80dsp/z80dsp.c video/tms9918.c sound/sn76496.c
CONS_SRC_coleco := drivers/consoles/coleco/coleco_core.c drivers/consoles/coleco/rom_data.c cpu/z80dsp/z80dsp.c video/tms9918.c sound/sn76496.c sound/ay8910.c machine/i2cmem.c
CONS_SRC_pv2000 := drivers/consoles/pv2000/pv2000_core.c drivers/consoles/pv2000/rom_data.c cpu/z80dsp/z80dsp.c video/tms9918.c sound/sn76496.c
CONS_SRC_pv1000 := drivers/consoles/pv1000/pv1000_core.c cpu/z80dsp/z80dsp.c
CONS_SRC_sms := drivers/consoles/sms/sms_core.c cpu/z80dsp/z80dsp.c video/sega_vdp.c sound/sn76496.c sound/ym2413.c
CONS_SRC_gamegear := drivers/consoles/gamegear/gg_core.c cpu/z80dsp/z80dsp.c video/sega_vdp.c sound/sn76496.c
CONS_SRC_nes := drivers/consoles/nes/nes_core.c cpu/m6502/m65xx.c video/nes_ppu.c sound/nes_apu.c
CONS_SRC_a2600 := drivers/consoles/a2600/a2600_core.c cpu/m6502/m65xx.c video/tia.c machine/mos6532.c
CONS_SRC_a7800 := drivers/consoles/a7800/a7800_core.c cpu/m6502/m65xx.c video/maria.c video/tia.c
CONS_SRC_vectrex := drivers/consoles/vectrex/vectrex_core.c drivers/consoles/vectrex/rom_data.c drivers/consoles/vectrex/vx_cpu.c machine/via6522.c sound/ay8910.c
