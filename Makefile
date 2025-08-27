# Simple Makefile for building on Windows (MinGW/MSYS2) with SDL2
# Usage examples:
#   make                   # builds all targets (zx48k and scr_viewer)
#   make zx48k             # builds only emulator
#   make clean             # cleans outputs
#
# Customize SDL2_DIR to point to your SDL2 install (unzipped development package)
# - For MSYS2 (64-bit): SDL2 is usually installed under /mingw64
#     SDL2_DIR=/mingw64
# - For MSYS2 (32-bit): SDL2 is usually under /mingw32
#     SDL2_DIR=/mingw32
# - For SDL.org development package on Windows (x64):
#     SDL2_DIR=C:/SDL2-2.30.8/x86_64-w64-mingw32

# Toolchain
CC := gcc
CXX := g++
AR := ar

# Directories
SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := build

# Allow override from environment or command line
SDL2_DIR ?=

# Includes and libs
ifeq ($(OS),Windows_NT)
  # Windows-specific defaults; can be overridden
  ifneq ($(SDL2_DIR),)
    SDL2_INCLUDE := -I"$(SDL2_DIR)/include" -I"$(SDL2_DIR)/include/SDL2"
    SDL2_LIBDIR  := -L"$(SDL2_DIR)/lib" -L"$(SDL2_DIR)/lib64"
  else
    # MSYS2 typical locations (autodetect path when building inside MSYS2)
    ifneq (,$(wildcard /mingw64/include/SDL2/SDL.h))
      SDL2_INCLUDE := -I/mingw64/include -I/mingw64/include/SDL2
      SDL2_LIBDIR  := -L/mingw64/lib
    else ifneq (,$(wildcard /mingw32/include/SDL2/SDL.h))
      SDL2_INCLUDE := -I/mingw32/include -I/mingw32/include/SDL2
      SDL2_LIBDIR  := -L/mingw32/lib
    endif
  endif
else
  # Non-Windows (fallback) — useful if building on Linux
  SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
  SDL2_LDFLAGS := $(shell pkg-config --libs sdl2 2>/dev/null)
  SDL2_INCLUDE := $(SDL2_CFLAGS)
  SDL2_LIBDIR :=
endif

# Link libraries for SDL2 on MinGW/MSYS2
# Order matters on Windows/MinGW
SDL2_LIBS_WIN := -lmingw32 -lSDL2main -lSDL2 -mwindows -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lversion

# If pkg-config delivered flags, prefer them; otherwise use Windows libs
ifeq ($(SDL2_LDFLAGS),)
  SDL2_LIBS := $(SDL2_LIBDIR) $(SDL2_LIBS_WIN)
else
  SDL2_LIBS := $(SDL2_LDFLAGS)
endif

# Common flags
CXXFLAGS := -std=gnu++17 -Wall -Wextra -Wpedantic -O2
CFLAGS   := -Wall -Wextra -O2

# Define to enable SDL-specific code paths in C++ frontend
CXXFLAGS += -DZX_WITH_SDL=1

# Include paths
CXXFLAGS += -I$(SRC_DIR) $(SDL2_INCLUDE)
CFLAGS   += -I$(SRC_DIR) $(SDL2_INCLUDE)

# Source files
CPP_SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
# Exclude minivadr_main.cpp from the zx48k target
CPP_SOURCES_NO_MINIVADR := $(filter-out $(SRC_DIR)/minivadr_main.cpp,$(CPP_SOURCES))

C_SOURCES := $(SRC_DIR)/main.c

# Object files
ZX_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES_NO_MINIVADR))
SCR_VIEWER_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))

# Binaries
ZX_BIN := $(BIN_DIR)/zx48k.exe
SCR_VIEWER_BIN := $(BIN_DIR)/scr_viewer.exe

.PHONY: all zx48k scr_viewer clean dirs

all: dirs $(ZX_BIN) $(SCR_VIEWER_BIN)

dirs:
	@mkdir -p "$(BIN_DIR)" "$(OBJ_DIR)"

# Build emulator (C++)
zx48k: dirs $(ZX_BIN)

$(ZX_BIN): $(ZX_OBJS)
	$(CXX) -o $@ $^ $(SDL2_LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build simple SCR viewer (C + SDL2)
scr_viewer: dirs $(SCR_VIEWER_BIN)

$(SCR_VIEWER_BIN): $(SCR_VIEWER_OBJS)
	$(CC) -o $@ $^ $(SDL2_LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)"

