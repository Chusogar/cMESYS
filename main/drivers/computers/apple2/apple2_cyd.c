// apple2_cyd.c - Apple ][, ][+, //e and //e Enhanced on the ESP32 CYD
// (common/comp_cyd.h). Built as apple2, apple2p, apple2e or apple2ee
// (A2_MODEL). The 560 x 192 picture is drawn 320 x 240 (7 dots in 4
// pixels, every fourth line twice).
//
// Media from the SD card (hold MEDIA): .dsk .do .po .nib into drive 1, which
// boots it (read from the card as it is used: what the Apple writes is not
// kept). The serial console is the keyboard.
//
// Touch zones (MEDIA: hold = SD menu, tap = CTRL-RESET):
//     MEDIA | Y     | N      | HELP
//     LEFT  | UP    | DOWN   | RIGHT     (the arrow keys and the joystick)
//     ESC   | SPACE | RETURN | BUTTON 0
// Nunchuk: the joystick, Z button 0, C button 1.
#include "apple2_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#ifndef A2_MODEL
#define A2_MODEL A2_IIEE
#endif

static apple2 *cpu_m;
static char media_name[40];

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".dsk", ".do", ".po", ".nib", NULL };
    if (!apple2_has_disk_card(cpu_m)) {
        *why = "no Disk II PROM (341-0027)";
        return false;
    }
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const char *err = apple2_load_pbuf(cpu_m, &pb, name);
    *why = err ? err : "";
    if (err) return false;
    snprintf(media_name, sizeof media_name, "%s", name);
    apple2_reset(cpu_m);                // (the ROM boots the disk at power on)
    return true;
}

static void tap(void)
{
    apple2_reset_key(cpu_m);
    cyd_show_toast("CTRL-RESET");
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "Type CATALOG";
    case 1: return "Type PR#6 (boot the disk)";
    case 2: return "Eject the disk";
    case 3: return "CTRL-RESET";
    case 4: return "Power off and on";
    default: return NULL;
    }
}

static void item_do(int i)
{
    switch (i) {
    case 0: cyd_type("CATALOG\r"); break;
    case 1: cyd_type("PR#6\r"); break;
    case 2: apple2_eject(cpu_m); media_name[0] = 0; break;
    case 3: apple2_reset_key(cpu_m); break;
    default: apple2_reset(cpu_m); break;
    }
}

// The keys for a character: the ][s and the //e put the symbols on different keys
static int char_keys(char c, int *k)
{
    const bool iie = A2_MODEL >= A2_IIE;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { k[0] = c; return 1; }
    static const char kShiftII[] = "!\"#$%&'()";               // ][: Shift + 1..9
    static const char kShiftIIe[] = "!@#$%^&*(";               // //e: Shift + 1..9
    const char *s = strchr(iie ? kShiftIIe : kShiftII, c);
    if (s && c) { k[0] = A2K_SHIFT; k[1] = '1' + (int)(s - (iie ? kShiftIIe : kShiftII)); return 2; }
    switch (c) {
    case ' ': k[0] = A2K_SPACE; return 1;
    case '\r': k[0] = A2K_RETURN; return 1;
    case 8: k[0] = A2K_LEFT; return 1;
    case 127: k[0] = iie ? A2K_DELETE : A2K_LEFT; return 1;
    case 27: k[0] = A2K_ESC; return 1;
    case ',': k[0] = A2K_COMMA; return 1;
    case '.': k[0] = A2K_PERIOD; return 1;
    case '/': k[0] = A2K_SLASH; return 1;
    case '<': k[0] = A2K_SHIFT; k[1] = A2K_COMMA; return 2;
    case '>': k[0] = A2K_SHIFT; k[1] = A2K_PERIOD; return 2;
    case '?': k[0] = A2K_SHIFT; k[1] = A2K_SLASH; return 2;
    case '-': k[0] = A2K_MINUS; return 1;
    case ';': k[0] = A2K_SEMICOLON; return 1;
    case '"': if (iie) { k[0] = A2K_SHIFT; k[1] = A2K_QUOTE; return 2; } return 0;
    case '\'': if (iie) { k[0] = A2K_QUOTE; return 1; } return 0;
    case ':': if (iie) { k[0] = A2K_SHIFT; k[1] = A2K_SEMICOLON; return 2; } k[0] = A2K_COLON; return 1;
    case '*': if (iie) { k[0] = A2K_SHIFT; k[1] = '8'; return 2; } k[0] = A2K_SHIFT; k[1] = A2K_COLON; return 2;
    case '+': if (iie) { k[0] = A2K_SHIFT; k[1] = A2K_EQUALS; return 2; } k[0] = A2K_SHIFT; k[1] = A2K_SEMICOLON; return 2;
    case '=': if (iie) { k[0] = A2K_EQUALS; return 1; } k[0] = A2K_SHIFT; k[1] = A2K_MINUS; return 2;
    default: return 0;
    }
}

static void key(int code, bool down) { apple2_key(cpu_m, code, down); }
static void keys_clear(void) { apple2_keys_clear(cpu_m); }

static void input(const comp_cyd_in *in)
{
    static const int kZone[12] = { -1, 'Y', 'N', -1, A2K_LEFT, A2K_UP, A2K_DOWN, A2K_RIGHT,
                                   A2K_ESC, A2K_SPACE, A2K_RETURN, -1 };
    static bool prev[12];
    for (int z = 0; z < 12; z++) {
        const bool on = (in->zones >> z) & 1;
        if (kZone[z] < 0 || on == prev[z]) continue;
        prev[z] = on;
        apple2_key(cpu_m, kZone[z], on);
    }
    uint8_t joy = 0;
    if (in->zones & (1 << 4) || in->left) joy |= A2_JOY_LEFT;
    if (in->zones & (1 << 5) || in->up) joy |= A2_JOY_UP;
    if (in->zones & (1 << 6) || in->down) joy |= A2_JOY_DOWN;
    if (in->zones & (1 << 7) || in->right) joy |= A2_JOY_RIGHT;
    if (in->zones & (1 << 11) || in->fire) joy |= A2_JOY_B0;
    if (in->fire2) joy |= A2_JOY_B1;
    apple2_joy(cpu_m, joy);
}

static int frame(const int16_t **audio)
{
    apple2_set_turbo(cpu_m, cyd_turbo);
    return apple2_run_frame(cpu_m, audio);
}

// 560 dots -> 320 pixels: each 7 dots into 4 (weights in quarters of a dot)
static void render(int y, uint16_t *row)
{
    static uint32_t line[A2_W];
    static const uint8_t w[4][3][2] = {
        { { 0, 4 }, { 1, 3 }, { 1, 0 } }, { { 1, 1 }, { 2, 4 }, { 3, 2 } },
        { { 3, 2 }, { 4, 4 }, { 5, 1 } }, { { 5, 3 }, { 6, 4 }, { 6, 0 } },
    };
    apple2_render_row(cpu_m, y * 4 / 5, line);
    const uint32_t *in = line;
    for (int x = 0; x < A2_W / 7; x++, in += 7, row += 4) {
        for (int o = 0; o < 4; o++) {
            uint32_t r = 0, g = 0, b = 0;
            for (int k = 0; k < 3; k++) {
                const uint32_t c = in[w[o][k][0]], q = w[o][k][1];
                r += ((c >> 16) & 255) * q;
                g += ((c >> 8) & 255) * q;
                b += (c & 255) * q;
            }
            row[o] = rgb565((uint8_t)(r / 7), (uint8_t)(g / 7), (uint8_t)(b / 7));
        }
    }
}

static const char *const kExts[] = A2_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "apple2", "apple2", "APPLE II", "MEDIA: RESET / HOLD=SD", { 120, 200, 90 },
    kExts, A2_MEDIA_LABEL, A2_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "Y", "N", NULL, "LEFT", "UP", "DOWN", "RIGHT", "ESC", "SPACE", "RETURN", "BUTTON" },
    char_keys, key, keys_clear, media_name, NULL,
};

void app_main(void)
{
    static const cyd_game_t info = { "apple2", "APPLE II", "MEDIA: RESET / HOLD=SD", { 120, 200, 90 } };
    cyd_boot(&info);
    cpu_m = apple2_create(A2_MODEL, A2_AUDIO_RATE);
    if (!a2_rom_load()) cyd_rom_error();
    if (!cpu_m || !a2_rom_main) {
        ESP_LOGE("apple2", "%s", cpu_m ? "no Apple II ROMs" : "out of memory for the Apple II");
        abort();
    }
    apple2_set_roms(cpu_m, a2_rom_main, a2_main_size(A2_MODEL), a2_rom_chr, a2_rom_chr ? a2_chr_size(A2_MODEL) : 0,
                    a2_rom_disk, a2_rom_aid);
    apple2_reset(cpu_m);
    comp_cyd_start(&kMachine);
}
