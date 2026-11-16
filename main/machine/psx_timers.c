// psx_timers.c - the PlayStation's root counters (see psx_timers.h).
#include "psx_timers.h"
#include <string.h>

static void timer_init(psx_timer *t, int number)
{
    memset(t, 0, sizeof *t);
    t->number = number;
    t->interrupt_request = 1;
    t->dot_div = 1;
}

void psx_timers_reset(psx_timers *P)
{
    for (int i = 0; i < 3; i++) timer_init(&P->t[i], i);
}

static uint32_t get_mode(psx_timer *t)
{
    uint32_t mode = 0;
    mode |= t->sync_enable;
    mode |= (uint32_t)t->sync_mode << 1;
    mode |= (uint32_t)t->reset_on_target << 3;
    mode |= (uint32_t)t->irq_on_target << 4;
    mode |= (uint32_t)t->irq_on_ffff << 5;
    mode |= (uint32_t)t->irq_repeat << 6;
    mode |= (uint32_t)t->irq_pulse << 7;
    mode |= (uint32_t)t->clock_source << 8;
    mode |= (uint32_t)t->interrupt_request << 10;
    mode |= (uint32_t)t->reached_target << 11;
    mode |= (uint32_t)t->reached_ffff << 12;
    t->reached_target = 0;
    t->reached_ffff = 0;
    return mode;
}

static void set_mode(psx_timer *t, uint32_t value)
{
    t->sync_enable = (uint8_t)(value & 1);
    t->sync_mode = (uint8_t)((value >> 1) & 3);
    t->reset_on_target = (uint8_t)((value >> 3) & 1);
    t->irq_on_target = (uint8_t)((value >> 4) & 1);
    t->irq_on_ffff = (uint8_t)((value >> 5) & 1);
    t->irq_repeat = (uint8_t)((value >> 6) & 1);
    t->irq_pulse = (uint8_t)((value >> 7) & 1);
    t->clock_source = (uint8_t)((value >> 8) & 3);
    t->interrupt_request = 1;
    t->already_fired = false;
    t->counter = 0;
}

uint32_t psx_timers_load(psx_timers *P, uint32_t addr)
{
    const int n = (int)((addr & 0xF0) >> 4);
    if (n > 2) return 0xFFFFFFFFu;
    psx_timer *t = &P->t[n];
    switch (addr & 0xF) {
    case 0x0: return t->counter;
    case 0x4: return get_mode(t);
    case 0x8: return t->target;
    default: return 0;
    }
}

void psx_timers_write(psx_timers *P, uint32_t addr, uint32_t value)
{
    const int n = (int)((addr & 0xF0) >> 4);
    if (n > 2) return;
    psx_timer *t = &P->t[n];
    switch (addr & 0xF) {
    case 0x0: t->counter = (uint16_t)value; break;
    case 0x4: set_mode(t, value); break;
    case 0x8: t->target = value; break;
    default: break;
    }
}

static void sync_one(psx_timer *t, const psx_gpu_sync *s)
{
    t->prev_hblank = t->hblank;
    t->prev_vblank = t->vblank;
    t->dot_div = s->dot_div;
    t->hblank = s->hblank;
    t->vblank = s->vblank;
}

void psx_timers_sync_gpu(psx_timers *P, const psx_gpu_sync *sync)
{
    sync_one(&P->t[0], sync);
    sync_one(&P->t[1], sync);
}

static bool handle_irq(psx_timer *t)
{
    bool irq = false;
    if (t->counter >= t->target) {
        t->reached_target = 1;
        if (t->reset_on_target == 1) t->counter = 0;
        if (t->irq_on_target == 1) irq = true;
    }
    if (t->counter >= 0xFFFF) {
        t->reached_ffff = 1;
        if (t->irq_on_ffff == 1) irq = true;
    }
    t->counter &= 0xFFFF;
    if (!irq) return false;
    if (t->irq_pulse == 0) t->interrupt_request = 0;
    else t->interrupt_request = (uint8_t)((t->interrupt_request + 1) & 1);
    const bool trigger = t->interrupt_request == 0;
    if (t->irq_repeat == 0) {
        if (!t->already_fired && trigger) t->already_fired = true;
        else return false;
    }
    t->interrupt_request = 1;
    return trigger;
}

bool psx_timers_tick(psx_timers *P, int n, int cycles_ticked)
{
    if (n < 0 || n > 2) return false;
    psx_timer *t = &P->t[n];
    t->cycle_accum += cycles_ticked;
    switch (n) {
    case 0:
        if (t->sync_enable == 1) {
            switch (t->sync_mode) {
            case 0: if (t->hblank) return false; break;
            case 1: if (t->hblank) t->counter = 0; break;
            case 2:
                if (t->hblank) t->counter = 0;
                if (!t->hblank) return false;
                break;
            case 3:
                if (!t->prev_hblank && t->hblank) t->sync_enable = 0;
                else return false;
                break;
            }
        }
        if (t->clock_source == 0 || t->clock_source == 2) {
            t->counter += (uint16_t)t->cycle_accum;
            t->cycle_accum = 0;
        } else {
            const uint16_t dots = (uint16_t)(t->cycle_accum * 11 / 7 / (t->dot_div > 1 ? t->dot_div : 1));
            t->counter += dots;
            t->cycle_accum = 0;
        }
        return handle_irq(t);
    case 1:
        if (t->sync_enable == 1) {
            switch (t->sync_mode) {
            case 0: if (t->vblank) return false; break;
            case 1: if (t->vblank) t->counter = 0; break;
            case 2:
                if (t->vblank) t->counter = 0;
                if (!t->vblank) return false;
                break;
            case 3:
                if (!t->prev_vblank && t->vblank) t->sync_enable = 0;
                else return false;
                break;
            }
        }
        if (t->clock_source == 0 || t->clock_source == 2) {
            t->counter += (uint16_t)t->cycle_accum;
            t->cycle_accum = 0;
        } else {
            if (!t->prev_hblank && t->hblank) t->counter += 1;
            t->cycle_accum = 0;
        }
        return handle_irq(t);
    default:
        if (t->sync_enable == 1 && (t->sync_mode == 0 || t->sync_mode == 3)) return false;
        if (t->clock_source == 0 || t->clock_source == 1) {
            t->counter += (uint16_t)t->cycle_accum;
            t->cycle_accum = 0;
        } else {
            t->counter += (uint16_t)(t->cycle_accum / 8);
            t->cycle_accum %= 8;
        }
        return handle_irq(t);
    }
}
