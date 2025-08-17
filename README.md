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
