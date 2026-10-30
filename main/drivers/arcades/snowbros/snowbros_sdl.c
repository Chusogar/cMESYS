// snowbros_sdl.c - Snow Bros on the desktop (SDL2), same core as the CYD.
// ROMs are compiled in (rom_data.c, made by tools/gen_roms_snowbros.py).
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "sb_core.h"
#include "emu8950/emu8950.h"

// YM3812: the core time-stamps the register writes in YM samples.
typedef struct { uint32_t when; uint8_t reg, val; } oplw_t;
static oplw_t q[8192];
static int qn;
static OPL *opl;
static uint32_t synth_pos;

void sb_opl_write(uint32_t when, uint8_t reg, uint8_t val)
{
    if (qn < 8192) q[qn++] = (oplw_t){when, reg, val};
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

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);         // --mute, --sound, --scale N
    static uint8_t fb[SB_SCREEN_W * SB_SCREEN_H];
    static uint32_t rgb[SB_SCREEN_W * SB_SCREEN_H];
    static int16_t audio[4096];
    static sb_video_t v;

    sb_init(SB_OPL_RATE);
    opl = OPL_new(SB_YM_CLOCK, SB_OPL_RATE);
    OPL_setChipType(opl, 2);            // YM3812
    OPL_reset(opl);
    sdl_port_cfg cfg = {"Snow Bros", SB_SCREEN_W, SB_SCREEN_H, 3, SB_OPL_RATE, SB_FPS};
    if (!sdl_port_open(&cfg)) return 1;

    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) sb_reset();
        if (sdl_port_paused()) { sdl_port_present(rgb); continue; }
        uint32_t k = sdl_port_keys();
        uint16_t p1 = 0x7fff, sys = 0xffff;
        if (k & SP_UP) p1 &= (uint16_t)~SB_IN_UP;
        if (k & SP_DOWN) p1 &= (uint16_t)~SB_IN_DOWN;
        if (k & SP_LEFT) p1 &= (uint16_t)~SB_IN_LEFT;
        if (k & SP_RIGHT) p1 &= (uint16_t)~SB_IN_RIGHT;
        if (k & SP_B1) p1 &= (uint16_t)~SB_IN_B1;       // shoot snow
        if (k & SP_B2) p1 &= (uint16_t)~SB_IN_B2;       // jump
        if (k & SP_COIN1) sys &= (uint16_t)~SB_SYS_COIN1;
        if (k & SP_COIN2) sys &= (uint16_t)~SB_SYS_COIN2;
        if (k & SP_START1) sys &= (uint16_t)~SB_SYS_START1;
        if (k & SP_START2) sys &= (uint16_t)~SB_SYS_START2;
        sb_input[0] = p1;
        sb_input[2] = sys;
        sb_run_frame();
        sdl_port_audio(audio, synth_until(audio, 4096, sb_sample_clock()));
        sb_snapshot(&v);
        sb_render(&v, fb);
        const uint16_t *pal = sb_palette();     // xBGR555
        for (int i = 0; i < SB_SCREEN_W * SB_SCREEN_H; i++) {
            uint16_t c = pal[fb[i]];
            unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            rgb[i] = ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
        sdl_port_present(rgb);
    }
    sdl_port_close();
    return 0;
}
