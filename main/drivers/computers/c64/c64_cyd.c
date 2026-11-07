// c64_cyd.c - Commodore 64 on the ESP32 CYD (common/comp_cyd.h).
//
// Media from the SD card (hold MEDIA): .prg .t64 .d64 .g64 .tap. The menu
// also types LOAD"*",8,1 + RUN for a disk, LOAD for a tape (and presses
// PLAY), swaps the joystick port and resets. The serial console is the
// keyboard (what you type there is typed into the C64).
//
// Touch zones (MEDIA: hold = SD menu, tap = tape play / pause):
//     MEDIA | F1     | RUN/STOP | HELP
//     LEFT  | UP     | DOWN     | RIGHT     (the joystick)
//     SPACE | RETURN | 1        | FIRE
// Nunchuk: the joystick, Z fire, C space.
#include "c64_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

static c64 *cpu_m;
static char media_name[40];
static int joy_port = 2;
static bool play_after_typing, run_after_typing;

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".d64", ".g64", ".tap", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = c64_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : c64_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void)
{
    if (!c64_tape_loaded(cpu_m)) {
        cyd_show_toast("No tape");
        return;
    }
    c64_tape_toggle(cpu_m);
    cyd_show_toast(c64_tape_playing(cpu_m) ? "Tape playing" : "Tape paused");
}

static const char *item(int i)
{
    switch (i) {
    case 0: return c64_tape_playing(cpu_m) ? "Tape pause" : "Tape play";
    case 1: return "Type LOAD\"*\",8,1 + RUN (disk)";
    case 2: return "Type LOAD + play (tape)";
    case 3: return joy_port == 1 ? "Joystick in port 2" : "Joystick in port 1";
    case 4: return "Reset";
    default: return NULL;
    }
}

static void item_do(int i)
{
    switch (i) {
    case 0: tap(); break;
    case 1: cyd_type("LOAD\"*\",8,1\r"); run_after_typing = true; break;
    case 2:
        cyd_type("LOAD\r");
        play_after_typing = !c64_tape_playing(cpu_m) && c64_tape_loaded(cpu_m);
        break;
    case 3: joy_port = 3 - joy_port; cyd_show_toast(joy_port == 1 ? "Joystick port 1" : "Joystick port 2"); break;
    default: c64_reset(cpu_m); break;
    }
}

static int char_keys(char c, int *k)
{
    static const int kLetters[26] = {
        C64K_A, C64K_B, C64K_C, C64K_D, C64K_E, C64K_F, C64K_G, C64K_H, C64K_I, C64K_J, C64K_K, C64K_L, C64K_M,
        C64K_N, C64K_O, C64K_P, C64K_Q, C64K_R, C64K_S, C64K_T, C64K_U, C64K_V, C64K_W, C64K_X, C64K_Y, C64K_Z,
    };
    static const int kDigits[10] = { C64K_0, C64K_1, C64K_2, C64K_3, C64K_4, C64K_5, C64K_6, C64K_7, C64K_8, C64K_9 };
    static const char kShifted[] = "!\"#$%&'()";               // Shift + 1..9
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') { k[0] = kLetters[c - 'A']; return 1; }
    if (c >= '0' && c <= '9') { k[0] = kDigits[c - '0']; return 1; }
    const char *s = c ? strchr(kShifted, c) : NULL;
    if (s) { k[0] = C64K_LSHIFT; k[1] = kDigits[s - kShifted + 1]; return 2; }
    switch (c) {
    case ' ': k[0] = C64K_SPACE; return 1;
    case '\r': k[0] = C64K_RETURN; return 1;
    case 8: case 127: k[0] = C64K_DEL; return 1;
    case 27: k[0] = C64K_RUNSTOP; return 1;
    case '*': k[0] = C64K_ASTERISK; return 1;
    case '+': k[0] = C64K_PLUS; return 1;
    case '-': k[0] = C64K_MINUS; return 1;
    case '=': k[0] = C64K_EQUALS; return 1;
    case ':': k[0] = C64K_COLON; return 1;
    case ';': k[0] = C64K_SEMICOLON; return 1;
    case ',': k[0] = C64K_COMMA; return 1;
    case '.': k[0] = C64K_PERIOD; return 1;
    case '/': k[0] = C64K_SLASH; return 1;
    case '@': k[0] = C64K_AT; return 1;
    case '[': k[0] = C64K_LSHIFT; k[1] = C64K_COLON; return 2;
    case ']': k[0] = C64K_LSHIFT; k[1] = C64K_SEMICOLON; return 2;
    case '<': k[0] = C64K_LSHIFT; k[1] = C64K_COMMA; return 2;
    case '>': k[0] = C64K_LSHIFT; k[1] = C64K_PERIOD; return 2;
    case '?': k[0] = C64K_LSHIFT; k[1] = C64K_SLASH; return 2;
    default: return 0;
    }
}

static void key(int code, bool down) { c64_key(cpu_m, code, down); }
static void keys_clear(void) { c64_keys_clear(cpu_m); }

static void input(const comp_cyd_in *in)
{
    c64_keys_clear(cpu_m);
    if (in->zones & (1 << 1)) c64_key(cpu_m, C64K_F1, true);
    if (in->zones & (1 << 2)) c64_key(cpu_m, C64K_RUNSTOP, true);
    if (in->zones & (1 << 8) || in->fire2) c64_key(cpu_m, C64K_SPACE, true);
    if (in->zones & (1 << 9)) c64_key(cpu_m, C64K_RETURN, true);
    if (in->zones & (1 << 10)) c64_key(cpu_m, C64K_1, true);
    uint8_t joy = 0;
    if (in->zones & (1 << 4) || in->left) joy |= C64_JOY_LEFT;
    if (in->zones & (1 << 5) || in->up) joy |= C64_JOY_UP;
    if (in->zones & (1 << 6) || in->down) joy |= C64_JOY_DOWN;
    if (in->zones & (1 << 7) || in->right) joy |= C64_JOY_RIGHT;
    if (in->zones & (1 << 11) || in->fire) joy |= C64_JOY_FIRE;
    c64_joy(cpu_m, joy_port, joy);
    c64_joy(cpu_m, 3 - joy_port, 0);
    if (run_after_typing && cyd_type_head == cyd_type_tail) {
        run_after_typing = false;               // (keys typed while it loads are lost)
        c64_kbuf(cpu_m, "RUN\r");
    }
    if (play_after_typing && cyd_type_head == cyd_type_tail) {
        play_after_typing = false;
        if (!c64_tape_playing(cpu_m)) c64_tape_toggle(cpu_m);
    }
}

static int frame(const int16_t **audio)
{
    c64_set_turbo(cpu_m, cyd_turbo);
    return c64_run_frame(cpu_m, audio);
}
static void render(int y, uint16_t *row) { c64_render_row565(cpu_m, y, row); }
static void colours(void) { c64_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = C64_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "c64", "c64", "COMMODORE 64", "MEDIA: TAPE / HOLD=SD", { 140, 160, 255 },
    kExts, C64_MEDIA_LABEL, C64_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "F1", "RUN/STOP", NULL, "LEFT", "UP", "DOWN", "RIGHT", "SPACE", "RETURN", "1", "FIRE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "c64", "COMMODORE 64", "MEDIA: TAPE / HOLD=SD", { 140, 160, 255 } };
    cyd_boot(&info);
    cpu_m = c64_create(C64_AUDIO_RATE);
    if (!cpu_m || !c64_rom_kernal) {
        ESP_LOGE("c64", "%s", cpu_m ? "no C64 ROMs built in (roms/c64/)" : "out of memory for the C64");
        abort();
    }
    c64_set_roms(cpu_m, c64_rom_kernal, c64_rom_basic, c64_rom_chargen, c64_rom_dos1541);
    c64_reset(cpu_m);
    comp_cyd_start(&kMachine);
}
