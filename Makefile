# Simple cross-platform Makefile for darcnesevolved
# Supports Linux/macOS and Windows (MSYS2/MinGW, mingw32-make)

# Tools
CC ?= gcc
CXX ?= g++
AR ?= ar
RM ?= rm -f
MKDIR ?= mkdir -p

# Build dirs
BUILDDIR := build
OBJDIR := $(BUILDDIR)/obj
BINDIR := $(BUILDDIR)/bin

# Source discovery
CPP_SOURCES := $(wildcard src/*.cpp)
C_SOURCES := $(wildcard src/*.c)

# Targets
ZX_MAIN := zx48k
MINIVADR := minivadr
SCRVIEW := scrview

# Filter sources per target
ZX_SOURCES := $(filter-out src/minivadr_main.cpp,$(CPP_SOURCES))
MINIVADR_SOURCES := src/minivadr_main.cpp
SCRVIEW_SOURCES := src/main.c

# Objects
ZX_OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(ZX_SOURCES))
MINIVADR_OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(MINIVADR_SOURCES))
SCRVIEW_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SCRVIEW_SOURCES))

# Detect platform for SDL2 linking quirks
OS_NAME := $(shell uname -s 2>/dev/null || echo Windows)
IS_WINDOWS := 0
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
endif
ifneq (,$(findstring MINGW,$(OS_NAME)))
  IS_WINDOWS := 1
endif

# Compiler flags
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
CFLAGS ?= -Wall -Wextra -O2
CPPFLAGS ?=
LDFLAGS ?=

# SDL2 flags: try pkg-config first, else SDL2DIR
PKG_CONFIG := $(shell command -v pkg-config 2>/dev/null)
ifdef PKG_CONFIG
  SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
  SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

ifeq ($(strip $(SDL2_CFLAGS)),)
  ifneq ($(strip $(SDL2DIR)),)
    SDL2_CFLAGS := -I"$(SDL2DIR)/include"
    ifeq ($(IS_WINDOWS),1)
      # Prefer MinGW import libs in SDL2DIR
      SDL2_LIBS := -L"$(SDL2DIR)/lib" -lSDL2
    else
      SDL2_LIBS := -L"$(SDL2DIR)/lib" -lSDL2
    endif
  endif
endif

# On Windows with MinGW, linking SDL2main can be helpful; keep optional
ifeq ($(IS_WINDOWS),1)
  SDL2_LIBS += -lSDL2main
  # Some MinGW setups need -lmingw32 first
  SDL2_LIBS := -lmingw32 $(SDL2_LIBS)
endif

# Defines: enable SDL code paths
CPPFLAGS += -DZX_WITH_SDL=1

.PHONY: all clean dirs

all: dirs $(BINDIR)/$(ZX_MAIN) $(BINDIR)/$(MINIVADR) $(BINDIR)/$(SCRVIEW)

dirs:
	$(MKDIR) $(OBJDIR) $(BINDIR)

# Link rules
$(BINDIR)/$(ZX_MAIN): $(ZX_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL2_LIBS)

$(BINDIR)/$(MINIVADR): $(MINIVADR_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL2_LIBS)

$(BINDIR)/$(SCRVIEW): $(SCRVIEW_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL2_LIBS)

# Compile rules
$(OBJDIR)/%.o: src/%.cpp | dirs
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SDL2_CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: src/%.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $< -o $@

clean:
	$(RM) -r $(BUILDDIR)

# Convenience run target (Linux/macOS): make run ROM=48.rom SNAP=game.sna
.PHONY: run
run: $(BINDIR)/$(ZX_MAIN)
	$(BINDIR)/$(ZX_MAIN) $(ROM) $(SNAP)

