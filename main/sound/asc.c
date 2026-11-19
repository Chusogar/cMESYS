// asc.c - Apple Sound Chip: C11 migration of dsp-cpp's sound/asc.cpp.
#include "asc.h"

#include <string.h>

enum { R_VERSION = 0x00, R_MODE = 0x01, R_CONTROL = 0x02, R_FIFOMODE = 0x03, R_FIFOSTAT = 0x04 };
#define HALF_A 0x01
#define FULL_A 0x02
#define HALF_B 0x04
#define FULL_B 0x08

static void set_irq(asc *a, bool on)
{
    a->irq_line = on;
    if (a->irq) a->irq(a->user, on);
}

void asc_reset(asc *a)
{
    memset(a->regs, 0, sizeof a->regs);
    memset(a->fifo, 0, sizeof a->fifo);
    for (int i = 0; i < 2; i++) a->rdptr[i] = a->wrptr[i] = a->cap[i] = 0;
    for (int i = 0; i < 4; i++) a->phase[i] = a->incr[i] = 0;
    set_irq(a, false);
}

uint8_t asc_read(asc *a, uint32_t offset)
{
    offset &= 0xfff;
    if (offset < 0x400) return a->fifo[0][offset];
    if (offset < 0x800) return a->fifo[1][offset - 0x400];
    const uint32_t r = offset - 0x800;
    if (r == R_VERSION) return 0x00;
    if (r == R_FIFOSTAT) {
        const uint8_t v = a->regs[R_FIFOSTAT];
        a->regs[R_FIFOSTAT] = 0;
        set_irq(a, false);
        return v;
    }
    if (r >= 0x10 && r < 0x30) {
        const int ch = (int)((r - 0x10) >> 3);
        const uint32_t v = (r & 4) ? a->incr[ch] : a->phase[ch];
        switch (r & 3) {
        case 1: return (uint8_t)(v >> 16);
        case 2: return (uint8_t)(v >> 8);
        case 3: return (uint8_t)v;
        default: return 0;
        }
    }
    return a->regs[r & 0x7ff];
}

void asc_write(asc *a, uint32_t offset, uint8_t data)
{
    offset &= 0xfff;
    if (offset < 0x800) {
        const int f = offset < 0x400 ? 0 : 1;
        if ((a->regs[R_MODE] & 3) == 2) {       // wavetables: plain RAM
            a->fifo[f][offset & 0x3ff] = data;
            return;
        }
        a->fifo[f][a->wrptr[f]] = data;
        a->wrptr[f] = (a->wrptr[f] + 1) & 0x3ff;
        if (a->cap[f] < 0x400) a->cap[f]++;
        const uint8_t half = f ? HALF_B : HALF_A, full = f ? FULL_B : FULL_A;
        if (a->cap[f] >= 0x200) {
            a->regs[R_FIFOSTAT] &= (uint8_t)~half;
            if (a->cap[f] >= 0x3ff) a->regs[R_FIFOSTAT] |= full;
        } else if (a->cap[f] > 0) {
            a->regs[R_FIFOSTAT] &= (uint8_t)~full;
        }
        return;
    }
    const uint32_t r = offset - 0x800;
    switch (r) {
    case R_MODE:
        data &= 3;
        if (data != a->regs[R_MODE])
            for (int i = 0; i < 2; i++) a->rdptr[i] = a->wrptr[i] = a->cap[i] = 0;
        a->regs[R_MODE] = data;
        return;
    case R_FIFOMODE:
        if (data & 0x80) {
            for (int i = 0; i < 2; i++) a->rdptr[i] = a->wrptr[i] = a->cap[i] = 0;
            a->regs[R_FIFOSTAT] |= 0x0a;
        }
        a->regs[R_FIFOMODE] = data;
        return;
    case R_FIFOSTAT:
        return;
    default: break;
    }
    if (r >= 0x10 && r < 0x30) {
        const int ch = (int)((r - 0x10) >> 3);
        uint32_t *v = (r & 4) ? &a->incr[ch] : &a->phase[ch];
        switch (r & 3) {
        case 1: *v = (*v & 0x00ffff) | ((uint32_t)data << 16); break;
        case 2: *v = (*v & 0xff00ff) | ((uint32_t)data << 8); break;
        case 3: *v = (*v & 0xffff00) | data; break;
        default: break;
        }
        return;
    }
    if (offset == 0xe00) {
        a->regs[R_FIFOSTAT] |= 0x0f;
        set_irq(a, true);
    }
    a->regs[r & 0x7ff] = data;
}

int32_t asc_generate(asc *a)
{
    switch (a->regs[R_MODE] & 3) {
    case 1: {                           // FIFO
        const bool stereo = a->regs[R_CONTROL] & 2;
        const int32_t sa = (int8_t)(a->fifo[0][a->rdptr[0]] ^ 0x80);
        const int32_t sb = stereo ? (int8_t)(a->fifo[1][a->rdptr[1]] ^ 0x80) : sa;
        const int cap_a = a->cap[0], cap_b = a->cap[1];
        if (a->cap[0]) {
            a->rdptr[0] = (a->rdptr[0] + 1) & 0x3ff;
            a->cap[0]--;
        }
        if (stereo && a->cap[1]) {
            a->rdptr[1] = (a->rdptr[1] + 1) & 0x3ff;
            a->cap[1]--;
        }
        if (cap_a == 0x1ff) {
            a->regs[R_FIFOSTAT] = (uint8_t)((a->regs[R_FIFOSTAT] | HALF_A) & ~FULL_A);
            set_irq(a, true);
        }
        if (stereo && cap_b == 0x1ff) {
            a->regs[R_FIFOSTAT] = (uint8_t)((a->regs[R_FIFOSTAT] | HALF_B) & ~FULL_B);
            set_irq(a, true);
        }
        return (sa + sb) * 2;
    }
    case 2: {                           // wavetables
        int32_t mix = 0;
        for (int ch = 0; ch < 4; ch++) {
            a->phase[ch] = (a->phase[ch] + a->incr[ch]) & 0xffffff;
            const uint8_t *table = a->fifo[ch < 2 ? 0 : 1];
            mix += (int8_t)(table[((a->phase[ch] >> 15) & 0x1ff) + ((ch & 1) ? 0x200 : 0)] ^ 0x80);
        }
        return mix;
    }
    default: return 0;
    }
}
