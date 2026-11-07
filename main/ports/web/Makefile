# Emscripten builds of every cMESYS driver (same *_sdl.c frontends + cores).
#
#   make -C main/ports/web GAME=robocop     -> ../../../build/web/robocop.html
#   make -C main/ports/web all
#   make -C main/ports/web serve            -> python http.server on build/web
#
# Needs emcc on PATH (source emsdk_env.sh). ROMs must already be in each
# driver's rom_data.c (tools/regen.sh <game>).
ROOT    := $(abspath ../../..)
M       := $(ROOT)/main
OUT     := $(ROOT)/build/web
GAME    ?= indytemp
EMCC    ?= emcc

ZX_GAMES := spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion
ifeq ($(GAME),ql)
  D     := $(M)/drivers/computers/$(GAME)
else ifneq ($(filter $(GAME),$(ZX_GAMES)),)
  D     := $(M)/drivers/computers/zxspec
else ifeq ($(GAME),cpc6128)
  D     := $(M)/drivers/computers/cpc
else ifneq ($(filter $(GAME),c64 lynx),)
  D     := $(M)/drivers/computers/$(GAME)
else ifneq ($(filter $(GAME),msx1 msx2),)
  D     := $(M)/drivers/computers/msx
else
  D     := $(M)/drivers/arcades/$(GAME)
endif
AS1     := $(M)/drivers/arcades/atarisy1
PP      := $(M)/drivers/arcades/polepos
ZX_SRC  := $(D)/zxspec_sdl.c $(D)/zxspec_core.c $(D)/zx48.c $(D)/zx128.c $(D)/zxclone.c $(D)/rom_data_$(GAME).c \
           $(M)/cpu/z80dsp/z80dsp.c $(M)/machine/zx_tape.c $(M)/machine/zx_snap.c $(M)/machine/zx_rzx.c \
           $(M)/machine/zx_inflate.c $(M)/machine/zx_pbuf.c $(M)/machine/zx_beta.c $(M)/machine/zx_nec765.c $(M)/sound/ay8910.c

CFLAGS  := -O2 -std=gnu11 -Wall -Wno-unused-function -Wno-unused-variable \
           -Wno-sign-compare -Wno-uninitialized \
           -I$(D) -I$(AS1) -I$(PP) -I$(M)/ports/web -I$(M)/cpu -I$(M)/cpu/m68k \
           -I$(M)/cpu/m6502 -I$(M)/cpu/mc6809 -I$(M)/cpu/mcs48 -I$(M)/cpu/m6805 \
           -I$(M)/cpu/z80 -I$(M)/sound -I$(M)/sound/emu8950 -I$(M)/machine -I$(M)/video
# SDL2 from Emscripten; ASYNCIFY so emscripten_sleep in sdl_port_present works
# with the existing while (sdl_port_poll()) loops in *_sdl.c.
LDFLAGS := -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 \
           -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576 \
           -sENVIRONMENT=web \
           --shell-file $(M)/ports/web/shell.html \
           -sEXIT_RUNTIME=1

M68K := $(M)/cpu/m68k/m68kcpu.c $(M)/cpu/m68k/m68kops.c $(M)/cpu/m68k/m68kopac.c \
        $(M)/cpu/m68k/m68kopdm.c $(M)/cpu/m68k/m68kopnz.c

ifeq ($(GAME),indytemp)
  SRC  := $(D)/indytemp_sdl.c $(AS1)/atarisy1_core.c $(AS1)/atarisy1_roms.c $(D)/rom_data.c $(M68K) \
          $(M)/sound/ym2151.c $(M)/sound/pokey.c $(M)/sound/tms5220.c \
          $(M)/machine/via6522.c $(M)/machine/slapstic.c $(M)/video/atari_mo.c
  DEFS := -DAS1_HOOK_PC
else ifeq ($(GAME),roadrunn)
  SRC  := $(D)/roadrunn_sdl.c $(AS1)/atarisy1_core.c $(AS1)/atarisy1_roms.c $(D)/rom_data.c $(M68K) \
          $(M)/sound/ym2151.c $(M)/sound/pokey.c $(M)/sound/tms5220.c \
          $(M)/machine/via6522.c $(M)/machine/slapstic.c $(M)/video/atari_mo.c
  DEFS := -DAS1_HOOK_PC
else ifeq ($(GAME),gauntlet)
  SRC  := $(D)/gauntlet_sdl.c $(D)/gauntlet_core.c $(D)/rom_data.c $(M68K) \
          $(M)/sound/ym2151.c $(M)/sound/pokey.c $(M)/sound/tms5220.c \
          $(M)/machine/slapstic.c $(M)/video/atari_mo.c
  DEFS :=
else ifeq ($(GAME),outrun)
  SRC  := $(D)/outrun_sdl.c $(D)/or_core.c $(D)/or_video.c $(D)/or_sound.c $(D)/rom_data.c $(M68K) $(M)/cpu/z80/z80.c \
          $(M)/sound/ym2151.c $(M)/sound/segapcm.c $(M)/machine/sega_315_5195.c
  DEFS :=
else ifeq ($(GAME),snowbros)
  SRC  := $(D)/snowbros_sdl.c $(D)/sb_core.c $(D)/rom_data.c $(M68K) $(M)/cpu/z80/z80.c \
          $(M)/sound/emu8950/emu8950.c $(M)/sound/emu8950/emuadpcm.c
  DEFS :=
else ifeq ($(GAME),trackfld)
  SRC  := $(D)/trackfld_sdl.c $(D)/tf_core.c $(D)/rom_data.c $(M)/cpu/mc6809/mc6809.c $(M)/cpu/z80/z80.c \
          $(M)/sound/sn76489.c $(M)/sound/vlm5030.c
  DEFS := -DMC6809_NO_SETJMP
else ifeq ($(GAME),pbobble)
  SRC  := $(D)/pbobble_sdl.c $(D)/pb_core.c $(D)/rom_data.c $(M68K) $(M)/cpu/z80/z80.c \
          $(M)/sound/ym2610.c $(M)/machine/tc0140syt.c $(M)/machine/eeprom93c46.c $(M)/video/tc0180vcu.c
  DEFS :=
else ifeq ($(GAME),retofinv)
  SRC  := $(D)/retofinv_sdl.c $(D)/rv_core.c $(D)/rv_video.c $(D)/rom_data.c $(M)/cpu/z80/z80.c \
          $(M)/cpu/m6805/m6805.c $(M)/machine/taito68705.c $(M)/sound/sn76489.c
  DEFS :=
else ifeq ($(GAME),robocop)
  SRC  := $(D)/robocop_sdl.c $(D)/rc_core.c $(D)/rc_video.c $(D)/rc_sound.c $(D)/rom_data.c $(M68K) \
          $(M)/cpu/h6280/h6280.c $(M)/sound/ym2610.c $(M)/sound/okim6295.c \
          $(M)/sound/emu8950/emu8950.c $(M)/sound/emu8950/emuadpcm.c
  DEFS :=
else ifeq ($(GAME),$(filter $(GAME),polepos polepos2))
  # core in polepos/, shared by polepos2
  SRC  := $(D)/$(GAME)_sdl.c $(PP)/pp_core.c $(PP)/pp_video.c $(D)/rom_data.c $(M)/cpu/z80/z80.c \
          $(M)/cpu/z8002/z8002.c $(M)/cpu/z8002/z8002_tab.c $(M)/cpu/mb88xx/mb88xx.c $(M)/machine/namco5x.c \
          $(M)/sound/polepos_snd.c
  DEFS :=
else ifeq ($(GAME),ql)
  SRC  := $(D)/ql_sdl.c $(D)/ql_core.c $(D)/ql_view.c $(D)/ql_cart_host.c $(D)/rom_data.c $(M68K) $(M)/cpu/mcs48/mcs48.c \
          $(M)/machine/zx8302.c $(M)/machine/ql_mdv.c $(M)/video/zx8301.c
  DEFS :=
  # MEMFS upload + exported notify for the cartridge menu / drag-drop shell.
  LDFLAGS += -sFORCE_FILESYSTEM=1 \
             "-sEXPORTED_FUNCTIONS=['_main','_ql_web_notify_upload']" \
             "-sEXPORTED_RUNTIME_METHODS=['FS']"
else ifneq ($(filter $(GAME),$(ZX_GAMES)),)
  SRC  := $(ZX_SRC)
  ifeq ($(GAME),spectrum48)
    DEFS := -DZX_MODEL=ZX_SPEC48
  else ifeq ($(GAME),spectrum128)
    DEFS := -DZX_MODEL=ZX_SPEC128
  else ifeq ($(GAME),plus3)
    DEFS := -DZX_MODEL=ZX_PLUS3
  else ifeq ($(GAME),pentagon)
    DEFS := -DZX_MODEL=ZX_PENTAGON
  else ifeq ($(GAME),pentagon1024)
    DEFS := -DZX_MODEL=ZX_PENTAGON1024
  else
    DEFS := -DZX_MODEL=ZX_SCORPION
  endif
  LDFLAGS += -sFORCE_FILESYSTEM=1 \
             "-sEXPORTED_FUNCTIONS=['_main','_zx_web_notify_upload','_zx_web_set_upload_path']" \
             "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','UTF8ToString','stringToUTF8']"
else ifneq ($(filter $(GAME),cpc6128 c64 lynx msx1 msx2),)
  ifneq ($(filter $(GAME),msx1 msx2),)
    SRC := $(D)/msx_sdl.c $(D)/msx_core.c $(D)/rom_data_$(GAME).c $(M)/cpu/z80dsp/z80dsp.c $(M)/video/tms9918.c \
           $(M)/video/v9938.c $(M)/sound/ay8910.c $(M)/machine/msx_cart.c $(M)/machine/msx_disk.c $(M)/machine/zx_pbuf.c
  else ifeq ($(GAME),cpc6128)
    SRC := $(D)/cpc_sdl.c $(D)/cpc_core.c $(D)/rom_data.c $(M)/cpu/z80dsp/z80dsp.c $(M)/sound/ay8910.c \
           $(M)/machine/zx_nec765.c $(M)/machine/zx_tape.c $(M)/machine/zx_pbuf.c $(M)/machine/zx_inflate.c
  else ifeq ($(GAME),c64)
    SRC := $(D)/c64_sdl.c $(D)/c64_core.c $(D)/rom_data.c $(M)/cpu/m6502/m65xx.c $(M)/video/mos6569.c \
           $(M)/sound/sid6581.c $(M)/machine/mos6526.c $(M)/machine/c1541.c $(M)/machine/via6522.c $(M)/machine/zx_pbuf.c
  else
    SRC := $(D)/lynx_sdl.c $(D)/lynx_core.c $(D)/rom_data.c $(M)/cpu/m6502/m65xx.c $(M)/machine/zx_pbuf.c
  endif
  DEFS := $(if $(filter $(GAME),msx1 msx2),-DMSX_MODEL=$(subst msx,,$(GAME)))
  # the upload bar / drag-and-drop (common/comp_sdl.h)
  LDFLAGS += -sFORCE_FILESYSTEM=1 \
             "-sEXPORTED_FUNCTIONS=['_main','_zx_web_notify_upload','_zx_web_set_upload_path']" \
             "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','UTF8ToString','stringToUTF8']"
else
  $(error GAME must be indytemp, roadrunn, gauntlet, outrun, snowbros, trackfld, pbobble, retofinv, robocop, polepos, polepos2, ql, spectrum48, spectrum128, plus3, pentagon, pentagon1024, scorpion, cpc6128, c64, lynx, msx1 or msx2)
endif

PORTSRC := $(M)/ports/web/sdl_port.c
GAMES   := indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 ql \
           spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion cpc6128 c64 lynx msx1 msx2

$(OUT)/$(GAME).html: $(SRC) $(PORTSRC) $(wildcard $(PP)/*.inc) $(M)/ports/web/shell.html $(M)/ports/web/Makefile
	@mkdir -p $(OUT)
	$(EMCC) $(CFLAGS) $(DEFS) -o $@ $(SRC) $(PORTSRC) $(LDFLAGS) -lm
	@cp -f $(M)/ports/web/index.html $(OUT)/index.html

all:
	@for g in $(GAMES); do $(MAKE) -f $(M)/ports/web/Makefile GAME=$$g || exit 1; done
	@cp -f $(M)/ports/web/index.html $(OUT)/index.html
	@echo "web builds in $(OUT)/"

serve: all
	python3 -m http.server --directory $(OUT) 8080

clean:
	rm -rf $(OUT)

.PHONY: all clean serve
