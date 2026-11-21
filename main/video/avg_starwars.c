// avg_starwars.c - the Star Wars AVG (see avg_starwars.h).
#include "avg_starwars.h"
#include <stdlib.h>
#include <string.h>

#define MAX_VECT 10000

bool avg_init(avg_starwars *a)
{
    a->xmin = 0;
    a->ymin = 0;
    a->xmax = AVG_VIS_W - 1;
    a->ymax = AVG_VIS_H - 1;
    a->halt = a->sync_halt = 1;
    a->xdac_xor = a->ydac_xor = 0x200;
    if (!a->sink && !a->pending) {
        a->pending = (avg_line *)malloc(AVG_MAX_LINES * sizeof(avg_line));
        a->display = (avg_line *)malloc(AVG_MAX_LINES * sizeof(avg_line));
        if (!a->pending || !a->display) return false;
    }
    return true;
}

void avg_set_prom(avg_starwars *a, const uint8_t *data, int size)
{
    memset(a->prom, 0, sizeof a->prom);
    if (data && size > 0) memcpy(a->prom, data, size < 256 ? (size_t)size : 256);
}

static void vgrst(avg_starwars *a)
{
    a->state_latch = 0;
    a->bin_scale = 0;
    a->scale = 0;
    a->color = 0;
}

static void vg_set_halt(avg_starwars *a, int v)
{
    a->halt = (uint8_t)v;
    a->sync_halt = (uint8_t)v;
}

void avg_vg_reset(avg_starwars *a)
{
    vgrst(a);
    vg_set_halt(a, 1);
}

void avg_reset(avg_starwars *a)
{
    avg_vg_reset(a);
    a->nvect = 0;
    a->npending = a->ndisplay = 0;
    a->pc = 0;
    a->sp = 0;
    a->dvx = a->dvy = 0;
    a->data = 0;
    a->op = 0;
    a->timer = 0;
    a->int_latch = 0;
    a->xpos = a->xcenter = ((a->xmax - a->xmin) / 2) << 16;
    a->ypos = a->ycenter = ((a->ymax - a->ymin) / 2) << 16;
    a->xdac_xor = a->ydac_xor = 0x200;
}

#define OP0(a) ((a)->op & 1)
#define OP1(a) (((a)->op >> 1) & 1)
#define OP2(a) (((a)->op >> 2) & 1)
#define ST3(a) (((a)->state_latch >> 3) & 1)

static uint8_t state_addr(const avg_starwars *a)
{
    return (uint8_t)((((a->state_latch >> 4) ^ 1) << 7) | (a->op << 4) | (a->state_latch & 0xf));
}

static void add_point(avg_starwars *a, int x, int y, int color, int intensity)
{
    if (a->nvect >= MAX_VECT) return;
    if (a->nvect == 0) a->last_x = x, a->last_y = y;
    if (intensity > 0) {
        avg_line l;
        l.x0 = (int16_t)(a->last_x >> 16);
        l.y0 = (int16_t)(a->last_y >> 16);
        l.x1 = (int16_t)(x >> 16);
        l.y1 = (int16_t)(y >> 16);
        l.color = (uint8_t)(color & 7);
        l.intensity = (uint8_t)(intensity < 255 ? intensity : 255);
        if (a->sink) a->sink(a->user, &l);
        else if (a->npending < AVG_MAX_LINES) a->pending[a->npending++] = l;
    }
    a->last_x = x;
    a->last_y = y;
    a->nvect++;
}

static void flush(avg_starwars *a)
{
    avg_line *t = a->display;
    a->display = a->pending;
    a->ndisplay = a->npending;
    a->pending = t;
    a->npending = 0;
    a->nvect = 0;
}

static void h0(avg_starwars *a) { a->dvy = (uint16_t)((a->dvy & 0x1f00) | a->data); a->pc++; }

static void h1(avg_starwars *a)
{
    a->dvy12 = (uint8_t)((a->data >> 4) & 1);
    a->op = (uint8_t)(a->data >> 5);
    a->int_latch = 0;
    a->dvy = (uint16_t)((a->dvy12 << 12) | ((a->data & 0xf) << 8));
    a->dvx = 0;
    a->pc++;
}

static void h2(avg_starwars *a) { a->dvx = (uint16_t)((a->dvx & 0x1f00) | a->data); a->pc++; }

static void h3(avg_starwars *a)
{
    a->int_latch = (uint8_t)(a->data >> 4);
    a->dvx = (uint16_t)(((uint16_t)(a->int_latch & 1) << 12) | ((a->data & 0xf) << 8) | (a->dvx & 0xff));
    a->pc++;
}

static void h4(avg_starwars *a)
{
    if (OP0(a)) {
        a->stack[a->sp & 3] = a->pc;
    } else {
        int i = 0;
        while ((((a->dvy ^ (uint16_t)(a->dvy << 1)) & 0x1000) == 0) &&
               (((a->dvx ^ (uint16_t)(a->dvx << 1)) & 0x1000) == 0) && (i++ < 16)) {
            a->dvy = (uint16_t)((a->dvy & 0x1000) | ((a->dvy << 1) & 0x1fff));
            a->dvx = (uint16_t)((a->dvx & 0x1000) | ((a->dvx << 1) & 0x1fff));
            a->timer = (uint16_t)((a->timer >> 1) | 0x4000 | ((uint16_t)OP1(a) << 7));
        }
        if (OP1(a)) a->timer &= 0xff;
    }
}

static void strobe1(avg_starwars *a)
{
    if (OP2(a)) {
        if (OP1(a)) a->sp = (uint8_t)((a->sp - 1) & 0xf);
        else a->sp = (uint8_t)((a->sp + 1) & 0xf);
    }
}

static void h5(avg_starwars *a)
{
    if (!OP2(a)) {
        for (int i = a->bin_scale; i > 0; i--)
            a->timer = (uint16_t)((a->timer >> 1) | 0x4000 | ((uint16_t)OP1(a) << 7));
        if (OP1(a)) a->timer &= 0xff;
    }
    strobe1(a);
}

static void strobe2(avg_starwars *a)
{
    if (OP2(a)) {
        if (OP0(a)) {
            a->pc = (uint16_t)(a->dvy << 1);
            // a jump to 0 ends a frame once there is a list (as dsp-cpp)
            if (a->dvy == 0 && a->nvect > 10) a->halt = 1;
        } else {
            a->pc = a->stack[a->sp & 3];
        }
    } else if (a->dvy12) {
        a->scale = (uint8_t)(a->dvy & 0xff);
        a->bin_scale = (uint8_t)((a->dvy >> 8) & 7);
    }
}

static void h6(avg_starwars *a)
{
    if (!OP2(a) && !a->dvy12) {
        a->intensity = (uint8_t)(a->dvy & 0xff);
        a->color = (uint8_t)((a->dvy >> 8) & 0xf);
    }
    strobe2(a);
}

static void strobe3(avg_starwars *a)
{
    int cycles = 0;
    a->halt = OP0(a);
    if (!OP0(a) && !OP2(a)) {
        if (OP1(a)) cycles = 0x100 - (a->timer & 0xff);
        else cycles = 0x8000 - a->timer;
        a->timer = 0;
        a->xpos += ((((int)(a->dvx >> 3) ^ (int)a->xdac_xor) - 0x200) * cycles * (a->scale ^ 0xff)) >> 4;
        a->ypos -= ((((int)(a->dvy >> 3) ^ (int)a->ydac_xor) - 0x200) * cycles * (a->scale ^ 0xff)) >> 4;
    }
    if (OP2(a)) {
        a->timer = 0;
        a->xpos = a->xcenter;
        a->ypos = a->ycenter;
        add_point(a, (int)a->xpos, (int)a->ypos, 0, 0);
    }
}

static void h7(avg_starwars *a)
{
    strobe3(a);
    if (!OP0(a) && !OP2(a))
        add_point(a, (int)a->xpos, (int)a->ypos, a->color, (((int)a->int_latch >> 1) * (int)a->intensity) >> 3);
}

static void run_state_machine(avg_starwars *a)
{
    int extra = 0;
    for (int step = 0; step < 200000; step++) {
        a->state_latch = (uint8_t)((a->state_latch & 0x10) | (a->prom[state_addr(a)] & 0xf));
        if (ST3(a)) {
            a->data = a->read ? a->read(a->user, a->pc) : 0;
            switch (a->state_latch & 7) {
            case 0: h0(a); break;
            case 1: h1(a); break;
            case 2: h2(a); break;
            case 3: h3(a); break;
            case 4: h4(a); break;
            case 5: h5(a); break;
            case 6: h6(a); break;
            default: h7(a); break;
            }
        }
        a->state_latch = (uint8_t)((a->halt << 4) | (a->state_latch & 0xf));
        if (a->halt && ++extra > 24) break;
    }
    a->sync_halt = a->halt ? 1 : 0;
}

void avg_go(avg_starwars *a)
{
    a->pc = 0;
    a->sp = 0;
    a->nvect = 0;
    a->npending = 0;
    vg_set_halt(a, 0);
    run_state_machine(a);
    flush(a);
}
