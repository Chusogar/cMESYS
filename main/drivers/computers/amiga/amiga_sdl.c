// amiga_sdl.c - Commodore Amiga 500 on desktop (SDL2: Linux, Windows) and
// web (Emscripten).
//
//   amiga [--kick kickstart.rom] [df0.adf [df1.adf]]
//
//   F11 menu (disks in DF0 / DF1, eject, reset, keypad joystick)   F12 TURBO
//   F1-F10 are the Amiga's. Mouse: click in the window to take it (the middle
//   button lets it go). Joystick (port 1): keypad 8 4 6 2 (7 9 1 3), fire
//   keypad 0 / 5 or Right Ctrl. Left Windows / Page Up = Left Amiga, Right
//   Windows / Menu / Page Down = Right Amiga, End = HELP, Insert = \ |,
//   Num Lock / Scroll Lock = keypad ( ). Ctrl + both Amigas resets.
// The keys go by position (the Spanish A500's layout is the Spanish PC's).
// Drag-and-drop an .adf onto the window: into DF0 (web: Upload bar).
#include "amiga_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[AMIGA_W * AMIGA_H];
static uint32_t pal565[65536];
static comp_menu mm;
static char names[AMIGA_DRIVES][48];
static int target_drive;               // where the next file goes
static bool keypad_joy = true;
static bool held_prev[0x68];
static bool led_drive;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_GRAVE, AK_GRAVE }, { SDL_SCANCODE_1, AK_1 }, { SDL_SCANCODE_2, AK_2 }, { SDL_SCANCODE_3, AK_3 },
    { SDL_SCANCODE_4, AK_4 }, { SDL_SCANCODE_5, AK_5 }, { SDL_SCANCODE_6, AK_6 }, { SDL_SCANCODE_7, AK_7 },
    { SDL_SCANCODE_8, AK_8 }, { SDL_SCANCODE_9, AK_9 }, { SDL_SCANCODE_0, AK_0 }, { SDL_SCANCODE_MINUS, AK_MINUS },
    { SDL_SCANCODE_EQUALS, AK_EQUALS }, { SDL_SCANCODE_INSERT, AK_BACKSLASH }, { SDL_SCANCODE_BACKSPACE, AK_BACKSPACE },
    { SDL_SCANCODE_TAB, AK_TAB }, { SDL_SCANCODE_Q, AK_Q }, { SDL_SCANCODE_W, AK_W }, { SDL_SCANCODE_E, AK_E },
    { SDL_SCANCODE_R, AK_R }, { SDL_SCANCODE_T, AK_T }, { SDL_SCANCODE_Y, AK_Y }, { SDL_SCANCODE_U, AK_U },
    { SDL_SCANCODE_I, AK_I }, { SDL_SCANCODE_O, AK_O }, { SDL_SCANCODE_P, AK_P },
    { SDL_SCANCODE_LEFTBRACKET, AK_LBRACKET }, { SDL_SCANCODE_RIGHTBRACKET, AK_RBRACKET },
    { SDL_SCANCODE_RETURN, AK_RETURN }, { SDL_SCANCODE_LCTRL, AK_CTRL }, { SDL_SCANCODE_CAPSLOCK, AK_CAPSLOCK },
    { SDL_SCANCODE_A, AK_A }, { SDL_SCANCODE_S, AK_S }, { SDL_SCANCODE_D, AK_D }, { SDL_SCANCODE_F, AK_F },
    { SDL_SCANCODE_G, AK_G }, { SDL_SCANCODE_H, AK_H }, { SDL_SCANCODE_J, AK_J }, { SDL_SCANCODE_K, AK_K },
    { SDL_SCANCODE_L, AK_L }, { SDL_SCANCODE_SEMICOLON, AK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, AK_QUOTE },
    { SDL_SCANCODE_BACKSLASH, AK_HASH }, { SDL_SCANCODE_NONUSHASH, AK_HASH },
    { SDL_SCANCODE_LSHIFT, AK_LSHIFT }, { SDL_SCANCODE_NONUSBACKSLASH, AK_LESS },
    { SDL_SCANCODE_Z, AK_Z }, { SDL_SCANCODE_X, AK_X }, { SDL_SCANCODE_C, AK_C }, { SDL_SCANCODE_V, AK_V },
    { SDL_SCANCODE_B, AK_B }, { SDL_SCANCODE_N, AK_N }, { SDL_SCANCODE_M, AK_M }, { SDL_SCANCODE_COMMA, AK_COMMA },
    { SDL_SCANCODE_PERIOD, AK_PERIOD }, { SDL_SCANCODE_SLASH, AK_SLASH }, { SDL_SCANCODE_RSHIFT, AK_RSHIFT },
    { SDL_SCANCODE_LALT, AK_LALT }, { SDL_SCANCODE_RALT, AK_RALT },
    { SDL_SCANCODE_LGUI, AK_LAMIGA }, { SDL_SCANCODE_PAGEUP, AK_LAMIGA },
    { SDL_SCANCODE_RGUI, AK_RAMIGA }, { SDL_SCANCODE_APPLICATION, AK_RAMIGA }, { SDL_SCANCODE_PAGEDOWN, AK_RAMIGA },
    { SDL_SCANCODE_SPACE, AK_SPACE }, { SDL_SCANCODE_ESCAPE, AK_ESC }, { SDL_SCANCODE_DELETE, AK_DEL },
    { SDL_SCANCODE_END, AK_HELP },
    { SDL_SCANCODE_UP, AK_UP }, { SDL_SCANCODE_DOWN, AK_DOWN }, { SDL_SCANCODE_LEFT, AK_LEFT }, { SDL_SCANCODE_RIGHT, AK_RIGHT },
    { SDL_SCANCODE_F1, AK_F1 }, { SDL_SCANCODE_F2, AK_F2 }, { SDL_SCANCODE_F3, AK_F3 }, { SDL_SCANCODE_F4, AK_F4 },
    { SDL_SCANCODE_F5, AK_F5 }, { SDL_SCANCODE_F6, AK_F6 }, { SDL_SCANCODE_F7, AK_F7 }, { SDL_SCANCODE_F8, AK_F8 },
    { SDL_SCANCODE_F9, AK_F9 }, { SDL_SCANCODE_F10, AK_F10 },
    { SDL_SCANCODE_NUMLOCKCLEAR, AK_KPLPAREN }, { SDL_SCANCODE_SCROLLLOCK, AK_KPRPAREN },
    { SDL_SCANCODE_KP_DIVIDE, AK_KPSLASH }, { SDL_SCANCODE_KP_MULTIPLY, AK_KPSTAR },
    { SDL_SCANCODE_KP_MINUS, AK_KPMINUS }, { SDL_SCANCODE_KP_PLUS, AK_KPPLUS },
    { SDL_SCANCODE_KP_ENTER, AK_KPENTER }, { SDL_SCANCODE_KP_PERIOD, AK_KPDOT },
};
// the keypad's digits: the joystick, or the Amiga's keypad
static const struct { int sc; int key; } keypad[] = {
    { SDL_SCANCODE_KP_0, AK_KP0 }, { SDL_SCANCODE_KP_1, AK_KP1 }, { SDL_SCANCODE_KP_2, AK_KP2 },
    { SDL_SCANCODE_KP_3, AK_KP3 }, { SDL_SCANCODE_KP_4, AK_KP4 }, { SDL_SCANCODE_KP_5, AK_KP5 },
    { SDL_SCANCODE_KP_6, AK_KP6 }, { SDL_SCANCODE_KP_7, AK_KP7 }, { SDL_SCANCODE_KP_8, AK_KP8 },
    { SDL_SCANCODE_KP_9, AK_KP9 },
};

static void set_title(void)
{
    char t[260];
    snprintf(t, sizeof t, "Amiga 500 - DF0: %s  DF1: %s%s%s  (F11 menu, F12 turbo, %s)",
             names[0][0] ? names[0] : "-", names[1][0] ? names[1] : "-", led_drive ? " [DISK]" : "",
             sdl_port_turbo() ? " [TURBO]" : "",
             sdl_port_mouse_captured() ? "middle button: free the mouse" : "click: mouse");
    sdl_port_set_title(t);
}

static void set_items(void)
{
    static char df0[40], df1[40];
    snprintf(df0, sizeof df0, "%s", names[0][0] ? "Change disk in DF0" : "Insert disk in DF0");
    snprintf(df1, sizeof df1, "%s", names[1][0] ? "Change disk in DF1" : "Insert disk in DF1");
    mm.items[0] = df0;
    mm.items[1] = df1;
    mm.items[2] = "Eject DF0 and DF1";
    mm.items[3] = "Reset";
    mm.items[4] = keypad_joy ? "Keypad: joystick" : "Keypad: Amiga keys";
    mm.items[5] = "Close menu";
    mm.n = 6;
    snprintf(mm.now, sizeof mm.now, "DF0 %.14s DF1 %.14s", names[0][0] ? names[0] : "-", names[1][0] ? names[1] : "-");
}

// Tests: AMIGA_INPUT="frame:action,..." with the actions jN (joystick bits),
// kXX / uXX (raw key XX hex down / up), mDX/DY (mouse motion), bN (mouse
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
        else if (*a == 'k') amiga_key((int)strtol(a + 1, &end, 16), true);
        else if (*a == 'u') amiga_key((int)strtol(a + 1, &end, 16), false);
        else if (*a == 'b') script_buttons = (int)strtol(a + 1, &end, 10);
        else if (*a == 'm') {
            int dx = (int)strtol(a + 1, &end, 10), dy = 0;
            if (*end == '/') dy = (int)strtol(end + 1, &end, 10);
            amiga_mouse(dx, dy);
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
    if (img && amiga_insert(drive, img, size, &why)) {
        snprintf(names[drive], sizeof names[drive], "%s", comp_basename(path));
        char msg[80];
        snprintf(msg, sizeof msg, "DF%d: %s", drive, names[drive]);
        comp_note(&mm, msg);
    } else {
        free(img);
        char msg[128];
        snprintf(msg, sizeof msg, "DF%d: %s", drive, why);
        comp_note(&mm, msg);
    }
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
        amiga_eject(0);
        amiga_eject(1);
        names[0][0] = names[1][0] = 0;
        comp_note(&mm, "Disks ejected");
        break;
    case 3:
        amiga_reset();
        break;
    case 4:
        keypad_joy = !keypad_joy;
        break;
    default:
        break;
    }
    mm.open = false;
    set_items();
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    const uint8_t *kick = amiga_rom_kick;
    uint32_t kick_size = 0x40000;
    uint8_t *kick_file = NULL;
    int nd = 0;
    const char *disks[AMIGA_DRIVES] = { NULL, NULL };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kick") && i + 1 < argc) {
            kick_file = read_file(argv[++i], &kick_size);
            if (!kick_file || (kick_size != 0x40000 && kick_size != 0x80000)) {
                fprintf(stderr, "%s: not a 256 or 512 KB Kickstart\n", argv[i]);
                return 1;
            }
            kick = kick_file;
        } else if (nd < AMIGA_DRIVES) {
            disks[nd++] = argv[i];
        }
    }
    if (!kick) {                          // not built in: from storage (romfs), or --kick
        if (!amiga_rom_load()) sdl_port_rom_error();
        kick = amiga_rom_kick;
    }
    if (!amiga_init(kick, kick_size)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    for (int i = 0; i < 65536; i++) {
        const int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
        pal565[i] = (uint32_t)(((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2)));
    }
    mm = (comp_menu){ frame, AMIGA_W, AMIGA_H, "AMIGA 500  F11", "ADF DISKS", "*.adf *.ADF",
                      { NULL }, 0, 6, false, "", "" };
    set_items();
    for (int i = 0; i < nd; i++) insert(i, disks[i]);

    sdl_port_cfg cfg = { "Amiga 500", AMIGA_W, AMIGA_H, 3, AMIGA_AUDIO_RATE, AMIGA_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);
    set_title();
    comp_web_setup("Amiga disks (.adf)", ".adf");
    bool captured = false;
    script = getenv("AMIGA_INPUT");
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
                amiga_keys_release();
                memset(held_prev, 0, sizeof held_prev);
            }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        // keys: what changed since the last frame
        bool held[0x68] = { false };
        uint8_t joy = 0;
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
            if (keypad_joy) {
                if (sdl_port_key(SDL_SCANCODE_KP_8) || sdl_port_key(SDL_SCANCODE_KP_7) || sdl_port_key(SDL_SCANCODE_KP_9))
                    joy |= AMIGA_JOY_UP;
                if (sdl_port_key(SDL_SCANCODE_KP_2) || sdl_port_key(SDL_SCANCODE_KP_1) || sdl_port_key(SDL_SCANCODE_KP_3))
                    joy |= AMIGA_JOY_DOWN;
                if (sdl_port_key(SDL_SCANCODE_KP_4) || sdl_port_key(SDL_SCANCODE_KP_7) || sdl_port_key(SDL_SCANCODE_KP_1))
                    joy |= AMIGA_JOY_LEFT;
                if (sdl_port_key(SDL_SCANCODE_KP_6) || sdl_port_key(SDL_SCANCODE_KP_9) || sdl_port_key(SDL_SCANCODE_KP_3))
                    joy |= AMIGA_JOY_RIGHT;
                if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || sdl_port_key(SDL_SCANCODE_RCTRL))
                    joy |= AMIGA_JOY_FIRE;
            } else {
                for (size_t i = 0; i < sizeof keypad / sizeof keypad[0]; i++)
                    if (sdl_port_key(keypad[i].sc)) held[keypad[i].key] = true;
                if (sdl_port_key(SDL_SCANCODE_RCTRL)) joy |= AMIGA_JOY_FIRE;
            }
        }
        for (int c = 0; c < 0x68; c++)
            if (held[c] != held_prev[c]) amiga_key(c, held[c]);
        memcpy(held_prev, held, sizeof held_prev);
        run_script(frame_no++);
        if (script_joy >= 0) joy |= (uint8_t)script_joy;
        amiga_joy(joy);
        int dx, dy;
        const uint32_t b = sdl_port_mouse(&dx, &dy);
        if (!mm.open) {
            amiga_mouse(dx, dy);
            amiga_mouse_buttons(b & 1, (b & 2) != 0);
        } else {
            amiga_mouse_buttons(false, false);
        }
        if (script_buttons >= 0) amiga_mouse_buttons(script_buttons & 1, (script_buttons & 2) != 0);
        if (captured != sdl_port_mouse_captured() || led_drive != amiga_drive_led()) {
            captured = sdl_port_mouse_captured();
            led_drive = amiga_drive_led();
            set_title();
        }

        const int16_t *audio;
        int n = amiga_run_frame(&audio);
        sdl_port_audio(audio, n);
        const uint16_t *fb = amiga_frame();
        for (int i = 0; i < AMIGA_W * AMIGA_H; i++) frame[i] = pal565[fb[i]];
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
