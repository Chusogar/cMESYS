// st_sdl.c - Atari 1040ST on desktop (SDL2: Linux, Windows) and web
// (Emscripten).
//
//   atarist [--tos tos.img] [a.st [b.st]]       (.st, .msa, .stx)
//
//   F11 menu (disks in A / B, eject, reset, keypad joystick)   F12 TURBO
//   F1-F10 are the ST's. Mouse: click in the window to take it (the middle
//   button lets it go). Joystick (port 1): keypad 8 4 6 2 (7 9 1 3), fire
//   keypad 0 / 5 or Right Ctrl. Alt = Alternate, Home = Clr Home, Page Up =
//   Help, Page Down = Undo, Num Lock / Scroll Lock = keypad ( ).
// The keys go by position. Drag-and-drop a disk onto the window: into A
// (web: Upload bar).
#include <time.h>
#include "st_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[ST_W * ST_H];
static uint32_t pal565[65536];
static comp_menu mm;
static char names[ST_DRIVES][48];
static int target_drive;
static bool keypad_joy = true;
static bool held_prev[128];
static bool led_drive;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_ESCAPE, SK_ESC }, { SDL_SCANCODE_1, SK_1 }, { SDL_SCANCODE_2, SK_2 }, { SDL_SCANCODE_3, SK_3 },
    { SDL_SCANCODE_4, SK_4 }, { SDL_SCANCODE_5, SK_5 }, { SDL_SCANCODE_6, SK_6 }, { SDL_SCANCODE_7, SK_7 },
    { SDL_SCANCODE_8, SK_8 }, { SDL_SCANCODE_9, SK_9 }, { SDL_SCANCODE_0, SK_0 }, { SDL_SCANCODE_MINUS, SK_MINUS },
    { SDL_SCANCODE_EQUALS, SK_EQUALS }, { SDL_SCANCODE_GRAVE, SK_GRAVE }, { SDL_SCANCODE_BACKSPACE, SK_BACKSPACE },
    { SDL_SCANCODE_TAB, SK_TAB }, { SDL_SCANCODE_Q, SK_Q }, { SDL_SCANCODE_W, SK_W }, { SDL_SCANCODE_E, SK_E },
    { SDL_SCANCODE_R, SK_R }, { SDL_SCANCODE_T, SK_T }, { SDL_SCANCODE_Y, SK_Y }, { SDL_SCANCODE_U, SK_U },
    { SDL_SCANCODE_I, SK_I }, { SDL_SCANCODE_O, SK_O }, { SDL_SCANCODE_P, SK_P },
    { SDL_SCANCODE_LEFTBRACKET, SK_LBRACKET }, { SDL_SCANCODE_RIGHTBRACKET, SK_RBRACKET },
    { SDL_SCANCODE_RETURN, SK_RETURN }, { SDL_SCANCODE_DELETE, SK_DELETE },
    { SDL_SCANCODE_LCTRL, SK_CTRL }, { SDL_SCANCODE_A, SK_A }, { SDL_SCANCODE_S, SK_S }, { SDL_SCANCODE_D, SK_D },
    { SDL_SCANCODE_F, SK_F }, { SDL_SCANCODE_G, SK_G }, { SDL_SCANCODE_H, SK_H }, { SDL_SCANCODE_J, SK_J },
    { SDL_SCANCODE_K, SK_K }, { SDL_SCANCODE_L, SK_L }, { SDL_SCANCODE_SEMICOLON, SK_SEMICOLON },
    { SDL_SCANCODE_APOSTROPHE, SK_QUOTE }, { SDL_SCANCODE_BACKSLASH, SK_BACKSLASH },
    { SDL_SCANCODE_NONUSHASH, SK_BACKSLASH },
    { SDL_SCANCODE_LSHIFT, SK_LSHIFT }, { SDL_SCANCODE_NONUSBACKSLASH, SK_ISO },
    { SDL_SCANCODE_Z, SK_Z }, { SDL_SCANCODE_X, SK_X }, { SDL_SCANCODE_C, SK_C }, { SDL_SCANCODE_V, SK_V },
    { SDL_SCANCODE_B, SK_B }, { SDL_SCANCODE_N, SK_N }, { SDL_SCANCODE_M, SK_M }, { SDL_SCANCODE_COMMA, SK_COMMA },
    { SDL_SCANCODE_PERIOD, SK_PERIOD }, { SDL_SCANCODE_SLASH, SK_SLASH }, { SDL_SCANCODE_RSHIFT, SK_RSHIFT },
    { SDL_SCANCODE_LALT, SK_ALT }, { SDL_SCANCODE_RALT, SK_ALT }, { SDL_SCANCODE_SPACE, SK_SPACE },
    { SDL_SCANCODE_CAPSLOCK, SK_CAPSLOCK },
    { SDL_SCANCODE_F1, SK_F1 }, { SDL_SCANCODE_F2, SK_F2 }, { SDL_SCANCODE_F3, SK_F3 }, { SDL_SCANCODE_F4, SK_F4 },
    { SDL_SCANCODE_F5, SK_F5 }, { SDL_SCANCODE_F6, SK_F6 }, { SDL_SCANCODE_F7, SK_F7 }, { SDL_SCANCODE_F8, SK_F8 },
    { SDL_SCANCODE_F9, SK_F9 }, { SDL_SCANCODE_F10, SK_F10 },
    { SDL_SCANCODE_HOME, SK_CLRHOME }, { SDL_SCANCODE_INSERT, SK_INSERT },
    { SDL_SCANCODE_PAGEUP, SK_HELP }, { SDL_SCANCODE_PAGEDOWN, SK_UNDO }, { SDL_SCANCODE_END, SK_UNDO },
    { SDL_SCANCODE_UP, SK_UP }, { SDL_SCANCODE_DOWN, SK_DOWN }, { SDL_SCANCODE_LEFT, SK_LEFT }, { SDL_SCANCODE_RIGHT, SK_RIGHT },
    { SDL_SCANCODE_NUMLOCKCLEAR, SK_KPLPAREN }, { SDL_SCANCODE_SCROLLLOCK, SK_KPRPAREN },
    { SDL_SCANCODE_KP_DIVIDE, SK_KPSLASH }, { SDL_SCANCODE_KP_MULTIPLY, SK_KPSTAR },
    { SDL_SCANCODE_KP_MINUS, SK_KPMINUS }, { SDL_SCANCODE_KP_PLUS, SK_KPPLUS },
    { SDL_SCANCODE_KP_ENTER, SK_KPENTER }, { SDL_SCANCODE_KP_PERIOD, SK_KPDOT },
};
static const struct { int sc; int key; } keypad[] = {
    { SDL_SCANCODE_KP_0, SK_KP0 }, { SDL_SCANCODE_KP_1, SK_KP1 }, { SDL_SCANCODE_KP_2, SK_KP2 },
    { SDL_SCANCODE_KP_3, SK_KP3 }, { SDL_SCANCODE_KP_4, SK_KP4 }, { SDL_SCANCODE_KP_5, SK_KP5 },
    { SDL_SCANCODE_KP_6, SK_KP6 }, { SDL_SCANCODE_KP_7, SK_KP7 }, { SDL_SCANCODE_KP_8, SK_KP8 },
    { SDL_SCANCODE_KP_9, SK_KP9 },
};

static void set_title(void)
{
    char t[260];
    snprintf(t, sizeof t, "Atari ST - A: %s  B: %s%s%s  (F11 menu, F12 turbo, %s)",
             names[0][0] ? names[0] : "-", names[1][0] ? names[1] : "-", led_drive ? " [DISK]" : "",
             sdl_port_turbo() ? " [TURBO]" : "",
             sdl_port_mouse_captured() ? "middle button: free the mouse" : "click: mouse");
    sdl_port_set_title(t);
}

static void set_items(void)
{
    mm.items[0] = names[0][0] ? "Change disk in A" : "Insert disk in A";
    mm.items[1] = names[1][0] ? "Change disk in B" : "Insert disk in B";
    mm.items[2] = "Eject A and B";
    mm.items[3] = "Reset";
    mm.items[4] = keypad_joy ? "Keypad: joystick" : "Keypad: ST keys";
    mm.items[5] = "Close menu";
    mm.n = 6;
    snprintf(mm.now, sizeof mm.now, "A %.14s B %.14s", names[0][0] ? names[0] : "-", names[1][0] ? names[1] : "-");
}

// Tests: ST_INPUT="frame:action,..." with the actions jN (joystick bits),
// kXX / uXX (scan code XX hex down / up), mDX/DY (mouse motion), bN (mouse
// buttons: 1 left, 2 right)
static const char *script;
static int script_joy = -1, script_buttons = -1;

static void run_script(long frame_no)
{
    while (script && *script) {
        char *end;
        long f = strtol(script, &end, 10);
        if (end == script || *end != ':' || f > frame_no) return;
        const char *a = end + 1;
        if (*a == 'j') script_joy = (int)strtol(a + 1, &end, 10);
        else if (*a == 'k') st_key((int)strtol(a + 1, &end, 16), true);
        else if (*a == 'u') st_key((int)strtol(a + 1, &end, 16), false);
        else if (*a == 'b') script_buttons = (int)strtol(a + 1, &end, 10);
        else if (*a == 'm') {
            int dx = (int)strtol(a + 1, &end, 10), dy = 0;
            if (*end == '/') dy = (int)strtol(end + 1, &end, 10);
            st_mouse(dx, dy);
        } else end = (char *)a + 1;
        script = *end == ',' ? end + 1 : end;
    }
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 && n < (16 << 20) ? (uint8_t *)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

static void insert(int drive, const char *path)
{
    if (!path || !path[0]) return;
    uint32_t size = 0;
    uint8_t *img = read_file(path, &size);
    const char *why = "cannot read the file";
    char msg[128];
    if (img && st_insert(drive, img, size, path, &why)) {
        snprintf(names[drive], sizeof names[drive], "%s", comp_basename(path));
        snprintf(msg, sizeof msg, "%c: %s", 'A' + drive, names[drive]);
    } else {
        free(img);
        snprintf(msg, sizeof msg, "%c: %s", 'A' + drive, why ? why : "not a disk");
    }
    comp_note(&mm, msg);
    set_items();
    set_title();
}

static void menu_activate(void)
{
    switch (mm.sel) {
    case 0:
    case 1: {
        target_drive = mm.sel;
        char *p = comp_pick_path(&mm);
        if (p) {
            insert(target_drive, p);
            free(p);
            target_drive = 0;
        }
        break;
    }
    case 2:
        st_eject(0);
        st_eject(1);
        names[0][0] = names[1][0] = 0;
        comp_note(&mm, "Disks ejected");
        break;
    case 3: st_reset(); break;
    case 4: keypad_joy = !keypad_joy; break;
    default: break;
    }
    mm.open = false;
    set_items();
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    const uint8_t *tos = st_rom_tos;
    uint32_t tos_size = ST_TOS_SIZE;
    int nd = 0;
    const char *disks[ST_DRIVES] = { NULL, NULL };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tos") && i + 1 < argc) {
            uint8_t *t = read_file(argv[++i], &tos_size);
            if (!t || tos_size != ST_TOS_SIZE) {
                fprintf(stderr, "%s: not a 192 KB TOS (1.00 - 1.04, EmuTOS 192K)\n", argv[i]);
                return 1;
            }
            tos = t;
        } else if (nd < ST_DRIVES) {
            disks[nd++] = argv[i];
        }
    }
    if (!tos) {                         // not built in: from storage (romfs), or --tos
        if (!st_rom_load()) sdl_port_rom_error();
        tos = st_rom_tos;
    }
    if (!st_init(tos, tos_size, false)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    {
        const time_t now = time(NULL);
        const struct tm *t = localtime(&now);
        if (t) st_set_time(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    }
    for (int i = 0; i < 65536; i++) {
        const int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
        pal565[i] = (uint32_t)(((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2)));
    }
    mm = (comp_menu){ frame, ST_W, ST_H, "ATARI ST  F11", "ST MSA STX DISKS",
                      "*.st *.ST *.msa *.MSA *.stx *.STX", { NULL }, 0, 6, false, "", "" };
    set_items();
    for (int i = 0; i < nd; i++) insert(i, disks[i]);

    sdl_port_cfg cfg = { "Atari ST", ST_W, ST_H, 2, ST_AUDIO_RATE, ST_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);
    set_title();
    comp_web_setup("Atari ST disks (.st .msa .stx)", ".st,.msa,.stx");
    bool captured = false;
    script = getenv("ST_INPUT");
    long frame_no = 0;

    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            insert(target_drive, drop);
            target_drive = 0;
            free(drop);
            mm.open = false;
        }
        int k;
        while ((k = sdl_port_next_press()) >= 0) {
            if (mm.open) {
                if (k == SDL_SCANCODE_UP) { if (mm.sel > 0) mm.sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (mm.sel < mm.n - 1) mm.sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) menu_activate();
                else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F11) mm.open = false;
                continue;
            }
            if (k == SDL_SCANCODE_F11) {
                mm.open = true;
                mm.sel = 0;
                mm.msg[0] = 0;
                st_keys_release();
                memset(held_prev, 0, sizeof held_prev);
            }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[128] = { false };
        uint8_t joy = 0;
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
            if (keypad_joy) {
                if (sdl_port_key(SDL_SCANCODE_KP_8) || sdl_port_key(SDL_SCANCODE_KP_7) || sdl_port_key(SDL_SCANCODE_KP_9))
                    joy |= ST_JOY_UP;
                if (sdl_port_key(SDL_SCANCODE_KP_2) || sdl_port_key(SDL_SCANCODE_KP_1) || sdl_port_key(SDL_SCANCODE_KP_3))
                    joy |= ST_JOY_DOWN;
                if (sdl_port_key(SDL_SCANCODE_KP_4) || sdl_port_key(SDL_SCANCODE_KP_7) || sdl_port_key(SDL_SCANCODE_KP_1))
                    joy |= ST_JOY_LEFT;
                if (sdl_port_key(SDL_SCANCODE_KP_6) || sdl_port_key(SDL_SCANCODE_KP_9) || sdl_port_key(SDL_SCANCODE_KP_3))
                    joy |= ST_JOY_RIGHT;
                if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || sdl_port_key(SDL_SCANCODE_RCTRL))
                    joy |= ST_JOY_FIRE;
            } else {
                for (size_t i = 0; i < sizeof keypad / sizeof keypad[0]; i++)
                    if (sdl_port_key(keypad[i].sc)) held[keypad[i].key] = true;
                if (sdl_port_key(SDL_SCANCODE_RCTRL)) joy |= ST_JOY_FIRE;
            }
        }
        for (int c = 0; c < 128; c++)
            if (held[c] != held_prev[c]) st_key(c, held[c]);
        memcpy(held_prev, held, sizeof held_prev);
        run_script(frame_no++);
        if (script_joy >= 0) joy |= (uint8_t)script_joy;
        st_joy(1, joy);
        int dx, dy;
        const uint32_t b = sdl_port_mouse(&dx, &dy);
        if (!mm.open) {
            st_mouse(dx, dy);
            st_mouse_buttons(b & 1, (b & 2) != 0);
        } else {
            st_mouse_buttons(false, false);
        }
        if (script_buttons >= 0) st_mouse_buttons(script_buttons & 1, (script_buttons & 2) != 0);
        if (captured != sdl_port_mouse_captured() || led_drive != st_drive_led()) {
            captured = sdl_port_mouse_captured();
            led_drive = st_drive_led();
            set_title();
        }

        const int16_t *audio;
        const int n = st_run_frame(&audio);
        sdl_port_audio(audio, n);
        const uint16_t *fb = st_frame();
        for (int i = 0; i < ST_W * ST_H; i++) frame[i] = pal565[fb[i]];
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
