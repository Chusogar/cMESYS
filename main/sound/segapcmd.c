// segapcmd.c - see segapcmd.h.
#include <string.h>
#include "segapcmd.h"

void segapcmd_init(segapcmd *p, uint32_t clock, float amplitude, int variant)
{
    memset(p, 0, sizeof *p);
    p->clock = clock;
    p->amplitude = amplitude;
    p->variant = variant;
    segapcmd_set_bank(p, SEGAPCMD_BANK_512);
    segapcmd_reset(p);
}

void segapcmd_set_bank(segapcmd *p, uint32_t bank)
{
    p->bankshift = (uint8_t)(bank & 0xf);
    p->bankmask = (uint8_t)(0x70 | ((bank >> 16) & 0xfc));
}

void segapcmd_reset(segapcmd *p)
{
    memset(p->ram, 0xff, sizeof p->ram);
    memset(p->low, 0, sizeof p->low);
    p->out_left = p->out_right = 0;
}

void segapcmd_clock(segapcmd *p)
{
    const bool discrete = p->variant == SEGAPCMD_DISCRETE;
    const int count = segapcmd_voices(p);
    int32_t lsum = 0, rsum = 0;
    for (int voice = 0; voice < count; voice++) {
        const size_t regs = (size_t)((discrete ? 0x40 : 0x00) + voice * 8);   // volume, loop, end, delta
        const size_t state = (size_t)((discrete ? 0xc0 : 0x80) + voice * 8);  // current address, control
        uint8_t *ctrl = &p->ram[state + 6];
        if (*ctrl & 1) {
            p->low[voice] = 0;
            continue;
        }
        uint32_t addr = ((uint32_t)p->ram[state + 5] << 16) | ((uint32_t)p->ram[state + 4] << 8) | p->low[voice];
        const uint8_t end = (uint8_t)(p->ram[regs + 6] + 1);
        if ((uint8_t)(addr >> 16) == end) {
            if (*ctrl & 2) {
                *ctrl |= 1;
                continue;
            }
            addr = ((uint32_t)p->ram[regs + 5] << 16) | ((uint32_t)p->ram[regs + 4] << 8);
        }
        const uint32_t bank = discrete ? 0 : (uint32_t)(*ctrl & p->bankmask) << p->bankshift;
        const uint8_t sample = p->rom && p->rom_size ? p->rom[(bank + (addr >> 8)) % p->rom_size] : 0x80;
        const int v = (int)(int8_t)(uint8_t)(sample - 0x80);
        lsum += v * (int)(p->ram[regs + 2] & 0x7f);
        rsum += v * (int)(p->ram[regs + 3] & 0x7f);
        addr = (addr + p->ram[regs + 7]) & 0xffffffu;
        p->ram[state + 4] = (uint8_t)(addr >> 8);
        p->ram[state + 5] = (uint8_t)(addr >> 16);
        p->low[voice] = (uint8_t)addr;
    }
    p->out_left = lsum;
    p->out_right = rsum;
}
