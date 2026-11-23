// k007232d.c - see k007232d.h
#include <math.h>
#include <string.h>
#include "k007232d.h"

void k007232d_init(k007232d *k, const uint8_t *rom, uint32_t rom_size, float amplify, bool stereo)
{
    k->rom = rom;
    k->rom_size = rom_size;
    k->amplify = amplify;
    k->stereo = stereo;
    for (int i = 0; i < 0x200; i++) k->fncode[i] = (uint32_t)lround((32 << 12) / (double)(0x200 - i));
    k007232d_reset(k);
}

void k007232d_reset(k007232d *k)
{
    memset(k->wreg, 0, sizeof k->wreg);
    memset(k->address, 0, sizeof k->address);
    memset(k->start, 0, sizeof k->start);
    memset(k->step, 0, sizeof k->step);
    memset(k->bank, 0, sizeof k->bank);
    k->play[0] = k->play[1] = false;
    k->vol[0][0] = 255, k->vol[0][1] = 0, k->vol[1][0] = 0, k->vol[1][1] = 255;
}

static uint32_t start_of(const k007232d *k, int p)
{
    return ((uint32_t)(k->wreg[p * 6 + 4] & 1) << 16) | ((uint32_t)k->wreg[p * 6 + 3] << 8) | k->wreg[p * 6 + 2] | k->bank[p];
}

void k007232d_write(k007232d *k, uint8_t a, uint8_t v)
{
    uint8_t r = a & 0x0f;
    k->wreg[r] = v;
    if (r == 0x0c) {
        if (k->volume_cb) k->volume_cb(k->user, v);
        return;
    }
    if (r == 0x0d) return;
    int p = 0;
    if (r >= 6) {
        p = 1;
        r -= 6;
    }
    if (r <= 1) k->step[p] = k->fncode[(((k->wreg[p * 6 + 1] << 8) & 0x100) | k->wreg[p * 6]) & 0x1ff];
    else if (r == 5) {
        k->start[p] = start_of(k, p);
        if (k->start[p] < k->rom_size) {
            k->play[p] = true;
            k->address[p] = 0;
        }
    }
}

int32_t k007232d_update(k007232d *k)
{
    int32_t o1 = 0, o2 = 0;
    for (int f = 0; f < 2; f++) {
        if (!k->play[f]) continue;
        uint32_t addr = k->start[f] + ((k->address[f] >> 12) & 0xfffff);
        const uint16_t va = (uint16_t)(k->vol[f][0] * 2), vb = (uint16_t)(k->vol[f][1] * 2);
        uint32_t old = addr;
        addr = k->start[f] + ((k->address[f] >> 12) & 0xfffff);
        while (old <= addr) {
            if (old >= k->rom_size || (k->rom[old] & 0x80)) {
                if (k->wreg[0x0d] & (1 << f)) {
                    k->start[f] = start_of(k, f);
                    addr = k->start[f];
                    k->address[f] = 0;
                } else k->play[f] = false;
                break;
            }
            old++;
        }
        if (!k->play[f]) continue;
        k->address[f] += k->step[f];
        if (addr < k->rom_size) {
            const int32_t pcm = (int32_t)(k->rom[addr] & 0x7f) - 0x40;
            o1 += pcm * va;
            o2 += pcm * vb;
        }
    }
    return (int32_t)((float)(k->stereo ? (o1 + o2) / 2 : o1) * k->amplify);
}
