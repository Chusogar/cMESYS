// a2600_cyd.c - Atari 2600 / VCS on the ESP32 CYD (common/comp_cyd.h): the
// 160x192 picture doubled across to 320x192, in the middle of the panel.
//
// Cartridges from the SD card (hold MEDIA): .a26 .bin .rom
//
// Touch zones (MEDIA: hold = SD menu, tap = GAME SELECT):
//     MEDIA | RESET | SELECT | HELP
//     LEFT  | UP    | DOWN   | RIGHT
//     FIRE  | FIRE  | FIRE   | FIRE
// The menu also has the console's switches (colour / B&W, difficulties).
// Nunchuk: joystick 1, Z or C = FIRE. Serial console: w a s d the stick,
// k or space FIRE, r GAME RESET, e GAME SELECT.
#include "a2600_core.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#define TOP ((240 - A2600_H) / 2)

static a2600 *cpu_m;
static char media_name[40];
static int select_frames;
static unsigned held, typed;

static bool load(const char *name, const char **why)
{
    zx_pbuf pb;
    a2600_eject(cpu_m);
    if (!comp_sd_pbuf(name, NULL, &pb, why)) return false;
    const bool ok = a2600_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : a2600_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { select_frames = 6; }

static const char *item(int i)
{
    const uint8_t sw = a2600_switches(cpu_m);
    switch (i) {
    case 0: return "Power cycle";
    case 1: return (sw & A2600_SW_COLOUR) ? "TV: colour -> B&W" : "TV: B&W -> colour";
    case 2: return (sw & A2600_SW_DIFF_LEFT_A) ? "Left difficulty: A -> B" : "Left difficulty: B -> A";
    case 3: return (sw & A2600_SW_DIFF_RIGHT_A) ? "Right difficulty: A -> B" : "Right difficulty: B -> A";
    default: return NULL;
    }
}

static void item_do(int i)
{
    const uint8_t sw = a2600_switches(cpu_m);
    if (i == 0) a2600_reset(cpu_m);
    if (i == 1) a2600_set_switches(cpu_m, sw ^ A2600_SW_COLOUR);
    if (i == 2) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_LEFT_A);
    if (i == 3) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_RIGHT_A);
}

static int char_keys(char c, int *k)
{
    switch (c) {
    case 'w': case 'W': k[0] = A2600_UP; return 1;
    case 's': case 'S': k[0] = A2600_DOWN; return 1;
    case 'a': case 'A': k[0] = A2600_LEFT; return 1;
    case 'd': case 'D': k[0] = A2600_RIGHT; return 1;
    case 'k': case 'K': case ' ': k[0] = A2600_FIRE; return 1;
    case 'r': case 'R': case '\r': k[0] = A2600_RESET; return 1;
    case 'e': case 'E': k[0] = A2600_SELECT; return 1;
    default: return 0;
    }
}

static void key(int code, bool down) { if (down) typed |= (unsigned)code; }
static void keys_clear(void) { typed = 0; }

static void input(const comp_cyd_in *in)
{
    unsigned b = 0;
    if (in->zones & (1 << 1)) b |= A2600_RESET;
    if (in->zones & (1 << 2)) b |= A2600_SELECT;
    if (in->zones & (1 << 4) || in->left) b |= A2600_LEFT;
    if (in->zones & (1 << 5) || in->up) b |= A2600_UP;
    if (in->zones & (1 << 6) || in->down) b |= A2600_DOWN;
    if (in->zones & (1 << 7) || in->right) b |= A2600_RIGHT;
    if (in->zones & 0xf00 || in->fire || in->fire2) b |= A2600_FIRE;
    if (select_frames > 0) { select_frames--; b |= A2600_SELECT; }
    held = b;
    typed = 0;
}

static int frame(const int16_t **audio)
{
    a2600_input(cpu_m, held | typed);
    a2600_set_turbo(cpu_m, cyd_turbo);
    return a2600_run_frame(cpu_m, audio);
}

static void render(int y, uint16_t *row)
{
    const int ly = y - TOP;
    if (ly < 0 || ly >= A2600_H) {
        for (int x = 0; x < 320; x++) row[x] = 0;
        return;
    }
    uint16_t src[A2600_W];
    a2600_render_row565(cpu_m, ly, src);
    for (int x = 0; x < A2600_W; x++) row[2 * x] = row[2 * x + 1] = src[x];
}

static void colours(void) { a2600_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = A2600_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "a2600", "a2600", "ATARI 2600", "MEDIA: SELECT / HOLD=SD", { 230, 120, 40 },
    kExts, A2600_MEDIA_LABEL, A2600_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "RESET", "SELECT", NULL, "LEFT", "UP", "DOWN", "RIGHT", "FIRE", "FIRE", "FIRE", "FIRE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "a2600", "ATARI 2600", "MEDIA: SELECT / HOLD=SD", { 230, 120, 40 } };
    cyd_boot(&info);
    cpu_m = a2600_create(A2600_AUDIO_RATE);
    if (!cpu_m) {
        ESP_LOGE("a2600", "out of memory for the 2600");
        abort();
    }
    comp_cyd_start(&kMachine);
}
