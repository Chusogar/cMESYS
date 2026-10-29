// pp_pico.inc - Pole Position / Pole Position II on the Waveshare
// RP2040-PiZero (HDMI). Included by polepos_pico.c and polepos2_pico.c
// (PP_TITLE). Same core as the CYD and the desktop.
//
// The 256x224 picture in the middle of the 320x240 screen, drawn by core 1
// from the core's RAM as it is when the rows are asked for. Keys: 5 coin,
// Left / Right steer, Up or Z gas, Down or X brake, C or Space gear, F3
// reset; or a USB pad (SELECT coin, A gas, B brake, X gear).
#include <string.h>
#include <stdlib.h>
#include "pico_port.h"
#include "pp_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define X0 ((PICO_W - PP_W) / 2)
#define Y0 ((PICO_H - PP_H) / 2)

static pp_core core;
static uint16_t pal[128];
static int16_t abuf[1024];

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    uint8_t row[PP_W];
    for (int i = 0; i < n; i++, out += PICO_W) {
        int sy = y + i - Y0;
        if (sy < 0 || sy >= PP_H) { pico_fill(out, PICO_W, 0); continue; }
        pp_render(&core, sy, sy + 1, row);
        pico_fill(out, X0, 0);
        for (int x = 0; x < PP_W; x++) out[X0 + x] = pal[row[x] & 127];
        pico_fill(out + X0 + PP_W, PICO_W - X0 - PP_W, 0);
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) pp_reset(&core);
        uint32_t k = pico_port_keys();
        pp_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & SP_COIN1;
        in.coin2 = k & SP_COIN2;
        in.left = k & SP_LEFT;
        in.right = k & SP_RIGHT;
        in.gas = k & (SP_UP | SP_B1);
        in.brake = k & (SP_DOWN | SP_B2);
        in.gear = k & SP_B3;
        pp_set_inputs(&core, &in);
        core.audio_n = 0;
        pp_run_frame(&core);
        pico_port_audio(abuf, core.audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    pp_roms roms;
    pp_rom_data(&roms);
    // the Z8002s' and the Z80's programs in RAM (the XIP cache is 16 KB)
    static uint8_t sub_ram[2][0x8000];
    memcpy(sub_ram[0], roms.sub1, 0x8000);
    memcpy(sub_ram[1], roms.sub2, 0x8000);
    roms.sub1 = sub_ram[0];
    roms.sub2 = sub_ram[1];
    uint8_t *z = malloc(roms.z80_size);
    if (z) { memcpy(z, roms.z80, roms.z80_size); roms.z80 = z; }
    core.audio = abuf;
    core.audio_cap = 1024;
    pp_init(&core, &roms, AUDIO_RATE);
    for (int i = 0; i < 128; i++) {
        uint32_t c = core.rgb[i];
        pal[i] = pico_rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
    pico_port_cfg cfg = { PP_TITLE, render, NULL, AUDIO_RATE, PP_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
