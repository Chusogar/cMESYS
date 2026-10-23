// okim6295.h - OKI MSM6295 4-voice ADPCM player. C port of MAME's
// sound/okim6295 + okiadpcm (command decoding, start/stop table, volume
// table, ADPCM step tables, playing-status reads). One sample every
// 132 (pin 7 high) or 165 (pin 7 low) clocks; the caller ticks it.
#ifndef OKIM6295_H
#define OKIM6295_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool playing;
    uint32_t base, sample, count;
    int32_t signal, step;
    int32_t volume;                 // 0..32
} okim6295_voice;

typedef struct {
    const uint8_t *rom;             // sample ROM (18-bit address space)
    uint32_t rom_size;              // bytes present (reads beyond: 0)
    okim6295_voice voice[4];
    int command;                    // -1: none pending
    int16_t out;                    // last sample (sum of the voices, clamped)
} okim6295;

void okim6295_init(okim6295 *o, const uint8_t *rom, uint32_t rom_size);
void okim6295_reset(okim6295 *o);
uint8_t okim6295_read(const okim6295 *o);
void okim6295_write(okim6295 *o, uint8_t command);
// Computes the next sample (o->out, also returned) in MAME's int16 scale.
int16_t okim6295_tick(okim6295 *o);

#endif
