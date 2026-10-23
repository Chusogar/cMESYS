// snowbros_pico.c - Snow Bros (Toaplan, 1990) on the Waveshare RP2040-PiZero
// (HDMI). Same core as the CYD and the desktop.
//
// Keys: 5 coin, 1 start, cursors, Z shoot, X jump, F3 reset; USB pad:
// A shoot, B jump, SELECT coin, START start.
#include <string.h>
#include "pico_port.h"
#include "sb_core.h"
#include "emu8950/emu8950.h"

#define X0 ((PICO_W - SB_SCREEN_W) / 2)
#define Y0 ((PICO_H - SB_SCREEN_H) / 2)

// YM3812: the core time-stamps the register writes in YM samples.
typedef struct { uint32_t when; uint8_t reg, val; } oplw_t;
#define QMAX 1024
static oplw_t q[QMAX];
static int qn;
static OPL *opl;
static uint32_t synth_pos;

void sb_opl_write(uint32_t when, uint8_t reg, uint8_t val)
{
    if (qn < QMAX) q[qn++] = (oplw_t){when, reg, val};
}

static int synth_until(int16_t *out, int cap, uint32_t end)
{
    int n = 0, qi = 0;
    while ((int32_t)(end - synth_pos) > 0 && n < cap) {
        while (qi < qn && (int32_t)(q[qi].when - synth_pos) <= 0) {
            OPL_writeReg(opl, q[qi].reg, q[qi].val);
            qi++;
        }
        out[n++] = OPL_calc(opl);
        synth_pos++;
    }
    for (; qi < qn; qi++) OPL_writeReg(opl, q[qi].reg, q[qi].val);
    qn = 0;
    return n;
}

static uint8_t fb[SB_SCREEN_W * SB_SCREEN_H];     // drawn by core 0 after each frame
static uint16_t pal[256];
static sb_video_t snap;
static int16_t audio[1024];

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        int sy = y + i - Y0;
        if (sy < 0 || sy >= SB_SCREEN_H) { pico_fill(out, PICO_W, 0); continue; }
        const uint8_t *src = fb + sy * SB_SCREEN_W;
        pico_fill(out, X0, 0);
        for (int x = 0; x < SB_SCREEN_W; x++) out[X0 + x] = pal[src[x]];
        pico_fill(out + X0 + SB_SCREEN_W, PICO_W - X0 - SB_SCREEN_W, 0);
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) sb_reset();
        uint32_t k = pico_port_keys();
        uint16_t p1 = 0x7fff, sys = 0xffff;
        if (k & SP_UP) p1 &= (uint16_t)~SB_IN_UP;
        if (k & SP_DOWN) p1 &= (uint16_t)~SB_IN_DOWN;
        if (k & SP_LEFT) p1 &= (uint16_t)~SB_IN_LEFT;
        if (k & SP_RIGHT) p1 &= (uint16_t)~SB_IN_RIGHT;
        if (k & SP_B1) p1 &= (uint16_t)~SB_IN_B1;
        if (k & SP_B2) p1 &= (uint16_t)~SB_IN_B2;
        if (k & SP_COIN1) sys &= (uint16_t)~SB_SYS_COIN1;
        if (k & SP_COIN2) sys &= (uint16_t)~SB_SYS_COIN2;
        if (k & SP_START1) sys &= (uint16_t)~SB_SYS_START1;
        if (k & SP_START2) sys &= (uint16_t)~SB_SYS_START2;
        sb_input[0] = p1;
        sb_input[2] = sys;
        sb_run_frame();
        int n = synth_until(audio, 1024, sb_sample_clock());
        sb_snapshot(&snap);
        sb_render(&snap, fb);
        const uint16_t *p = sb_palette();           // xBGR555
        for (int i = 0; i < 256; i++) {
            unsigned r = p[i] & 31, g = (p[i] >> 5) & 31, b = (p[i] >> 10) & 31;
            pal[i] = pico_rgb565((uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 3 | g >> 2), (uint8_t)(b << 3 | b >> 2));
        }
        pico_port_audio(audio, n);
        pico_port_frame_done();
    }
}

int main(void)
{
    sb_init(SB_OPL_RATE);
    opl = OPL_new(SB_YM_CLOCK, SB_OPL_RATE);
    OPL_setChipType(opl, 2);                    // YM3812
    OPL_reset(opl);
    pico_port_cfg cfg = { "Snow Bros", render, NULL, SB_OPL_RATE, SB_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
