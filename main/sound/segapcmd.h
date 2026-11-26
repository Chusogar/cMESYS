// segapcmd.h - Sega PCM: a C port of dsp-cpp's sound/sega_pcm (after MAME's
// segapcm.cpp). Two variants: the discrete board of Hang-On / Space Harrier
// (8 voices at 0x40+8*v / 0xc0+8*v, no banking, a sample every 64 clocks)
// and the 315-5218 (16 voices at 8*v / 0x80+8*v, bank bits in the control
// register, a sample every 128 clocks). (sound/segapcm stays MAME's.)
#ifndef SEGAPCMD_H
#define SEGAPCMD_H
#include <stdbool.h>
#include <stdint.h>

enum { SEGAPCMD_DISCRETE, SEGAPCMD_315_5218 };
#define SEGAPCMD_BANK_256 11
#define SEGAPCMD_BANK_512 12
#define SEGAPCMD_BANK_12M 13

typedef struct {
    const uint8_t *rom;                         // read modulo its size (NULL: 0x80)
    uint32_t rom_size, clock;
    int variant;
    uint8_t ram[0x100], low[16], bankshift, bankmask;
    float amplitude;
    int32_t out_left, out_right;
} segapcmd;

void segapcmd_init(segapcmd *p, uint32_t clock, float amplitude, int variant);   // (bank 512)
void segapcmd_set_bank(segapcmd *p, uint32_t bank);
void segapcmd_reset(segapcmd *p);
static inline uint8_t segapcmd_read(const segapcmd *p, uint16_t a) { return p->ram[a & 0xff]; }
static inline void segapcmd_write(segapcmd *p, uint16_t a, uint8_t v) { p->ram[a & 0xff] = v; }
void segapcmd_clock(segapcmd *p);              // one output sample
static inline int segapcmd_voices(const segapcmd *p) { return p->variant == SEGAPCMD_DISCRETE ? 8 : 16; }
static inline uint32_t segapcmd_tick_rate(const segapcmd *p) { return p->clock / (uint32_t)(segapcmd_voices(p) * 8); }
static inline int32_t segapcmd_left(const segapcmd *p) { return (int32_t)((float)p->out_left * p->amplitude); }
static inline int32_t segapcmd_right(const segapcmd *p) { return (int32_t)((float)p->out_right * p->amplitude); }
static inline int32_t segapcmd_last_sample(const segapcmd *p) { return (segapcmd_left(p) + segapcmd_right(p)) / 2; }

#endif
