// gauntlet_pico.c - Gauntlet (Atari, 1985) on the Waveshare RP2040-PiZero
// (HDMI). Same core as the CYD and the desktop; core 1 draws the picture
// from the core's video RAM (336 wide: the middle 320 columns are shown).
//
// Keys (player 1): 5 coin, cursors, Z / Ctrl fire, X / Alt or 1 magic (joins
// the game), F3 reset; USB pad: A fire, B or START magic, SELECT coin.
// (The EEPROM is not kept: the board has no storage in use.)
#include <string.h>
#include "pico_port.h"
#include "gauntlet_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define XSKIP ((GT_SCREEN_W - PICO_W) / 2)

static gt_core core;
static gt_renderer rend;
static uint16_t pal[0x400];
static int16_t abuf[2048];
static uint16_t cband[GT_SCREEN_W * 4];

static void on_frame(void *user, gt_core *c)
{
    (void)user;
    for (int i = 0; i < 0x400; i++) {
        uint32_t rgb = gt_palette_rgb(&c->v, i);
        pal[i] = pico_rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
    }
}

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    gt_compose(&rend, &core.roms, &core.v, y, y + n, cband);
    for (int i = 0; i < n; i++, out += PICO_W) {
        const uint16_t *src = cband + i * GT_SCREEN_W + XSKIP;
        for (int x = 0; x < PICO_W; x++) out[x] = pal[src[x]];
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) gt_reset(&core);
        uint32_t k = pico_port_keys();
        gt_inputs in;
        memset(&in, 0, sizeof in);
        in.coin[0] = k & SP_COIN1;
        in.coin[1] = k & SP_COIN2;
        in.p[0].up = k & SP_UP;
        in.p[0].down = k & SP_DOWN;
        in.p[0].left = k & SP_LEFT;
        in.p[0].right = k & SP_RIGHT;
        in.p[0].fire = k & SP_B1;
        in.p[0].magic = k & (SP_B2 | SP_START1);
        gt_set_inputs(&core, &in);
        gt_run_frame(&core);
        pico_port_audio(abuf, core.audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    gt_roms roms;
    gt_rom_data(&roms);
    gt_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;
    core.on_frame = on_frame;
    core.audio = abuf;
    core.audio_cap = 2048;
    gt_renderer_init(&rend);
    on_frame(NULL, &core);
    pico_port_cfg cfg = { "Gauntlet", render, NULL, AUDIO_RATE, GT_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
