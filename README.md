# ZX Spectrum 48K Emulator (C++)

A compact, modular ZX Spectrum 48K emulator in modern C++17, with SDL2 for video/audio/input.

Status: CPU core implements a large subset of Z80 instructions and a framework for full coverage. ULA video, keyboard matrix, and beeper audio are implemented. 48K `.SNA` snapshots supported for quick loading.

## Build

Dependencies:
- CMake >= 3.16
- C++17 compiler
- SDL2 development libraries

On Debian/Ubuntu:
```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake libsdl2-dev
```

Configure and build:
```bash
cmake -S . -B build
cmake --build build -j
```

Binary will be at `build/bin/zx48k`.

### Windows (MSYS2/MinGW) via Makefile

Optionally, you can build with the provided Makefile which is friendly to Windows environments:

1. Install MSYS2 and MinGW toolchain, then install SDL2:
```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 make
```
2. Open an MSYS2 MinGW64 shell and run:
```bash
mingw32-make
```

If `pkg-config` is not available, set `SDL2DIR` to your SDL2 install prefix:
```bash
mingw32-make SDL2DIR="C:/msys64/mingw64"
```

Artifacts will be in `build/bin/`:
- `zx48k` main emulator
- `minivadr` MiniVadr sample (requires `minivadr_main.cpp`)
- `scrview` simple `.scr` viewer

## Run

You need a 48K Spectrum ROM (16KB). Place it somewhere and pass the path. Optionally pass a 48K `.sna` snapshot to start directly into a program.

```bash
./build/bin/zx48k /path/to/48.rom [game.sna]
```

Controls:
- Letters/numbers map to Spectrum keys
- Shift = CAPS SHIFT, Ctrl = SYMBOL SHIFT
- Enter/Space as expected

## Notes
- Timing is not cycle-accurate yet; enough for many titles and BASIC.
- Tape loading via ROM is not implemented. Use `.sna` snapshots for now.
- CPU coverage is being expanded; please open issues for missing instructions.
