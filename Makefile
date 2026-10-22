# Desktop (SDL2) builds of the arcade drivers, from the same sources as the
# ESP32 CYD firmware.
#
#   make GAME=indytemp        (indytemp | roadrunn | snowbros | trackfld | pbobble)  -> build/sdl/<game>
#   make all                  all of them
#   make clean
#
# The firmware is built with ESP-IDF instead:  idf.py -DGAME=<game> build

GAME    ?= indytemp
CC      ?= gcc
OUT     := build/sdl
M       := main
D       := $(M)/drivers/arcades/$(GAME)
AS1     := $(M)/drivers/arcades/atarisy1

CFLAGS  ?= -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-maybe-uninitialized
CFLAGS  += -std=gnu11 -I$(D) -I$(AS1) -I$(M)/ports/sdl2 -I$(M)/cpu -I$(M)/cpu/m68k -I$(M)/cpu/m6502 \
           -I$(M)/cpu/mc6809 -I$(M)/sound -I$(M)/machine -I$(M)/video
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || echo -lSDL2)

M68K := $(M)/cpu/m68k/m68kcpu.c $(M)/cpu/m68k/m68kops.c $(M)/cpu/m68k/m68kopac.c \
        $(M)/cpu/m68k/m68kopdm.c $(M)/cpu/m68k/m68kopnz.c
PORT := $(M)/ports/sdl2/sdl_port.c

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
else
  $(error GAME must be indytemp, roadrunn, snowbros, trackfld or pbobble)
endif

$(OUT)/$(GAME): $(SRC) $(PORT) $(wildcard $(D)/*.h) $(wildcard $(AS1)/*.h) Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(DEFS) $(SDL_CFLAGS) -o $@ $(SRC) $(PORT) $(SDL_LIBS) -lm

all:
	$(MAKE) GAME=indytemp
	$(MAKE) GAME=roadrunn
	$(MAKE) GAME=snowbros
	$(MAKE) GAME=trackfld
	$(MAKE) GAME=pbobble

clean:
	rm -rf $(OUT)

.PHONY: all clean
