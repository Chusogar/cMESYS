// sdl_port.c - Emscripten/Web frontend (SDL2 via USE_SDL=2).
// Same behaviour as ports/sdl2/sdl_port.c; frame pacing uses emscripten_sleep
// so the existing while (sdl_port_poll()) loops in *_sdl.c keep the browser
// responsive (link with -sASYNCIFY).
#include "sdl_port.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID dev;
static sdl_port_cfg cfg;
static uint32_t keys;
static bool paused, turbo;
static unsigned turbo_count;
static uint64_t next_tick, frame_ticks;
static long frames, quit_after = -1;
static bool held[SDL_NUM_SCANCODES];
static int presses[32], np;
static char *drops[8];
static int ndrops;

static long url_long(const char *key, long def)
{
#ifdef __EMSCRIPTEN__
    // Read ?key=value from the page URL (optional; env still works in node).
    int n = (int)strlen(key);
    char js[160];
    snprintf(js, sizeof js,
             "(function(){var q=new URLSearchParams(location.search).get('%s');"
             "return q===null?-1:parseInt(q,10)|0;})()", key);
    (void)n;
    int v = emscripten_run_script_int(js);
    return v < 0 ? def : (long)v;
#else
    (void)key;
    return def;
#endif
}

// ---------------------------------------------------------------------------
// --mute / --sound / --scale N
// ---------------------------------------------------------------------------
static int opt_mute = -1;       // -1: as the driver says
static int opt_scale;           // 0: as the driver says

void sdl_port_args(int *argc, char **argv)
{
    if (!argc || !argv) return;
    int out = 1;
    for (int i = 1; i < *argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--mute") || !strcmp(a, "-m")) { opt_mute = 1; continue; }
        if (!strcmp(a, "--sound") || !strcmp(a, "--unmute")) { opt_mute = 0; continue; }
        if (!strncmp(a, "--scale=", 8) || !strcmp(a, "--scale") || !strcmp(a, "-s")) {
            const char *v = a[7] == '=' ? a + 8 : (i + 1 < *argc ? argv[++i] : "");
            int n = atoi(v);
            if (n < 1 || n > 10) {
                fprintf(stderr, "--scale: a number from 1 to 10 (not '%s')\n", v);
                exit(2);
            }
            opt_scale = n;
            continue;
        }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            printf("Options: --mute (no sound), --sound, --scale N (window = picture x N, 1-10)\n");
            exit(0);
        }
        argv[out++] = argv[i];
    }
    argv[out] = NULL;
    *argc = out;
}

bool sdl_port_open(const sdl_port_cfg *c)
{
    cfg = *c;
    if (opt_mute == 1) cfg.audio_rate = 0;
    if (opt_scale) cfg.scale = opt_scale;
    if (SDL_Init(SDL_INIT_VIDEO | (cfg.audio_rate ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    win = SDL_CreateWindow(cfg.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           cfg.width * cfg.scale, cfg.height * cfg.scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, cfg.width, cfg.height);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cfg.width, cfg.height);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
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
    if (getenv("SDL_PORT_VERBOSE"))
        printf("sdl_port: window %dx%d (scale %d), sound %s\n", cfg.width * cfg.scale, cfg.height * cfg.scale,
               cfg.scale, dev ? "on" : "off");
    frame_ticks = (uint64_t)(SDL_GetPerformanceFrequency() / cfg.fps);
    next_tick = SDL_GetPerformanceCounter();
    const char *e = getenv("SDL_PORT_FRAMES");
    if (e) quit_after = atol(e);
    else quit_after = url_long("frames", -1);
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
    char *c = (char *)malloc(strlen(path) + 1);
    if (!c) return;
    memcpy(c, path, strlen(path) + 1);
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
void sdl_port_set_title(const char *t)
{
    if (win) SDL_SetWindowTitle(win, t);
#ifdef __EMSCRIPTEN__
    if (t) EM_ASM({ document.title = UTF8ToString($0); }, t);
#endif
}
bool sdl_port_paused(void) { return paused; }

void sdl_port_audio(const int16_t *s, int n)
{
    if (!dev || n <= 0 || turbo) return;
    if (SDL_GetQueuedAudioSize(dev) < (Uint32)(cfg.audio_rate / 10) * 2)
        SDL_QueueAudio(dev, s, (Uint32)n * 2);
}

void sdl_port_set_turbo(bool on)
{
    turbo = on;
    turbo_count = 0;
    if (turbo && dev) SDL_ClearQueuedAudio(dev);
    next_tick = SDL_GetPerformanceCounter();
}

bool sdl_port_turbo(void) { return turbo; }

void sdl_port_present(const uint32_t *rgb)
{
    frames++;
    if (turbo && (++turbo_count & 3) != 0 && quit_after < 0) {
#ifdef __EMSCRIPTEN__
        if ((turbo_count & 15) == 1) emscripten_sleep(0);     // let the browser breathe
#endif
        return;
    }
    SDL_UpdateTexture(tex, NULL, rgb, cfg.width * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (quit_after >= 0) return;
    if (turbo) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0);
#endif
        next_tick = SDL_GetPerformanceCounter();
        return;
    }
    next_tick += frame_ticks;
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < next_tick) {
        uint32_t ms = (uint32_t)((next_tick - now) * 1000 / SDL_GetPerformanceFrequency());
        if (ms > 0) {
#ifdef __EMSCRIPTEN__
            emscripten_sleep(ms);       // yields to the browser (needs ASYNCIFY)
#else
            SDL_Delay(ms);
#endif
        }
    } else next_tick = now;
}
