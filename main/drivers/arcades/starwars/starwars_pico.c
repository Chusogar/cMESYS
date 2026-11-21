// starwars_pico.c - Star Wars (Atari, 1983) and The Empire Strikes Back
// (Atari, 1985) on the Waveshare RP2040/RP2350-PiZero (HDMI): one firmware
// for both, the same core (sw_core.c) as the desktop. The vectors are drawn
// straight into an 8-bit 320x240 picture (colour and brightness, fading as
// a vector monitor's phosphor) that core 1 shows.
//
// The yoke: a USB mouse (left / right fire) or the cursors / the pad's stick.
// Keys: 5 coin, 1 start, Z / X / C fire, F3 reset, F4 the other game;
// pad: A / B / X fire, SELECT coin, START start.
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "sw_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define F4 0x3d                 // HID usage

static uint16_t pal[256];
static const uint8_t *volatile pic;
static volatile int pic_w, pic_h;
static int16_t audio[SW_AUDIO_MAX];

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    const uint8_t *p = pic;
    const int w = pic_w, h = pic_h, x0 = (PICO_W - w) / 2, y0 = (PICO_H - h) / 2;
    for (int i = 0; i < n; i++, out += PICO_W) {
        int sy = y + i - y0;
        if (!p || sy < 0 || sy >= h) { pico_fill(out, PICO_W, 0); continue; }
        const uint8_t *src = p + sy * w;
        pico_fill(out, x0, 0);
        for (int x = 0; x < w; x++) out[x0 + x] = pal[src[x]];
        pico_fill(out + x0 + w, PICO_W - x0 - w, 0);
    }
}

static bool start_game(int game)
{
    pic = NULL;
    if (!sw_init(game, PICO_W, PICO_H)) return false;
    sw_set_audio_rate(AUDIO_RATE);
    const uint32_t *c = sw_palette();
    for (int i = 0; i < 256; i++) pal[i] = pico_rgb565((uint8_t)(c[i] >> 16), (uint8_t)(c[i] >> 8), (uint8_t)c[i]);
    pic_w = sw_width();
    pic_h = sw_height();
    pic = sw_picture();
    return true;
}

static void game(void)
{
    int mx = 0x80 << 4, my = 0x80 << 4;
    for (;;) {
        if (pico_port_poll()) sw_reset();
        bool f4_now = false;
        for (int u; (u = pico_port_next_press()) >= 0;)
            if (u == F4) f4_now = true;
        if (f4_now) {
            int other = sw_game() == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB;
            if (!start_game(other)) start_game(other == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB);
            pico_osd_toast(sw_title(), 2000);
        }
        uint32_t k = pico_port_keys();
        sw_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = (k & SP_COIN1) != 0;
        in.coin2 = (k & SP_COIN2) != 0;
        in.start = (k & (SP_START1 | SP_START2)) != 0;
        in.fire1 = (k & SP_B1) != 0;
        in.fire2 = (k & SP_B2) != 0;
        in.fire3 = (k & SP_B3) != 0;
        int dx, dy;
        uint32_t mb = pico_port_mouse(&dx, &dy);
        mx += dx * 24;
        my += dy * 24;
        mx = mx < 0 ? 0 : mx > (255 << 4) ? 255 << 4 : mx;
        my = my < 0 ? 0 : my > (255 << 4) ? 255 << 4 : my;
        if (mb & 4) mx = my = 0x80 << 4;        // middle button: the yoke centred
        in.x = (uint8_t)(mx >> 4);
        in.y = (uint8_t)(my >> 4);
        if (mb & 1) in.fire1 = true;
        if (mb & 2) in.fire2 = true;
        if (k & SP_UP) in.y = 0x10;
        if (k & SP_DOWN) in.y = 0xf0;
        if (k & SP_LEFT) in.x = 0x10;
        if (k & SP_RIGHT) in.x = 0xf0;
        sw_set_inputs(&in);
        int n = sw_run_frame(audio);
        pico_port_audio(audio, n);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();
    if (!start_game(SW_GAME_STARWARS) && !start_game(SW_GAME_ESB)) pico_port_rom_error();
    pico_port_cfg cfg = {"Star Wars / The Empire Strikes Back", render, NULL, AUDIO_RATE, SW_FPS, false};
    pico_port_open(&cfg);
    pico_port_run(game);
}
