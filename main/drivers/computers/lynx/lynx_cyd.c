// lynx_cyd.c - Atari Lynx on the ESP32 CYD (common/comp_cyd.h): the
// 160x102 LCD doubled to 320x204 in the middle of the panel.
//
// Cartridges from the SD card (hold MEDIA): .lnx .lyx .o
//
// Touch zones (MEDIA: hold = SD menu, tap = PAUSE):
//     MEDIA | OPTION 1 | OPTION 2 | HELP
//     LEFT  | UP       | DOWN     | RIGHT
//     PAUSE | B        | A        | A
// Nunchuk: the pad, Z = A, C = B. Serial console: cursors are not read;
// w a s d the pad, k = A, j = B, p = PAUSE, 1 / 2 the options.
#include "lynx_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#define TOP ((240 - LYNX_H * 2) / 2)

static lynx *cpu_m;
static char media_name[40];
static int pause_frames;
static unsigned typed;              // buttons held by the serial console's last character

static bool load(const char *name, const char **why)
{
    // A cartridge stays on the card behind a cache as big as the memory
    // takes (all of a 128 KB one, most of a 256 KB one): the game reads it
    // as it plays, and a small cache made every read a card access
    zx_pbuf pb;
    lynx_eject(cpu_m);                  // (the old one's memory first)
    const bool cart = comp_sd_ext(name, ".lnx") || comp_sd_ext(name, ".lyx");
    if (cart ? !comp_sd_pbuf_cached(name, &pb, why, 32768) : !comp_sd_pbuf(name, NULL, &pb, why)) return false;
    const bool ok = lynx_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : lynx_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { pause_frames = 6; }
static const char *item(int i) { return i == 0 ? "Reset" : NULL; }
static void item_do(int i) { if (i == 0) lynx_reset(cpu_m); }

// The typist's "keys" here are buttons (no keyboard on a Lynx)
static int char_keys(char c, int *k)
{
    switch (c) {
    case 'w': case 'W': k[0] = LYNX_UP; return 1;
    case 's': case 'S': k[0] = LYNX_DOWN; return 1;
    case 'a': case 'A': k[0] = LYNX_LEFT; return 1;
    case 'd': case 'D': k[0] = LYNX_RIGHT; return 1;
    case 'k': case 'K': case ' ': k[0] = LYNX_A; return 1;
    case 'j': case 'J': k[0] = LYNX_B; return 1;
    case 'p': case 'P': case '\r': k[0] = LYNX_PAUSE; return 1;
    case '1': k[0] = LYNX_OPT1; return 1;
    case '2': k[0] = LYNX_OPT2; return 1;
    default: return 0;
    }
}

static unsigned held;
static void key(int code, bool down) { if (down) typed |= (unsigned)code; }
static void keys_clear(void) { typed = 0; }

static void input(const comp_cyd_in *in)
{
    unsigned b = 0;
    if (in->zones & (1 << 1)) b |= LYNX_OPT1;
    if (in->zones & (1 << 2)) b |= LYNX_OPT2;
    if (in->zones & (1 << 4) || in->left) b |= LYNX_LEFT;
    if (in->zones & (1 << 5) || in->up) b |= LYNX_UP;
    if (in->zones & (1 << 6) || in->down) b |= LYNX_DOWN;
    if (in->zones & (1 << 7) || in->right) b |= LYNX_RIGHT;
    if (in->zones & (1 << 8)) b |= LYNX_PAUSE;
    if (in->zones & (1 << 9) || in->fire2) b |= LYNX_B;
    if (in->zones & ((1 << 10) | (1 << 11)) || in->fire) b |= LYNX_A;
    if (pause_frames > 0) { pause_frames--; b |= LYNX_PAUSE; }
    held = b;
    typed = 0;
}

// (the typist runs after input(): its buttons are added in frame())
static int frame(const int16_t **audio)
{
    lynx_buttons(cpu_m, held | typed);
    lynx_set_turbo(cpu_m, cyd_turbo);
    return lynx_run_frame(cpu_m, audio);
}

static void render(int y, uint16_t *row)
{
    const int ly = (y - TOP) >> 1;
    if (y < TOP || ly >= LYNX_H) {
        for (int x = 0; x < 320; x++) row[x] = 0;
        return;
    }
    uint16_t src[LYNX_W];
    lynx_render_row565(cpu_m, ly, src);
    for (int x = 0; x < LYNX_W; x++) row[2 * x] = row[2 * x + 1] = src[x];
}

static void colours(void) { lynx_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = LYNX_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "lynx", "lynx", "ATARI LYNX", "MEDIA: PAUSE / HOLD=SD", { 255, 140, 40 },
    kExts, LYNX_MEDIA_LABEL, LYNX_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "OPTION 1", "OPTION 2", NULL, "LEFT", "UP", "DOWN", "RIGHT", "PAUSE", "B", "A", "A" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "lynx", "ATARI LYNX", "MEDIA: PAUSE / HOLD=SD", { 255, 140, 40 } };
    cyd_boot(&info);
    cpu_m = lynx_create(LYNX_AUDIO_RATE);
    if (!cpu_m) {
        ESP_LOGE("lynx", "out of memory for the Lynx");
        abort();
    }
    if (lynx_rom_boot) lynx_set_boot_rom(cpu_m, lynx_rom_boot);
    comp_cyd_start(&kMachine);
}
