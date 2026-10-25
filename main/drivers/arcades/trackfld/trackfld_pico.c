// trackfld_pico.c - Track & Field (Konami, 1983) on the Waveshare
// RP2040-PiZero (HDMI). Same core as the CYD and the desktop.
//
// Keys: 5 coin, 1 start, C = RUN 1, Z = RUN 2 (alternate them), X = jump,
// F3 reset; USB pad: X / A run, B jump, SELECT coin, START start.
#include <string.h>
#include "pico_port.h"
#include "tf_core.h"

#define X0 ((PICO_W - TF_W) / 2)
#define Y0 ((PICO_H - TF_H) / 2)

static uint8_t fb[TF_W * TF_H];          // drawn by core 0 after each frame
static uint16_t pal[32];
static tf_video_t snap;
static int16_t audio[TF_SAMPLES_FRAME];

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        int sy = y + i - Y0;
        if (sy < 0 || sy >= TF_H) { pico_fill(out, PICO_W, 0); continue; }
        const uint8_t *src = fb + sy * TF_W;
        pico_fill(out, X0, 0);
        for (int x = 0; x < TF_W; x++) out[X0 + x] = pal[src[x] & 31];
        pico_fill(out + X0 + TF_W, PICO_W - X0 - TF_W, 0);
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) tf_reset();
        uint32_t k = pico_port_keys();
        uint8_t in0 = 0xff, in1 = 0xff;
        if (k & SP_COIN1) in0 &= (uint8_t)~TF_IN0_COIN1;
        if (k & SP_COIN2) in0 &= (uint8_t)~TF_IN0_COIN2;
        if (k & SP_START1) in0 &= (uint8_t)~TF_IN0_START1;
        if (k & SP_START2) in0 &= (uint8_t)~TF_IN0_START2;
        if (k & SP_B3) in1 &= (uint8_t)~TF_IN1_RUN1;
        if (k & SP_B1) in1 &= (uint8_t)~TF_IN1_RUN2;
        if (k & SP_B2) in1 &= (uint8_t)~TF_IN1_JUMP;
        tf_in.in0 = in0;
        tf_in.in1 = in1;
        tf_run_frame(audio);
        tf_snapshot(&snap);
        tf_render(&snap, fb);
        pico_port_audio(audio, TF_SAMPLES_FRAME);
        pico_port_frame_done();
    }
}

int main(void)
{
    uint8_t rgb[32][3];
    tf_init();
    tf_palette_rgb(rgb);
    for (int i = 0; i < 32; i++) pal[i] = pico_rgb565(rgb[i][0], rgb[i][1], rgb[i][2]);
    pico_port_cfg cfg = { "Track & Field", render, NULL, TF_AUDIO_RATE, TF_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
