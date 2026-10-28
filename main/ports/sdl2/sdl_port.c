// sdl_port.c - common desktop (SDL2) frontend of the arcade drivers.
#include "sdl_port.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID dev;
static sdl_port_cfg cfg;
static uint32_t keys;
static bool paused;
static uint64_t next_tick, frame_ticks;
static long frames, quit_after = -1, shot_at = -1;
static bool held[SDL_NUM_SCANCODES];
static int presses[32], np;
static char *drops[8];
static int ndrops;

bool sdl_port_open(const sdl_port_cfg *c)
{
    cfg = *c;
    if (SDL_Init(SDL_INIT_VIDEO | (cfg.audio_rate ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    win = SDL_CreateWindow(cfg.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           cfg.width * cfg.scale, cfg.height * cfg.scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, cfg.width, cfg.height);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cfg.width, cfg.height);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);   // 0xRRGGBB, no alpha
    if (!win || !ren || !tex) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    if (cfg.audio_rate) {
        SDL_AudioSpec want = {0}, have;
        want.freq = cfg.audio_rate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev) SDL_PauseAudioDevice(dev, 0);
    }
    frame_ticks = (uint64_t)(SDL_GetPerformanceFrequency() / cfg.fps);
    next_tick = SDL_GetPerformanceCounter();
    const char *e = getenv("SDL_PORT_FRAMES");
    if (e) quit_after = atol(e);
    e = getenv("SDL_PORT_SHOT");
    if (e) shot_at = atol(e);
    return true;
}

void sdl_port_close(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

static uint32_t key_bit(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_5: return SP_COIN1;
    case SDL_SCANCODE_6: return SP_COIN2;
    case SDL_SCANCODE_1: return SP_START1;
    case SDL_SCANCODE_2: return SP_START2;
    case SDL_SCANCODE_UP: return SP_UP;
    case SDL_SCANCODE_DOWN: return SP_DOWN;
    case SDL_SCANCODE_LEFT: return SP_LEFT;
    case SDL_SCANCODE_RIGHT: return SP_RIGHT;
    case SDL_SCANCODE_Z: case SDL_SCANCODE_LCTRL: return SP_B1;
    case SDL_SCANCODE_X: case SDL_SCANCODE_LALT: return SP_B2;
    case SDL_SCANCODE_C: case SDL_SCANCODE_SPACE: return SP_B3;
    default: return 0;
    }
}

void sdl_port_queue_drop(const char *path)
{
    if (!path || ndrops >= 8) return;
    size_t n = 0;
    while (path[n]) n++;
    char *c = (char *)malloc(n + 1);
    if (!c) return;
    for (size_t i = 0; i <= n; i++) c[i] = path[i];
    drops[ndrops++] = c;
}

char *sdl_port_next_drop(void)
{
    if (!ndrops) return NULL;
    char *p = drops[0];
    for (int i = 1; i < ndrops; i++) drops[i - 1] = drops[i];
    ndrops--;
    return p;
}

bool sdl_port_poll(bool *reset)
{
    SDL_Event e;
    if (reset) *reset = false;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_DROPFILE) {
            if (e.drop.file) {
                sdl_port_queue_drop(e.drop.file);
                SDL_free(e.drop.file);
            }
            continue;
        }
        if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) continue;
        bool dn = e.type == SDL_KEYDOWN;
        SDL_Scancode sc = e.key.keysym.scancode;
        if (cfg.computer) {
            if ((unsigned)sc < SDL_NUM_SCANCODES) held[sc] = dn;
            if (dn && !e.key.repeat && np < 32) presses[np++] = sc;
            continue;
        }
        if (sc == SDL_SCANCODE_ESCAPE && dn) return false;
        if (sc == SDL_SCANCODE_F3 && dn && reset) *reset = true;
        if (sc == SDL_SCANCODE_P && dn && !e.key.repeat) paused = !paused;
        uint32_t b = key_bit(sc);
        if (dn) keys |= b; else keys &= ~b;
    }
    return quit_after < 0 || frames < quit_after;
}

uint32_t sdl_port_keys(void) { return keys; }
bool sdl_port_key(int sc) { return (unsigned)sc < SDL_NUM_SCANCODES && held[sc]; }
int sdl_port_next_press(void)
{
    if (!np) return -1;
    int k = presses[0];
    for (int i = 1; i < np; i++) presses[i - 1] = presses[i];
    np--;
    return k;
}
void sdl_port_set_title(const char *t) { if (win) SDL_SetWindowTitle(win, t); }
bool sdl_port_paused(void) { return paused; }

void sdl_port_audio(const int16_t *s, int n)
{
    // Keep about 3 frames queued: drop when far ahead (the video pacing rules).
    if (!dev || n <= 0) return;
    if (SDL_GetQueuedAudioSize(dev) < (Uint32)(cfg.audio_rate / 10) * 2) SDL_QueueAudio(dev, s, (Uint32)n * 2);
}

static void save_shot(const uint32_t *rgb)
{
    FILE *f = fopen("shot.ppm", "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", cfg.width, cfg.height);
    for (int i = 0; i < cfg.width * cfg.height; i++) {
        fputc((rgb[i] >> 16) & 255, f);
        fputc((rgb[i] >> 8) & 255, f);
        fputc(rgb[i] & 255, f);
    }
    fclose(f);
}

void sdl_port_present(const uint32_t *rgb)
{
    frames++;
    if (frames == shot_at) save_shot(rgb);
    SDL_UpdateTexture(tex, NULL, rgb, cfg.width * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (quit_after >= 0) return;        // tests run flat out
    next_tick += frame_ticks;
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < next_tick) SDL_Delay((Uint32)((next_tick - now) * 1000 / SDL_GetPerformanceFrequency()));
    else next_tick = now;
}
