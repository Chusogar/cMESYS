// msx_cyd.c - MSX1 / MSX2 on the ESP32 CYD (common/comp_cyd.h). Built as
// msx1 or msx2 (MSX_MODEL). The MSX2 has 64 KB of mapper RAM and 64 KB of
// VRAM here (the ESP32's memory; the BIOS takes the smaller chip as such).
//
// Media from the SD card (hold MEDIA): .rom .dsk .cas .tsx. A tape runs by
// itself (CLOAD / RUN"CAS:" / BLOAD"CAS:",R are typed for it). The serial
// console is the keyboard.
//
// Touch zones (MEDIA: hold = SD menu, tap = SPACE):
//     MEDIA | F1     | STOP  | HELP
//     LEFT  | UP     | DOWN  | RIGHT     (the cursor keys and joystick 1)
//     SPACE | RETURN | ESC   | FIRE
// Nunchuk: joystick 1, Z trigger A, C trigger B.
#include "msx_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#ifndef MSX_MODEL
#define MSX_MODEL 1
#endif

static msx *cpu_m;
static char media_name[40];
static int space_frames;

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".dsk", NULL };
    msx_drop_media_for(cpu_m, name);    // (the old cartridge out first: no room for two)
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) {
        // a cartridge bigger than the free memory: read from the card as it
        // is used, through as much cache as there is room for (16 KB at most)
        static const char *const all[] = { ".rom", ".mx1", ".mx2", NULL };
        bool ok = false;
        for (int slots = 64; slots >= 8 && !ok; slots /= 2) {
            comp_sd_lazy_slots = slots;
            ok = comp_sd_pbuf(name, all, &pb, why);
        }
        comp_sd_lazy_slots = 8;
        if (!ok) return false;
    }
    const bool ok = msx_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : msx_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { space_frames = 4; }

static const char *item(int i)
{
    switch (i) {
    case 0: return "Eject the cartridge";
    case 1: return "Reset";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        msx_eject_cart(cpu_m);
        media_name[0] = 0;
    } else {
        msx_reset(cpu_m);
    }
}

static int char_keys(char c, int *k)
{
    static const int kLetters[26] = {
        MSXK_A, MSXK_B, MSXK_C, MSXK_D, MSXK_E, MSXK_F, MSXK_G, MSXK_H, MSXK_I, MSXK_J, MSXK_K, MSXK_L, MSXK_M,
        MSXK_N, MSXK_O, MSXK_P, MSXK_Q, MSXK_R, MSXK_S, MSXK_T, MSXK_U, MSXK_V, MSXK_W, MSXK_X, MSXK_Y, MSXK_Z,
    };
    static const int kDigits[10] = { MSXK_0, MSXK_1, MSXK_2, MSXK_3, MSXK_4, MSXK_5, MSXK_6, MSXK_7, MSXK_8, MSXK_9 };
    static const char kShifted[] = ")!@#$%^&*(";               // Shift + 0..9 (international)
    if (c >= 'a' && c <= 'z') { k[0] = kLetters[c - 'a']; return 1; }
    if (c >= 'A' && c <= 'Z') { k[0] = MSXK_SHIFT; k[1] = kLetters[c - 'A']; return 2; }
    if (c >= '0' && c <= '9') { k[0] = kDigits[c - '0']; return 1; }
    const char *s = c ? strchr(kShifted, c) : NULL;
    if (s) { k[0] = MSXK_SHIFT; k[1] = kDigits[s - kShifted]; return 2; }
    switch (c) {
    case ' ': k[0] = MSXK_SPACE; return 1;
    case '\r': k[0] = MSXK_RETURN; return 1;
    case 8: case 127: k[0] = MSXK_BS; return 1;
    case 27: k[0] = MSXK_ESC; return 1;
    case '-': k[0] = MSXK_MINUS; return 1;
    case '_': k[0] = MSXK_SHIFT; k[1] = MSXK_MINUS; return 2;
    case '=': k[0] = MSXK_EQUALS; return 1;
    case '+': k[0] = MSXK_SHIFT; k[1] = MSXK_EQUALS; return 2;
    case ';': k[0] = MSXK_SEMICOLON; return 1;
    case ':': k[0] = MSXK_SHIFT; k[1] = MSXK_SEMICOLON; return 2;
    case '\'': k[0] = MSXK_QUOTE; return 1;
    case '"': k[0] = MSXK_SHIFT; k[1] = MSXK_QUOTE; return 2;
    case ',': k[0] = MSXK_COMMA; return 1;
    case '<': k[0] = MSXK_SHIFT; k[1] = MSXK_COMMA; return 2;
    case '.': k[0] = MSXK_PERIOD; return 1;
    case '>': k[0] = MSXK_SHIFT; k[1] = MSXK_PERIOD; return 2;
    case '/': k[0] = MSXK_SLASH; return 1;
    case '?': k[0] = MSXK_SHIFT; k[1] = MSXK_SLASH; return 2;
    case '[': k[0] = MSXK_LBRACKET; return 1;
    case ']': k[0] = MSXK_RBRACKET; return 1;
    case '\\': k[0] = MSXK_BACKSLASH; return 1;
    default: return 0;
    }
}

static void key(int code, bool down) { msx_key(cpu_m, code, down); }
static void keys_clear(void) { msx_keys_clear(cpu_m); }

static void input(const comp_cyd_in *in)
{
    static const int kZone[12] = { -1, MSXK_F1, MSXK_STOP, -1, MSXK_LEFT, MSXK_UP, MSXK_DOWN, MSXK_RIGHT,
                                   MSXK_SPACE, MSXK_RETURN, MSXK_ESC, -1 };
    msx_keys_clear(cpu_m);
    for (int z = 0; z < 12; z++)
        if ((in->zones >> z) & 1 && kZone[z] >= 0) msx_key(cpu_m, kZone[z], true);
    if (space_frames > 0) {
        space_frames--;
        msx_key(cpu_m, MSXK_SPACE, true);
    }
    uint8_t joy = 0;
    if (in->zones & (1 << 4) || in->left) joy |= MSX_JOY_LEFT;
    if (in->zones & (1 << 5) || in->up) joy |= MSX_JOY_UP;
    if (in->zones & (1 << 6) || in->down) joy |= MSX_JOY_DOWN;
    if (in->zones & (1 << 7) || in->right) joy |= MSX_JOY_RIGHT;
    if (in->zones & (1 << 11) || in->fire) joy |= MSX_JOY_A;
    if (in->fire2) joy |= MSX_JOY_B;
    msx_joy(cpu_m, 1, joy);
    msx_joy(cpu_m, 2, 0);
}

static int frame(const int16_t **audio)
{
    msx_set_turbo(cpu_m, cyd_turbo);
    return msx_run_frame(cpu_m, audio);
}

static void render(int y, uint16_t *row) { msx_render_row565(cpu_m, y, row); }
static void colours(void) { msx_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = MSX_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "msx", MSX_MODEL == 2 ? "msx2" : "msx1", MSX_MODEL == 2 ? "MSX2" : "MSX", "MEDIA: SPACE / HOLD=SD",
    { 120, 160, 255 }, kExts, MSX_MEDIA_LABEL, MSX_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "F1", "STOP", NULL, "LEFT", "UP", "DOWN", "RIGHT", "SPACE", "RETURN", "ESC", "FIRE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { MSX_MODEL == 2 ? "msx2" : "msx1", MSX_MODEL == 2 ? "MSX2" : "MSX",
                                     "MEDIA: SPACE / HOLD=SD", { 120, 160, 255 } };
    cyd_boot(&info);
    cpu_m = msx_create(MSX_MODEL, 64, 64, MSX_AUDIO_RATE);
    if (!cpu_m || !msx_rom_bios || (MSX_MODEL == 2 && !msx_rom_sub)) {
        ESP_LOGE("msx", "%s", cpu_m ? "no MSX ROMs built in (roms/msx1/, roms/msx2/)" : "out of memory for the MSX");
        abort();
    }
    msx_set_roms(cpu_m, msx_rom_bios, msx_rom_sub, msx_rom_disk);
    comp_cyd_start(&kMachine);
}
