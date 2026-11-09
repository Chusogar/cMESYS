// cpc_cyd.c - Amstrad CPC 6128 on the ESP32 CYD (common/comp_cyd.h).
//
// Media from the SD card (hold MEDIA): .sna .dsk .edsk .cdt .tzx .csw. The
// menu also types |TAPE + RUN", CAT or |CPM for you, plays / pauses the
// tape, resets and turns the green monitor on. The serial console is the
// keyboard (what you type there is typed into the CPC).
//
// Touch zones (MEDIA: hold = SD menu, tap = tape play / pause):
//     MEDIA | 1     | 2     | HELP
//     LEFT  | UP    | DOWN  | RIGHT     (the cursor keys and the joystick)
//     COPY  | SPACE | ENTER | FIRE
// Nunchuk: the joystick, Z fire 1, C fire 2.
#include "cpc_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

static cpc *cpu_m;
static char media_name[40];
static bool green;

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".dsk", ".edsk", ".cdt", ".tzx", ".csw", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = cpc_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : cpc_last_error(cpu_m);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void)
{
    if (!cpc_tape_loaded(cpu_m)) {
        cyd_show_toast("No tape");
        return;
    }
    cpc_tape_toggle(cpu_m);
    cyd_show_toast(cpc_tape_playing(cpu_m) ? "Tape playing" : "Tape paused");
}

static const char *item(int i)
{
    switch (i) {
    case 0: return cpc_tape_playing(cpu_m) ? "Tape pause" : "Tape play";
    case 1: return "Type |TAPE + RUN\" (tapes)";
    case 2: return "Type CAT (disks)";
    case 3: return "Type |CPM";
    case 4: return green ? "Colour monitor" : "Green monitor";
    case 5: return "Reset";
    default: return NULL;
    }
}

static void item_do(int i)
{
    switch (i) {
    case 0: tap(); break;
    case 1: cyd_type("|TAPE\rRUN\"\r "); break;
    case 2: cyd_type("CAT\r"); break;
    case 3: cyd_type("|CPM\r"); break;
    case 4: green = !green; cpc_set_color_monitor(cpu_m, !green); break;
    default: cpc_reset(cpu_m); break;
    }
}

static int char_keys(char c, int *k)
{
    static const char kShifted[] = "!\"#$%&'()";               // Shift + 1..9
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') {
        static const int kLetters[26] = {
            CPCK_A, CPCK_B, CPCK_C, CPCK_D, CPCK_E, CPCK_F, CPCK_G, CPCK_H, CPCK_I, CPCK_J, CPCK_K, CPCK_L, CPCK_M,
            CPCK_N, CPCK_O, CPCK_P, CPCK_Q, CPCK_R, CPCK_S, CPCK_T, CPCK_U, CPCK_V, CPCK_W, CPCK_X, CPCK_Y, CPCK_Z,
        };
        k[0] = kLetters[c - 'A'];
        return 1;
    }
    static const int kDigits[10] = { CPCK_0, CPCK_1, CPCK_2, CPCK_3, CPCK_4, CPCK_5, CPCK_6, CPCK_7, CPCK_8, CPCK_9 };
    if (c >= '0' && c <= '9') { k[0] = kDigits[c - '0']; return 1; }
    const char *s = strchr(kShifted, c);
    if (s && c) { k[0] = CPCK_SHIFT; k[1] = kDigits[s - kShifted + 1]; return 2; }
    switch (c) {
    case ' ': k[0] = CPCK_SPACE; return 1;
    case '\r': k[0] = CPCK_RETURN; return 1;
    case 8: case 127: k[0] = CPCK_DEL; return 1;
    case 27: k[0] = CPCK_ESC; return 1;
    case '|': k[0] = CPCK_SHIFT; k[1] = CPCK_AT; return 2;
    case '@': k[0] = CPCK_AT; return 1;
    case ':': k[0] = CPCK_COLON; return 1;
    case '*': k[0] = CPCK_SHIFT; k[1] = CPCK_COLON; return 2;
    case ';': k[0] = CPCK_SEMICOLON; return 1;
    case '+': k[0] = CPCK_SHIFT; k[1] = CPCK_SEMICOLON; return 2;
    case ',': k[0] = CPCK_COMMA; return 1;
    case '<': k[0] = CPCK_SHIFT; k[1] = CPCK_COMMA; return 2;
    case '.': k[0] = CPCK_PERIOD; return 1;
    case '>': k[0] = CPCK_SHIFT; k[1] = CPCK_PERIOD; return 2;
    case '/': k[0] = CPCK_SLASH; return 1;
    case '?': k[0] = CPCK_SHIFT; k[1] = CPCK_SLASH; return 2;
    case '-': k[0] = CPCK_MINUS; return 1;
    case '=': k[0] = CPCK_SHIFT; k[1] = CPCK_MINUS; return 2;
    case '^': k[0] = CPCK_CARET; return 1;
    case '[': k[0] = CPCK_LBRACKET; return 1;
    case ']': k[0] = CPCK_RBRACKET; return 1;
    case '\\': k[0] = CPCK_BACKSLASH; return 1;
    default: return 0;
    }
}

static void key(int code, bool down) { cpc_key(cpu_m, code, down); }
static void keys_clear(void) { cpc_keys_clear(cpu_m); }

static void input(const comp_cyd_in *in)
{
    static const int kZone[12] = { -1, CPCK_1, CPCK_2, -1, CPCK_LEFT, CPCK_UP, CPCK_DOWN, CPCK_RIGHT,
                                   CPCK_COPY, CPCK_SPACE, CPCK_RETURN, -1 };
    cpc_keys_clear(cpu_m);
    uint8_t joy = 0;
    for (int z = 0; z < 12; z++)
        if ((in->zones >> z) & 1 && kZone[z] >= 0) cpc_key(cpu_m, kZone[z], true);
    if (in->zones & (1 << 4) || in->left) joy |= CPC_JOY_LEFT;
    if (in->zones & (1 << 5) || in->up) joy |= CPC_JOY_UP;
    if (in->zones & (1 << 6) || in->down) joy |= CPC_JOY_DOWN;
    if (in->zones & (1 << 7) || in->right) joy |= CPC_JOY_RIGHT;
    if (in->zones & (1 << 11) || in->fire) joy |= CPC_JOY_FIRE1;
    if (in->fire2) joy |= CPC_JOY_FIRE2;
    cpc_joy(cpu_m, joy);
}

static int frame(const int16_t **audio)
{
    cpc_set_turbo(cpu_m, cyd_turbo);
    return cpc_run_frame(cpu_m, audio);
}
static void render(int y, uint16_t *row) { cpc_render_row565(cpu_m, CPC_PIC_Y + y, CPC_PIC_X, 320, row); }
static void colours(void) { cpc_set_rgb565(cpu_m, rgb565); }

static const char *const kExts[] = CPC_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "cpc", "cpc6128", "AMSTRAD CPC 6128", "MEDIA: TAPE / HOLD=SD", { 255, 180, 40 },
    kExts, CPC_MEDIA_LABEL, CPC_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "1", "2", NULL, "LEFT", "UP", "DOWN", "RIGHT", "COPY", "SPACE", "ENTER", "FIRE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "cpc6128", "AMSTRAD CPC 6128", "MEDIA: TAPE / HOLD=SD", { 255, 180, 40 } };
    cyd_boot(&info);
    cpu_m = cpc_create(CPC_6128, CPC_AUDIO_RATE);
    if (!cpu_m || !cpc_rom_os) {
        ESP_LOGE("cpc", "%s", cpu_m ? "no CPC ROMs built in (roms/cpc6128/)" : "out of memory for the CPC");
        abort();
    }
    cpc_set_roms(cpu_m, cpc_rom_os, cpc_rom_amsdos);
    cpc_reset(cpu_m);
    comp_cyd_start(&kMachine);
}
