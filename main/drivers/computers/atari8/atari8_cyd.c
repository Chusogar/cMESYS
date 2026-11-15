// atari8_cyd.c - Atari 800XL on the ESP32 CYD (common/comp_cyd.h): the
// middle 320 of the 384 columns (the playfield), all 240 lines. The ROMs
// built in (roms/atari800xl).
//
// Disks from the SD card (hold MEDIA): .atr (the machine boots them). The
// serial console is the keyboard (what is typed is typed into the Atari).
//
// Touch zones (MEDIA: hold = SD menu, tap = RETURN):
//     MEDIA | START | OPTION | HELP
//     LEFT  | UP    | DOWN   | RIGHT     (joystick 1)
//     FIRE  | SPACE | SELECT | FIRE
// Nunchuk: joystick 1, Z or C = FIRE.
#include "atari8_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#define X0 ((A8_W - 320) / 2)
#define K_SHIFT 64                  // the typist's SHIFT (not an Atari scan code)

static atari8 *cpu_m;
static char media_name[40];
static int return_frames;
static unsigned joy, con;
static int zone_key, typed_key;
static bool typed_shift;

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".atr", NULL };
    zx_pbuf pb;
    atari8_eject(cpu_m);
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = atari8_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : atari8_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { return_frames = 4; }

static const char *item(int i)
{
    switch (i) {
    case 0: return "Eject the disk (BASIC)";
    case 1: return "Cold start";
    case 2: return "BREAK";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        atari8_eject(cpu_m);
        media_name[0] = 0;
        atari8_reset(cpu_m);
    }
    if (i == 1) atari8_reset(cpu_m);
    if (i == 2) atari8_break(cpu_m);
}

static int char_keys(char c, int *k)
{
    bool shift;
    const int code = atari8_char_key(c, &shift);
    if (code < 0) return 0;
    k[0] = code;
    if (!shift) return 1;
    k[1] = K_SHIFT;
    return 2;
}

static void key(int code, bool down)
{
    if (!down) return;
    if (code == K_SHIFT) typed_shift = true;
    else typed_key = code;
}

static void keys_clear(void)
{
    typed_key = -1;
    typed_shift = false;
}

static void input(const comp_cyd_in *in)
{
    joy = con = 0;
    zone_key = -1;
    if (in->zones & (1 << 1)) con |= A8_START;
    if (in->zones & (1 << 2)) con |= A8_OPTION;
    if (in->zones & (1 << 10)) con |= A8_SELECT;
    if (in->zones & (1 << 4) || in->left) joy |= A8_LEFT;
    if (in->zones & (1 << 5) || in->up) joy |= A8_UP;
    if (in->zones & (1 << 6) || in->down) joy |= A8_DOWN;
    if (in->zones & (1 << 7) || in->right) joy |= A8_RIGHT;
    if (in->zones & ((1 << 8) | (1 << 11)) || in->fire || in->fire2) joy |= A8_FIRE;
    if (in->zones & (1 << 9)) zone_key = A8K_SPACE;
    if (return_frames > 0) {
        return_frames--;
        zone_key = A8K_RETURN;
    }
    keys_clear();
}

static int frame(const int16_t **audio)
{
    const int code = typed_key >= 0 ? typed_key : zone_key;
    atari8_set_key(cpu_m, code, typed_key >= 0 && typed_shift, false);
    atari8_joy(cpu_m, 0, joy);
    atari8_console(cpu_m, con);
    atari8_set_turbo(cpu_m, cyd_turbo);
    return atari8_run_frame(cpu_m, audio);
}

static void render(int y, uint16_t *row) { atari8_render_row565(cpu_m, y, X0, 320, row); }

static void colours(void) { atari8_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = A8_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "atari8", "atari8", "ATARI 800XL", "MEDIA: RETURN / HOLD=SD", { 230, 140, 60 },
    kExts, A8_MEDIA_LABEL, A8_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "START", "OPTION", NULL, "LEFT", "UP", "DOWN", "RIGHT", "FIRE", "SPACE", "SELECT", "FIRE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "atari8", "ATARI 800XL", "MEDIA: RETURN / HOLD=SD", { 230, 140, 60 } };
    cyd_boot(&info);
    cpu_m = atari8_rom_os ? atari8_create(A8_MODEL_XL, A8_AUDIO_RATE, atari8_rom_os, 0x4000, atari8_rom_basic) : NULL;
    if (!cpu_m) {
        ESP_LOGE("atari8", "%s", atari8_rom_os ? "out of memory for the Atari" : "no Atari OS ROM built in (roms/atari800xl/)");
        abort();
    }
    typed_key = -1;
    comp_cyd_start(&kMachine);
}
