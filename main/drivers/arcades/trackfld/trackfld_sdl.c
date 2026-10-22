// trackfld_sdl.c - Track & Field on the desktop (SDL2), same core as the CYD.
// ROMs are compiled in (rom_data.c, made by tools/gen_roms_trackfld.c).
// Keys: C = run 1, Z = run 2 (alternate them to run), X = jump.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "tf_core.h"

int main(void)
{
    static uint8_t fb[TF_W * TF_H];
    static uint32_t rgb[TF_W * TF_H];
    static int16_t audio[TF_SAMPLES_FRAME];
    static tf_video_t v;
    uint8_t pal[32][3];
    uint32_t pal32[32];

    tf_init();
    tf_palette_rgb(pal);
    for (int i = 0; i < 32; i++) pal32[i] = ((uint32_t)pal[i][0] << 16) | ((uint32_t)pal[i][1] << 8) | pal[i][2];
    sdl_port_cfg cfg = {"Track & Field", TF_W, TF_H, 3, TF_AUDIO_RATE, TF_FPS};
    if (!sdl_port_open(&cfg)) return 1;

    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) tf_reset();
        if (sdl_port_paused()) { sdl_port_present(rgb); continue; }
        uint32_t k = sdl_port_keys();
        uint8_t in0 = 0xff, in1 = 0xff;
        if (k & SP_COIN1) in0 &= (uint8_t)~TF_IN0_COIN1;
        if (k & SP_COIN2) in0 &= (uint8_t)~TF_IN0_COIN2;
        if (k & SP_START1) in0 &= (uint8_t)~TF_IN0_START1;
        if (k & SP_START2) in0 &= (uint8_t)~TF_IN0_START2;
        if (k & SP_B3) in1 &= (uint8_t)~TF_IN1_RUN1;    // C
        if (k & SP_B1) in1 &= (uint8_t)~TF_IN1_RUN2;    // Z
        if (k & SP_B2) in1 &= (uint8_t)~TF_IN1_JUMP;    // X
        tf_in.in0 = in0;
        tf_in.in1 = in1;
        tf_run_frame(audio);
        sdl_port_audio(audio, TF_SAMPLES_FRAME);
        tf_snapshot(&v);
        tf_render(&v, fb);
        for (int i = 0; i < TF_W * TF_H; i++) rgb[i] = pal32[fb[i] & 31];
        sdl_port_present(rgb);
    }
    sdl_port_close();
    return 0;
}
