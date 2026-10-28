# Desktop (SDL2) builds of the arcade drivers, from the same sources as the
# ESP32 CYD firmware.
#
#   make GAME=indytemp        (indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble | retofinv | robocop | ql)  -> build/sdl/<game>
#                             (ql: the Sinclair QL computer, in main/drivers/computers/ql)
#   make TARGET=windows ...   same, but MinGW -> build/win/<game>.exe  (see ./build_windows.sh)
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
ifeq ($(GAME),ql)
  D     := $(M)/drivers/computers/$(GAME)
else
  D     := $(M)/drivers/arcades/$(GAME)
endif
AS1     := $(M)/drivers/arcades/atarisy1

CFLAGS  ?= -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-maybe-uninitialized
CFLAGS  += -std=gnu11 -I$(D) -I$(AS1) -I$(M)/ports/sdl2 -I$(M)/cpu -I$(M)/cpu/m68k -I$(M)/cpu/m6502 \
           -I$(M)/cpu/mc6809 -I$(M)/cpu/mcs48 -I$(M)/sound -I$(M)/machine -I$(M)/video

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
else ifeq ($(GAME),robocop)
  SRC  := $(D)/robocop_sdl.c $(D)/rc_core.c $(D)/rc_video.c $(D)/rc_sound.c $(D)/rom_data.c $(M68K) \
          $(M)/cpu/h6280/h6280.c $(M)/sound/ym2610.c $(M)/sound/okim6295.c \
          $(M)/sound/emu8950/emu8950.c $(M)/sound/emu8950/emuadpcm.c
  DEFS :=
else ifeq ($(GAME),ql)
  SRC  := $(D)/ql_sdl.c $(D)/ql_core.c $(D)/ql_view.c $(D)/ql_cart_host.c $(D)/rom_data.c $(M68K) $(M)/cpu/mcs48/mcs48.c \
          $(M)/machine/zx8302.c $(M)/machine/ql_mdv.c $(M)/video/zx8301.c
  DEFS :=
else
  $(error GAME must be indytemp, roadrunn, gauntlet, outrun, snowbros, trackfld, pbobble, retofinv, robocop or ql)
endif

# PORT=picohost: the game's RP2040-PiZero frontend (<game>_pico.c) on the
# desktop without a window (tools/pico_host.c), to test it without the board.
# PORT=web: Emscripten build -> build/web/<game>.html (needs emcc; see
# main/ports/web/Makefile and ./build_web.sh).
ifeq ($(PORT),picohost)
  OUT   := build/picohost
  SRC   := $(patsubst %_sdl.c,%_pico.c,$(SRC))
  PORTSRC := tools/pico_host.c
  CFLAGS += -I$(M)/ports/rp2040-pizero
  SDL_CFLAGS :=
  SDL_LIBS :=
else ifeq ($(PORT),web)
  $(info Use: make -C main/ports/web GAME=$(GAME)   or ./build_web.sh)
  $(error PORT=web is built by main/ports/web/Makefile)
else
  PORTSRC := $(M)/ports/sdl2/sdl_port.c
endif

$(OUT)/$(GAME)$(EXE): $(SRC) $(PORTSRC) $(wildcard $(D)/*.h) $(wildcard $(AS1)/*.h) Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(DEFS) $(SDL_CFLAGS) -o $@ $(SRC) $(PORTSRC) $(SDL_LIBS) -lm

all:
	$(MAKE) GAME=indytemp TARGET=$(TARGET)
	$(MAKE) GAME=roadrunn TARGET=$(TARGET)
	$(MAKE) GAME=gauntlet TARGET=$(TARGET)
	$(MAKE) GAME=outrun TARGET=$(TARGET)
	$(MAKE) GAME=snowbros TARGET=$(TARGET)
	$(MAKE) GAME=trackfld TARGET=$(TARGET)
	$(MAKE) GAME=pbobble TARGET=$(TARGET)
	$(MAKE) GAME=retofinv TARGET=$(TARGET)
	$(MAKE) GAME=robocop TARGET=$(TARGET)
	$(MAKE) GAME=ql TARGET=$(TARGET)

clean:
	rm -rf $(OUT)

.PHONY: all clean
