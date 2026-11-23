// k007232d.h - Konami's 007232 PCM chip as dsp-cpp's sound/k007232 (from
// k007232.pas): two channels of 7-bit samples, one step a call at the
// host's mix rate. A C port.
#ifndef K007232D_H
#define K007232D_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const uint8_t *rom;
    uint32_t rom_size;
    float amplify;
    bool stereo;
    void (*volume_cb)(void *user, uint8_t v);   // register 0x0c (optional)
    void *user;
    uint8_t wreg[0x10];
    uint32_t address[2], start[2], step[2], bank[2];
    bool play[2];
    uint8_t vol[2][2];
    uint32_t fncode[0x200];
} k007232d;

void k007232d_init(k007232d *k, const uint8_t *rom, uint32_t rom_size, float amplify, bool stereo);
void k007232d_reset(k007232d *k);
void k007232d_write(k007232d *k, uint8_t a, uint8_t v);
static inline void k007232d_set_volume(k007232d *k, int ch, uint8_t a, uint8_t b) { if (ch >= 0 && ch < 2) k->vol[ch][0] = a, k->vol[ch][1] = b; }
static inline void k007232d_set_bank(k007232d *k, int a, int b) { k->bank[0] = (uint32_t)a << 17; k->bank[1] = (uint32_t)b << 17; }
int32_t k007232d_update(k007232d *k);

#endif
