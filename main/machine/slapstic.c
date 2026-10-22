// slapstic.c - Atari SLAPSTIC. C port of dsp-cpp's machine/slapstic.cpp.
#include "slapstic.h"

#include <stddef.h>

#define UNK 0xffff

enum {
    S_DISABLED = 0, S_ENABLED, S_ALT1, S_ALT2, S_ALT3,
    S_BIT1, S_BIT2, S_BIT3, S_ADD1, S_ADD2, S_ADD3
};

#define NOADD {UNK, UNK}, {UNK, UNK}, {UNK, UNK}, {UNK, UNK}, {UNK, UNK}

static const slapstic_config cfg101 = {3, {0x0080, 0x0090, 0x00a0, 0x00b0},
    {0x007f, UNK}, {0x1fff, 0x1dff}, {0x1ffc, 0x1b5c}, {0x1fcf, 0x0080}, 0,
    {0x1ff0, 0x1540}, {0x1ff3, 0x1540}, {0x1ff3, 0x1541}, {0x1ff3, 0x1542}, {0x1ff3, 0x1543}, {0x1ff8, 0x1550},
    NOADD};
static const slapstic_config cfg103 = {3, {0x0040, 0x0050, 0x0060, 0x0070},
    {0x007f, 0x002d}, {0x3fff, 0x3d14}, {0x3ffc, 0x3d24}, {0x3fcf, 0x0040}, 0,
    {0x3ff0, 0x34c0}, {0x3ff3, 0x34c0}, {0x3ff3, 0x34c1}, {0x3ff3, 0x34c2}, {0x3ff3, 0x34c3}, {0x3ff8, 0x34d0},
    NOADD};
static const slapstic_config cfg104 = {3, {0x0020, 0x0028, 0x0030, 0x0038},
    {0x007f, 0x0069}, {0x3fff, 0x3735}, {0x3ffc, 0x3764}, {0x3fe7, 0x0020}, 0,
    {0x3ff0, 0x3d90}, {0x3ff3, 0x3d90}, {0x3ff3, 0x3d91}, {0x3ff3, 0x3d92}, {0x3ff3, 0x3d93}, {0x3ff8, 0x3da0},
    NOADD};
static const slapstic_config cfg105 = {3, {0x0010, 0x0014, 0x0018, 0x001c},
    {0x007f, 0x003d}, {0x3fff, 0x0092}, {0x3ffc, 0x00a4}, {0x3ff3, 0x0010}, 0,
    {0x3ff0, 0x35b0}, {0x3ff3, 0x35b0}, {0x3ff3, 0x35b1}, {0x3ff3, 0x35b2}, {0x3ff3, 0x35b3}, {0x3ff8, 0x35c0},
    NOADD};
static const slapstic_config cfg106 = {3, {0x0008, 0x000a, 0x000c, 0x000e},
    {0x007f, 0x002b}, {0x3fff, 0x0052}, {0x3ffc, 0x0064}, {0x3ff9, 0x0008}, 0,
    {0x3ff0, 0x3da0}, {0x3ff3, 0x3da0}, {0x3ff3, 0x3da1}, {0x3ff3, 0x3da2}, {0x3ff3, 0x3da3}, {0x3ff8, 0x3db0},
    NOADD};
static const slapstic_config cfg107 = {3, {0x0018, 0x001a, 0x001c, 0x001e},
    {0x007f, 0x006b}, {0x3fff, 0x3d52}, {0x3ffc, 0x3d64}, {0x3ff9, 0x0018}, 0,
    {0x3ff0, 0x00a0}, {0x3ff3, 0x00a0}, {0x3ff3, 0x00a1}, {0x3ff3, 0x00a2}, {0x3ff3, 0x00a3}, {0x3ff8, 0x00b0},
    NOADD};
static const slapstic_config cfg108 = {3, {0x0028, 0x002a, 0x002c, 0x002e},
    {0x007f, 0x001f}, {0x3fff, 0x3772}, {0x3ffc, 0x3764}, {0x3ff9, 0x0028}, 0,
    {0x3ff0, 0x0060}, {0x3ff3, 0x0060}, {0x3ff3, 0x0061}, {0x3ff3, 0x0062}, {0x3ff3, 0x0063}, {0x3ff8, 0x0070},
    NOADD};

static inline int matches(uint32_t v, slapstic_mv m) { return (v & m.mask) == m.value; }

void slapstic_init(slapstic *s, int number, const slapstic_cpu *cpu)
{
    switch (number) {
    case 101: s->cfg = &cfg101; break;
    case 103: s->cfg = &cfg103; break;
    case 104: s->cfg = &cfg104; break;
    case 105: s->cfg = &cfg105; break;
    case 106: s->cfg = &cfg106; break;
    case 108: s->cfg = &cfg108; break;
    default: s->cfg = &cfg107; break;
    }
    if (cpu) s->cpu = *cpu;
    else s->cpu.pc = NULL;
    s->alt_bank = s->bit_bank = s->add_bank = s->bit_xor = 0;
    slapstic_reset(s);
}

void slapstic_reset(slapstic *s)
{
    s->state = S_DISABLED;
    s->current_bank = (uint8_t)s->cfg->bankstart;
}

static uint8_t alt2_kludge(slapstic *s)
{
    const slapstic_config *c = s->cfg;
    if (s->cpu.pc == NULL) return S_ALT2;
    if (!matches(s->cpu.pc(s->cpu.user) >> 1, c->alt1)) return S_ENABLED;
    uint16_t opcode = s->cpu.peek_word(s->cpu.user, s->cpu.ppc(s->cpu.user) & 0xffffff);
    if ((opcode & 0xf1f8) == 0x3090 || (opcode & 0xf1f8) == 0xb148) {
        uint32_t regval = s->cpu.areg(s->cpu.user, (opcode >> 9) & 7) >> 1;
        if (matches(regval, c->alt3)) {
            s->alt_bank = (uint8_t)((regval >> c->altshift) & 3);
            return S_ALT3;
        }
    }
    return S_ALT2;
}

uint8_t slapstic_tweak(slapstic *s, uint16_t offset)
{
    const slapstic_config *c = s->cfg;
    int is_bank = offset == c->bank[0] || offset == c->bank[1] || offset == c->bank[2] || offset == c->bank[3];

    if (offset == 0x0000) {
        s->state = S_ENABLED;
        return s->current_bank;
    }
    switch (s->state) {
    case S_DISABLED:
        break;
    case S_ENABLED:
        if (matches(offset, c->bit1)) s->state = S_BIT1;
        else if (matches(offset, c->add1)) s->state = S_ADD1;
        else if (matches(offset, c->alt1)) s->state = S_ALT1;
        else if (matches(offset, c->alt2)) s->state = alt2_kludge(s);
        else if (is_bank) {
            s->state = S_DISABLED;
            for (uint8_t b = 0; b < 4; ++b)
                if (offset == c->bank[b]) s->current_bank = b;
        }
        break;
    case S_ALT1:
        s->state = matches(offset, c->alt2) ? S_ALT2 : S_ENABLED;
        break;
    case S_ALT2:
        if (matches(offset, c->alt3)) {
            s->state = S_ALT3;
            s->alt_bank = (uint8_t)((offset >> c->altshift) & 3);
        } else {
            s->state = S_ENABLED;
        }
        break;
    case S_ALT3:
        if (matches(offset, c->alt4)) {
            s->state = S_DISABLED;
            s->current_bank = s->alt_bank;
        }
        break;
    case S_BIT1:
        if (is_bank) {
            s->state = S_BIT2;
            s->bit_bank = s->current_bank;
            s->bit_xor = 0;
        }
        break;
    case S_BIT2:
        if (matches(offset ^ s->bit_xor, c->bit2c0)) { s->bit_bank &= 0xfe; s->bit_xor ^= 3; }
        else if (matches(offset ^ s->bit_xor, c->bit2s0)) { s->bit_bank |= 1; s->bit_xor ^= 3; }
        else if (matches(offset ^ s->bit_xor, c->bit2c1)) { s->bit_bank &= 0xfd; s->bit_xor ^= 3; }
        else if (matches(offset ^ s->bit_xor, c->bit2s1)) { s->bit_bank |= 2; s->bit_xor ^= 3; }
        else if (matches(offset, c->bit3)) s->state = S_BIT3;
        break;
    case S_BIT3:
        if (is_bank) {
            s->state = S_DISABLED;
            s->current_bank = s->bit_bank;
        }
        break;
    case S_ADD1:
        if (matches(offset, c->add2)) {
            s->state = S_ADD2;
            s->add_bank = s->current_bank;
        } else {
            s->state = S_ENABLED;
        }
        break;
    case S_ADD2:
        if (matches(offset, c->addplus1)) s->add_bank = (uint8_t)((s->add_bank + 1) & 3);
        if (matches(offset, c->addplus2)) s->add_bank = (uint8_t)((s->add_bank + 2) & 3);
        if (matches(offset, c->add3)) s->state = S_ADD3;
        break;
    default:
        if (is_bank) {
            s->state = S_DISABLED;
            s->current_bank = s->add_bank;
        }
        break;
    }
    return s->current_bank;
}
