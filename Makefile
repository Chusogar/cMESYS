# Desktop (SDL2) builds of the arcade drivers, from the same sources as the
# ESP32 CYD firmware.
#
#   make GAME=indytemp        (indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble | retofinv | robocop | polepos | polepos2 | starwars | ql
#                             | spectrum48 | spectrum128 | plus3 | pentagon | pentagon1024 | scorpion
#                             | cpc6128 | c64 | lynx | msx1 | msx2 | amiga | atarist
#                             | apple2 | apple2p | apple2e | apple2ee | apple2gs | macplus | macii | specnext | samcoupe | exl100 | exeltel
#                             | a console of main/drivers/consoles/consoles.mk)  -> build/sdl/<game>
#                             (ql / spectrum*: computers in main/drivers/computers/)
#   make TARGET=windows ...   same, but MinGW -> build/win/<game>.exe  (see ./build_windows.sh)
#   make ROMS=external ...    no ROMs built in: all from storage (roms/<set>/, <set>.zip, --roms DIR)
#   make all                  all of them
#   make clean
#
# The firmware is built with ESP-IDF instead:  idf.py -DGAME=<game> build

GAME    ?= indytemp
TARGET  ?=
CC      ?= gcc
OUT     := build/sdl
EXE     :=
M       := main
ZX_GAMES := spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion
A2_GAMES := apple2 apple2p apple2e apple2ee
A2_MODEL_apple2 := 0
A2_MODEL_apple2p := 1
A2_MODEL_apple2e := 2
A2_MODEL_apple2ee := 3
EXELV_GAMES := exl100 exeltel
EXELV_MODEL_exl100 := 0
EXELV_MODEL_exeltel := 1
include $(M)/drivers/consoles/consoles.mk
ifeq ($(GAME),ql)
  D     := $(M)/drivers/computers/$(GAME)
else ifeq ($(GAME),cpc6128)
  D     := $(M)/drivers/computers/cpc
else ifeq ($(GAME),c64)
  D     := $(M)/drivers/computers/c64
else ifeq ($(GAME),amiga)
  D     := $(M)/drivers/computers/amiga
else ifeq ($(GAME),atarist)
  D     := $(M)/drivers/computers/atarist
else ifeq ($(GAME),lynx)
  D     := $(M)/drivers/consoles/lynx
else ifneq ($(filter $(GAME),msx1 msx2),)
  D     := $(M)/drivers/computers/msx
else ifneq ($(filter $(GAME),$(A2_GAMES)),)
  D     := $(M)/drivers/computers/apple2
else ifneq ($(filter $(GAME),$(EXELV_GAMES)),)
  D     := $(M)/drivers/computers/exelv
else ifneq ($(filter $(GAME),apple2gs macplus macii specnext samcoupe),)
  D     := $(M)/drivers/computers/$(GAME)
else ifneq ($(filter $(GAME),$(ZX_GAMES)),)
  D     := $(M)/drivers/computers/zxspec
else
  D     := $(M)/drivers/arcades/$(GAME)
endif
AS1     := $(M)/drivers/arcades/atarisy1
ZX_SRC  := $(D)/zxspec_sdl.c $(D)/zxspec_core.c $(D)/zx48.c $(D)/zx128.c $(D)/zxclone.c $(D)/rom_data_$(GAME).c \
           $(M)/cpu/z80dsp/z80dsp.c $(M)/machine/zx_tape.c $(M)/machine/zx_snap.c $(M)/machine/zx_rzx.c \
           $(M)/machine/zx_inflate.c $(M)/machine/zx_pbuf.c $(M)/machine/zx_beta.c $(M)/machine/zx_nec765.c $(M)/sound/ay8910.c

PP      := $(M)/drivers/arcades/polepos

CFLAGS  ?= -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-maybe-uninitialized
CFLAGS  += -std=gnu11 -I$(D) -I$(AS1) -I$(PP) -I$(M)/ports/sdl2 -I$(M)/cpu -I$(M)/cpu/m68k -I$(M)/cpu/m6502 \
           -I$(M)/cpu/mc6809 -I$(M)/cpu/mcs48 -I$(M)/cpu/z80 -I$(M)/sound -I$(M)/machine -I$(M)/video

# SDL2: sdl2-config by default; override with SDL_CFLAGS / SDL_LIBS or SDL2DIR.
SDL2_CONFIG ?= sdl2-config

ifeq ($(TARGET),windows)
  OUT := build/win
  EXE := .exe
  # Prefer an explicit MinGW SDL2 prefix (needed when cross-compiling from Linux).
  ifneq ($(strip $(SDL2DIR)),)
    SDL_CFLAGS := -I"$(SDL2DIR)/include" -I"$(SDL2DIR)/include/SDL2"
    SDL_LIBS   := -L"$(SDL2DIR)/lib" -lmingw32 -lSDL2main -lSDL2
  endif
endif

SDL_CFLAGS  ?= $(shell $(SDL2_CONFIG) --cflags 2>/dev/null)
SDL_LIBS    ?= $(shell $(SDL2_CONFIG) --libs 2>/dev/null)

ifeq ($(TARGET),windows)
  # Fallback when there is no matching sdl2-config (SDL.org zip / MSYS2 paths).
  ifeq ($(strip $(SDL_LIBS)),)
    ifneq (,$(wildcard /mingw64/include/SDL2/SDL.h))
      SDL_CFLAGS := -I/mingw64/include -I/mingw64/include/SDL2
      SDL_LIBS   := -L/mingw64/lib -lmingw32 -lSDL2main -lSDL2
    else ifneq (,$(wildcard /mingw32/include/SDL2/SDL.h))
      SDL_CFLAGS := -I/mingw32/include -I/mingw32/include/SDL2
      SDL_LIBS   := -L/mingw32/lib -lmingw32 -lSDL2main -lSDL2
    else
      SDL_LIBS := -lmingw32 -lSDL2main -lSDL2
    endif
  endif
endif

ifeq ($(strip $(SDL_LIBS)),)
  SDL_LIBS := -lSDL2
endif
ifeq ($(TARGET),windows)
  SDL_LIBS += -lcomdlg32          # (the computers' file dialog: common/comp_sdl.h)
endif

MUSASHI4 := $(M)/cpu/musashi/m68kcpu.c $(M)/cpu/musashi/m68kops.c $(M)/cpu/musashi/m68kdasm.c \
            $(M)/cpu/musashi/softfloat/softfloat.c
M68K := $(M)/cpu/m68k/m68kcpu.c $(M)/cpu/m68k/m68kops.c $(M)/cpu/m68k/m68kopac.c \
        $(M)/cpu/m68k/m68kopdm.c $(M)/cpu/m68k/m68kopnz.c

ifeq ($(GAME),indytemp)
  # Atari System 1 core shared with roadrunn (cpu/m6502/dsp6502.c is compiled inside it)
  SRC  := $(D)/indytemp_sdl.c $(AS1)/atarisy1_core.c $(AS1)/atarisy1_roms.c $(D)/rom_data.c $(M68K) \
          $(M)/sound/ym2151.c $(M)/sound/pokey.c $(M)/sound/tms5220.c \
          $(M)/machine/via6522.c $(M)/machine/slapstic.c $(M)/video/atari_mo.c
  DEFS := -DAS1_HOOK_PC
else ifeq ($(GAME),roadrunn)
  # Atari System 1 core shared with indytemp (cpu/m6502/dsp6502.c is compiled inside it)
  SRC  := $(D)/roadrunn_sdl.c $(AS1)/atarisy1_core.c $(AS1)/atarisy1_roms.c $(D)/rom_data.c $(M68K) \
          $(M)/sound/ym2151.c $(M)/sound/pokey.c $(M)/sound/tms5220.c \
          $(M)/machine/via6522.c $(M)/machine/slapstic.c $(M)/video/atari_mo.c
  DEFS := -DAS1_HOOK_PC
else ifeq ($(GAME),gauntlet)
  # (cpu/m6502/dsp6502.c is compiled inside gauntlet_core.c)
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
else ifeq ($(GAME),starwars)
  # Star Wars and The Empire Strikes Back (starwars esb / F4)
  SRC  := $(D)/starwars_sdl.c $(D)/sw_core.c $(D)/rom_data.c $(M)/cpu/m6809dsp/m6809dsp.c $(M)/machine/mos6532.c \
          $(M)/machine/starwars_math.c $(M)/video/avg_starwars.c $(M)/sound/pokey.c $(M)/sound/tms5220.c
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
else ifeq ($(GAME),cpc6128)
  SRC  := $(D)/cpc_sdl.c $(D)/cpc_core.c $(D)/rom_data.c $(M)/cpu/z80dsp/z80dsp.c $(M)/sound/ay8910.c \
          $(M)/machine/zx_nec765.c $(M)/machine/zx_tape.c $(M)/machine/zx_pbuf.c $(M)/machine/zx_inflate.c
  DEFS :=
else ifeq ($(GAME),c64)
  SRC  := $(D)/c64_sdl.c $(D)/c64_core.c $(D)/rom_data.c $(M)/cpu/m6502/m65xx.c $(M)/video/mos6569.c \
          $(M)/sound/sid6581.c $(M)/machine/mos6526.c $(M)/machine/c1541.c $(M)/machine/via6522.c \
          $(M)/machine/zx_pbuf.c
  DEFS :=
else ifeq ($(GAME),amiga)
  SRC  := $(D)/amiga_sdl.c $(D)/amiga_core.c $(D)/amiga_chipset.c $(D)/rom_data.c $(M68K)
  DEFS :=
else ifeq ($(GAME),atarist)
  SRC  := $(D)/st_sdl.c $(D)/st_core.c $(D)/st_mfp.c $(D)/st_floppy.c $(D)/rom_data.c $(M)/sound/ay8910.c $(M68K)
  DEFS :=
else ifeq ($(GAME),lynx)
  SRC  := $(D)/lynx_sdl.c $(D)/lynx_core.c $(D)/rom_data.c $(M)/machine/zx_pbuf.c
  DEFS :=
else ifneq ($(filter $(GAME),msx1 msx2),)
  SRC  := $(D)/msx_sdl.c $(D)/msx_core.c $(D)/rom_data_$(GAME).c $(M)/cpu/z80dsp/z80dsp.c $(M)/video/tms9918.c \
          $(M)/video/v9938.c $(M)/sound/ay8910.c $(M)/machine/msx_cart.c $(M)/machine/msx_disk.c $(M)/machine/zx_pbuf.c
  DEFS := -DMSX_MODEL=$(subst msx,,$(GAME))
else ifneq ($(filter $(GAME),$(A2_GAMES)),)
  SRC  := $(D)/apple2_sdl.c $(D)/apple2_core.c $(D)/rom_data_$(GAME).c $(M)/cpu/m6502/m65xx.c $(M)/machine/diskii.c \
          $(M)/machine/zx_pbuf.c
  DEFS := -DA2_MODEL=$(A2_MODEL_$(GAME))
else ifeq ($(GAME),apple2gs)
  SRC  := $(D)/apple2gs_sdl.c $(D)/apple2gs_core.c $(D)/rom_data.c $(M)/cpu/w65c816/w65c816.c $(M)/sound/es5503.c \
          $(M)/machine/blkfile.c
  DEFS := -I$(M)/cpu/w65c816
else ifeq ($(GAME),macplus)
  SRC  := $(D)/macplus_sdl.c $(D)/macplus_core.c $(D)/rom_data.c $(M68K) $(M)/machine/via6522.c $(M)/machine/iwm.c \
          $(M)/machine/mac_dsk.c $(M)/machine/mac_rtc.c $(M)/machine/ncr5380.c $(M)/machine/mac_dcmp.c \
          $(M)/machine/blkfile.c
  DEFS :=
else ifeq ($(GAME),macii)
  # Musashi 4 (68020) instead of cpu/m68k's: its sources found by relative path
  SRC  := $(D)/macii_sdl.c $(D)/macii_core.c $(D)/rom_data.c $(MUSASHI4) $(M)/machine/via6522.c $(M)/machine/iwm.c \
          $(M)/machine/mac_dsk.c $(M)/machine/ncr5380.c $(M)/sound/asc.c $(M)/machine/blkfile.c
  DEFS :=
else ifeq ($(GAME),specnext)
  SRC  := $(D)/specnext_sdl.c $(D)/specnext_core.c $(D)/rom_data.c $(M)/cpu/z80dsp/z80dsp.c \
          $(M)/sound/ay8910.c $(M)/machine/sdcard_spi.c $(M)/machine/zxn_dma.c $(M)/machine/blkfile.c
  DEFS :=
else ifneq ($(filter $(GAME),$(EXELV_GAMES)),)
  SRC  := $(D)/exelv_sdl.c $(D)/exelv_core.c $(D)/exelv_tape.c $(D)/rom_data_$(GAME).c $(M)/cpu/tms7000/tms7000.c \
          $(M)/video/tms3556.c $(M)/sound/tms5220.c $(M)/drivers/consoles/common/cons_util.c
  DEFS := -DEXELV_MODEL=$(EXELV_MODEL_$(GAME))
else ifeq ($(GAME),samcoupe)
  SRC  := $(D)/samcoupe_sdl.c $(D)/samcoupe_core.c $(D)/rom_data.c $(M)/cpu/z80dsp/z80dsp.c $(M)/sound/saa1099.c \
          $(M)/machine/wd1772.c $(M)/machine/sam_disk.c $(M)/machine/sam_tape.c
  DEFS :=
else ifeq ($(GAME),spectrum48)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_SPEC48
else ifeq ($(GAME),spectrum128)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_SPEC128
else ifeq ($(GAME),plus3)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_PLUS3
else ifeq ($(GAME),pentagon)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_PENTAGON
else ifeq ($(GAME),pentagon1024)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_PENTAGON1024
else ifeq ($(GAME),scorpion)
  SRC  := $(ZX_SRC)
  DEFS := -DZX_MODEL=ZX_SCORPION
else ifneq ($(filter $(GAME),$(CONSOLES)),)
  D    := $(M)/drivers/consoles/$(GAME)
  SRC  := $(M)/drivers/consoles/common/cons_sdl.c $(addprefix $(M)/,$(CONS_COMMON) $(CONS_SRC_$(GAME)))
  DEFS := $(addprefix -D,$(CONS_DEFS_$(GAME)))
else
  $(error GAME must be indytemp, roadrunn, gauntlet, outrun, snowbros, trackfld, pbobble, retofinv, robocop, polepos, polepos2, starwars, ql, spectrum48, spectrum128, plus3, pentagon, pentagon1024, scorpion, cpc6128, c64, lynx, msx1, msx2, amiga, atarist or a console: $(CONSOLES))
endif

# The ROMs that rom_data.c does not have come from storage (main/rom/romfs.c
# and the machine's rom_load.c): --roms DIR, $CMESYS_ROMS, ./roms, the
# program's folder/roms; <set>/<file>, <set>.zip (MAME's) or <set>/*.zip.
ROM_SRC := $(M)/rom/romfs.c $(M)/machine/zx_inflate.c $(wildcard $(D)/rom_load.c)
ifneq ($(filter indytemp roadrunn,$(GAME)),)
  ROM_SRC += $(AS1)/atarisy1_roms.c
endif
SRC := $(SRC) $(filter-out $(SRC),$(ROM_SRC))

# PORT=picohost: the game's RP2040-PiZero frontend (<game>_pico.c) on the
# desktop without a window (tools/pico_host.c), to test it without the board.
# PORT=web: Emscripten build -> build/web/<game>.html (needs emcc; see
# main/ports/web/Makefile and ./build_web.sh).
ifeq ($(PORT),picohost)
  OUT   := build/picohost
  SRC   := $(patsubst %_sdl.c,%_pico.c,$(SRC))
  PORTSRC := tools/pico_host.c
  CFLAGS += -I$(M)/ports/rp2040-pizero -I$(M)/drivers/computers/ql -DPICO_BAND=4
  # microSD is board-only; stub the API so ql / zxspec *_pico.c link on the host.
  ifneq ($(filter $(GAME),ql $(ZX_GAMES) cpc6128 c64 lynx msx1 msx2 amiga atarist $(A2_GAMES) apple2gs macplus macii specnext samcoupe $(EXELV_GAMES) $(CONSOLES)),)
    PORTSRC += tools/ql_sd_host_stub.c
  endif
  SDL_CFLAGS :=
  SDL_LIBS :=
else ifeq ($(PORT),web)
  $(info Use: make -C main/ports/web GAME=$(GAME)   or ./build_web.sh)
  $(error PORT=web is built by main/ports/web/Makefile)
else
  PORTSRC := $(M)/ports/sdl2/sdl_port.c
endif

# ROMS=external: no ROMs built in at all (rom_data.c compiled without its
# arrays), only those from storage; a build that can be given away.
#   make GAME=robocop ROMS=external   -> build/sdl-noroms/robocop
ifeq ($(ROMS),external)
  OUT  := $(OUT)-noroms
  DEFS += -DCMESYS_ROMS_EXTERNAL
endif

$(OUT)/$(GAME)$(EXE): $(SRC) $(PORTSRC) $(wildcard $(D)/*.h) $(wildcard $(AS1)/*.h) $(wildcard $(PP)/*.h) $(wildcard $(PP)/*.inc) Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(DEFS) $(SDL_CFLAGS) -o $@ $(SRC) $(PORTSRC) $(SDL_LIBS) -lm

all:
	$(MAKE) GAME=indytemp TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=roadrunn TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=gauntlet TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=outrun TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=snowbros TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=trackfld TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=pbobble TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=retofinv TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=robocop TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=starwars TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=polepos TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=polepos2 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=ql TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=spectrum48 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=spectrum128 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=plus3 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=pentagon TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=pentagon1024 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=scorpion TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=cpc6128 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=c64 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=lynx TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=msx1 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=msx2 TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=amiga TARGET=$(TARGET) ROMS=$(ROMS)
	$(MAKE) GAME=atarist TARGET=$(TARGET) ROMS=$(ROMS)
	for c in $(CONSOLES); do $(MAKE) GAME=$$c TARGET=$(TARGET) ROMS=$(ROMS) || exit 1; done

clean:
	rm -rf $(OUT)

.PHONY: all clean
