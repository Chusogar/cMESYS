// as1_pico.inc - Atari System 1 frontend for the Waveshare RP2040-PiZero
// (HDMI), included by indytemp_pico.c and roadrunn_pico.c with:
//   AS1_PICO_TITLE      the game's name
//   AS1_PICO_FRAME_MO   1: the core builds the motion objects as the RAM
//                       changes (Road Runner); 0: built here once a frame (Indy)
// Same core as the CYD and the desktop; core 1 draws the picture from the
// core's video RAM (336 wide: the middle 320 columns are shown).
// (The EEPROM is not kept: the board has no storage in use.)
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "atarisy1_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define XSKIP ((AS1_SCREEN_W - PICO_W) / 2)

static as1_core core;
#if PICO_RP2350
// RP2350: core 1 draws a whole picture while core 0 runs the next frame,
// which empties or rebuilds the motion object list: the sprites came and
// went. Three lists: the one being built, the last finished one and the one
// core 1 is drawing from.
#define AS1_MO3 1
#include "hardware/sync.h"
static as1_mo_list *mo_buf[3];
static int mo_build, mo_ready = -1, mo_used = -1;
static spin_lock_t *mo_lock;
static const as1_mo_list *mo_draw;
#else
#define AS1_MO3 0
#if !AS1_PICO_FRAME_MO
static as1_mo_list mol;
#endif
#endif
static uint16_t pal[0x400];
static int16_t abuf[2048];
static uint16_t cband[AS1_SCREEN_W * 4];

static void on_frame(void *user, as1_core *c)
{
    (void)user;
#if AS1_MO3
    if (mo_lock) {
#if !AS1_PICO_FRAME_MO
        as1_build_mo_list(&c->gfx, &c->v, mo_buf[mo_build]);
#endif
        uint32_t irq = spin_lock_blocking(mo_lock);
        mo_ready = mo_build;
        int b = 0;
        while (b == mo_ready || b == mo_used) b++;
        mo_build = b;
        spin_unlock(mo_lock, irq);
#if AS1_PICO_FRAME_MO
        c->frame_mo = mo_buf[b];            // (emptied when the next frame starts)
#endif
    }
#elif !AS1_PICO_FRAME_MO
    as1_build_mo_list(&c->gfx, &c->v, &mol);
#endif
    for (int i = 0; i < 0x400; i++) {
        uint32_t rgb = as1_palette_rgb(&c->v, i);
        pal[i] = pico_rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
    }
}

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
#if AS1_MO3
    if (y == 0) {                           // a new picture: the last finished list
        uint32_t irq = spin_lock_blocking(mo_lock);
        mo_used = mo_ready;
        spin_unlock(mo_lock, irq);
        mo_draw = mo_used >= 0 ? mo_buf[mo_used] : NULL;
    }
    const as1_mo_list *mo = mo_draw;
    if (!mo) {
        for (int i = 0; i < n * PICO_W; i++) out[i] = pal[0];
        return;
    }
#elif AS1_PICO_FRAME_MO
    const as1_mo_list *mo = core.frame_mo;
#else
    const as1_mo_list *mo = &mol;
#endif
    as1_compose(&core.gfx, &core.v, mo, y, y + n, cband);
    for (int i = 0; i < n; i++, out += PICO_W) {
        const uint16_t *src = cband + i * AS1_SCREEN_W + XSKIP;
        for (int x = 0; x < PICO_W; x++) out[x] = pal[src[x]];
    }
}

static void game(void)
{
#if AS1_MO3
    // (after pico_port_open: the frame buffers have the SRAM first)
    mo_buf[0] = AS1_PICO_FRAME_MO ? core.frame_mo : (as1_mo_list *)calloc(1, sizeof *mo_buf[0]);
    mo_buf[1] = (as1_mo_list *)calloc(1, sizeof *mo_buf[1]);
    mo_buf[2] = (as1_mo_list *)calloc(1, sizeof *mo_buf[2]);
    mo_lock = spin_lock_instance((uint)spin_lock_claim_unused(true));
#endif
    for (;;) {
        if (pico_port_poll()) as1_reset(&core);
        uint32_t k = pico_port_keys();
        as1_inputs in = {
            .coin1 = k & SP_COIN1, .coin2 = k & SP_COIN2, .start = k & (SP_START1 | SP_START2),
            .button1 = k & SP_B1, .button2 = k & SP_B2,
            .up = k & SP_UP, .down = k & SP_DOWN, .left = k & SP_LEFT, .right = k & SP_RIGHT,
        };
        as1_set_inputs(&core, &in);
        as1_run_frame(&core);
        pico_port_audio(abuf, core.audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    as1_roms roms;
    as1_rom_data(&roms);
    as1_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;
    core.on_frame = on_frame;
    core.audio = abuf;
    core.audio_cap = 2048;
    on_frame(NULL, &core);
    pico_port_cfg cfg = { AS1_PICO_TITLE, render, NULL, AUDIO_RATE, AS1_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
