// SDL2 desktop frontend for cMESYS Track & Field.
// Uses the shared common/tf_core (same core as ESP32 CYD).
// Controls match zxtiny/zxm/trackfld.c and the CYD serial keyboard:
//   C = RUN1, Z = RUN2, X = JUMP, 5 = coin, 1 = start, Esc = quit, F1 = turbo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>

#include "tf_core.h"

#define TF_SCALE 2
#define TF_AUDIO_SAMPLES 1024

static uint8_t fb[TF_W * TF_H];
static uint8_t pal[32][3];
static uint32_t pixels[TF_W * TF_H];
static uint32_t pal_argb[32];

static void rebuild_palette(void)
{
    tf_palette_rgb(pal);
    for (int i = 0; i < 32; i++) {
        pal_argb[i] = 0xff000000u |
                      ((uint32_t)pal[i][0] << 16) |
                      ((uint32_t)pal[i][1] << 8) |
                      (uint32_t)pal[i][2];
    }
}

static void fb_to_argb(void)
{
    for (int i = 0; i < TF_W * TF_H; i++)
        pixels[i] = pal_argb[fb[i] & 31];
}

static void set_active_low(volatile uint8_t *v, uint8_t mask, bool down)
{
    if (down) *v &= (uint8_t)~mask;
    else      *v |= mask;
}

static void handle_key(SDL_Scancode sc, bool down)
{
    switch (sc) {
        case SDL_SCANCODE_5: set_active_low(&tf_in.in0, TF_IN0_COIN1, down); break;
        case SDL_SCANCODE_6: set_active_low(&tf_in.in0, TF_IN0_COIN2, down); break;
        case SDL_SCANCODE_1: set_active_low(&tf_in.in0, TF_IN0_START1, down); break;
        case SDL_SCANCODE_2: set_active_low(&tf_in.in0, TF_IN0_START2, down); break;
        case SDL_SCANCODE_C: set_active_low(&tf_in.in1, TF_IN1_RUN1, down); break;
        case SDL_SCANCODE_Z: set_active_low(&tf_in.in1, TF_IN1_RUN2, down); break;
        case SDL_SCANCODE_X: set_active_low(&tf_in.in1, TF_IN1_JUMP, down); break;
        default: break;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "cMESYS Track & Field (SDL2)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        TF_W * TF_SCALE, TF_H * TF_SCALE, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, TF_W, TF_H);

    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL video: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, TF_W, TF_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    memset(&have, 0, sizeof have);
    want.freq = TF_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = TF_AUDIO_SAMPLES;
    want.callback = NULL;

    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev)
        SDL_PauseAudioDevice(audio_dev, 0);
    else
        fprintf(stderr, "SDL audio: %s (continuing without sound)\n", SDL_GetError());

    tf_init();
    rebuild_palette();

    tf_video_t video;
    int16_t audio[TF_SAMPLES_FRAME];
    bool quit = false;
    bool turbo = false;
    uint32_t frame = 0;
    uint32_t next_tick = SDL_GetTicks();

    printf("cMESYS Track & Field — SDL2\n");
    printf("Keys: C/Z run, X jump, 5 coin, 1 start, F1 turbo, Esc quit\n");

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                quit = true;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                    quit = true;
                else if (e.key.keysym.scancode == SDL_SCANCODE_F1)
                    turbo = !turbo;
                else
                    handle_key(e.key.keysym.scancode, true);
            } else if (e.type == SDL_KEYUP) {
                handle_key(e.key.keysym.scancode, false);
            }
        }

        tf_run_frame(audio);
        if (audio_dev) {
            // Cap queued audio to ~100 ms to avoid lag after turbo/focus stalls.
            while (SDL_GetQueuedAudioSize(audio_dev) > (Uint32)(TF_AUDIO_RATE / 10 * sizeof(int16_t)))
                SDL_Delay(1);
            SDL_QueueAudio(audio_dev, audio, (Uint32)(TF_SAMPLES_FRAME * sizeof(int16_t)));
        }

        tf_snapshot(&video);
        tf_render(&video, fb);
        fb_to_argb();
        SDL_UpdateTexture(texture, NULL, pixels, TF_W * (int)sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frame++;
        if (!turbo) {
            next_tick += 1000 / TF_FPS;
            uint32_t now = SDL_GetTicks();
            if ((int32_t)(next_tick - now) > 0)
                SDL_Delay(next_tick - now);
            else
                next_tick = now;
        }
    }

    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("%u frames\n", frame);
    return 0;
}
