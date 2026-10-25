// segapcm.h - Sega 315-5218 PCM (16 voices of 8-bit samples), as MAME's
// sound/segapcm.cpp: one output sample per voice pass (clock / 128).
#ifndef SEGAPCM_H
#define SEGAPCM_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t addr;          // 16.8 fixed point
    uint16_t loop;
    uint8_t end, freq, lvol, rvol, ctrl;
} segapcm_voice;

typedef struct segapcm {
    segapcm_voice v[16];
    uint8_t ram[256];       // the register file (scratch locations included)
    // sample ROM: byte at (bank + offset), bank = (ctrl & 0x70) << 12
    uint8_t (*read_rom)(void *user, uint32_t addr);
    void *user;
    int32_t out_l, out_r;   // last sample (sum of the voices, as MAME before scaling)
} segapcm;

void segapcm_init(segapcm *p);
uint8_t segapcm_read(segapcm *p, uint8_t offset);
void segapcm_write(segapcm *p, uint8_t offset, uint8_t data);
// One output sample: every voice advances once.
void segapcm_tick(segapcm *p);
// true while some voice plays (a tick of a silent chip only does bookkeeping)
bool segapcm_active(const segapcm *p);

#endif
