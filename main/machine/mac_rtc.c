// mac_rtc.c - Macintosh Plus clock / PRAM: C11 migration of dsp-cpp's
// machine/mac_rtc.cpp.
#include "mac_rtc.h"

#include <string.h>

static void store_seconds(mac_rtc *r)
{
    r->mem[0] = (uint8_t)(r->seconds >> 24);
    r->mem[1] = (uint8_t)(r->seconds >> 16);
    r->mem[2] = (uint8_t)(r->seconds >> 8);
    r->mem[3] = (uint8_t)r->seconds;
}

void mac_rtc_reset(mac_rtc *r)
{
    memset(r, 0, sizeof *r);
    r->seconds = 2716300800u;   // 1990-01-01, seconds since 1904
    store_seconds(r);
    r->data_in = r->data_out = true;
}

void mac_rtc_ce(mac_rtc *r, bool level)
{
    if (level == r->ce) return;
    r->ce = level;
    if (r->ce) {
        r->bit = r->state = 0;
        r->cmd = r->data = 0;
    }
    r->data_out = true;
}

void mac_rtc_clk(mac_rtc *r, bool level)
{
    if (!r->ce || level == r->clk) {
        r->clk = level;
        return;
    }
    const bool rise = level && !r->clk;
    r->clk = level;
    if (!rise) return;
    if (r->state == 0) {
        r->cmd = (uint8_t)((r->cmd << 1) | (r->data_in ? 1 : 0));
        if (++r->bit == 8) {
            r->bit = 0;
            if (r->cmd & 0x80) {
                r->state = 1;
                r->data = 0;
            } else {
                r->state = 2;
                r->data = r->mem[r->cmd & 0x1f];
                r->data_out = (r->data & 0x80) != 0;
            }
        }
    } else if (r->state == 1) {
        r->data = (uint8_t)((r->data << 1) | (r->data_in ? 1 : 0));
        if (++r->bit == 8) {
            r->mem[r->cmd & 0x1f] = r->data;
            if ((r->cmd & 0x1f) <= 3)
                r->seconds = ((uint32_t)r->mem[0] << 24) | ((uint32_t)r->mem[1] << 16) | ((uint32_t)r->mem[2] << 8) | r->mem[3];
            r->state = r->bit = 0;
        }
    } else {
        r->data = (uint8_t)(r->data << 1);
        r->data_out = (r->data & 0x80) != 0;
        if (++r->bit == 8) {
            r->state = r->bit = 0;
            r->data_out = true;
        }
    }
}

void mac_rtc_tick_second(mac_rtc *r)
{
    r->seconds++;
    store_seconds(r);
}
