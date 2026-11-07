# HDMI with sound for the RP2350-PiZero

`dvi/` and `util/` are Shuichi Takano's `pico_lib` (MIT, see `LICENSE`) as
kept up to date by Frank Hoedemakers for his RP2350 emulators
(https://github.com/fhoedemakers/pico_lib, commit 0fe7a6749d9b), which runs on
the Waveshare RP2350-PiZero (TMDS on GPIO32-39 through a PIO with GPIO base
16) and sends the sound in HDMI data islands.

Changes for cMESYS (in `dvi/dvi.h`, `dvi/dvi.cpp`, marked `cMESYS`):

- `setLineCallback()`: a function the DMA interrupt calls after every line;
  `pico_port.c` queues the next rows of its frame buffer from there, so a
  game drawing slowly on core 1 never makes a line late.
- `tryConvertRow565()`: an RGB565 row (all 16 bits, not RGB444), pixel
  doubled, queued if a TMDS buffer is free.
- A queued buffer whose lines have already gone by is dropped instead of
  blocking the queue (and the picture) for the rest of the frame.
- `getActiveLine()`: the output line being set up (-1 in the blanking).

`hdmi_glue.cpp` is the C interface `pico_port.c` uses.
