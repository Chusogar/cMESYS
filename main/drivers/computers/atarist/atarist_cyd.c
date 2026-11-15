// atarist_cyd.c - Atari ST on the ESP32 CYD (common/comp_cyd.h): 128-256 KB
// of RAM (what the ESP32 has room for; TOS told it is already sized, as after
// a warm reset), the picture as 320x200 (low resolution as it is, medium
// and high halved) in the middle of the panel. TOS built in (roms/atarist).
//
// Disks from the SD card (hold MEDIA): .st .msa .stx into drive A (read from
// the card as they are used: such a disk is read only). The serial console
// is the keyboard.
//
// The touch panel is the mouse: the arrow goes where it is touched, and the
// left button is held while the finger stays (MEDIA's and HELP's corners
// excepted). Nunchuk: the joystick (port 1), Z its fire, C the right button.
#include "atarist_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_cyd.h"

#define TOP ((240 - 200) / 2)
#define K_SHIFT 0x80                // the typist's SHIFT (with the key)

static char media_name[40];
static int return_frames;
static int touch_frames;            // frames the finger has been down
static int typed[3], ntyped;

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".st", ".stx", NULL };
    zx_pbuf pb;
    atarist_eject(0);
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = atarist_load_pbuf(0, &pb, name);
    *why = ok ? "" : atarist_last_error();
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { return_frames = 4; }

static const char *item(int i)
{
    switch (i) {
    case 0: return "Eject the disk";
    case 1: return "Reset";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        atarist_eject(0);
        media_name[0] = 0;
    }
    if (i == 1) atarist_reset();
}

static int char_keys(char c, int *k)
{
    bool shift;
    const int code = atarist_char_key(c, &shift);
    if (!code) return 0;
    k[0] = code | (shift ? K_SHIFT : 0);
    return 1;
}

static void key(int code, bool down)
{
    if (down && ntyped < 3) typed[ntyped++] = code;
}

static void keys_clear(void) { ntyped = 0; }

static void input(const comp_cyd_in *in)
{
    atarist_keys_clear();
    for (int i = 0; i < ntyped; i++) {
        atarist_key(typed[i] & 0x7f, true);
        if (typed[i] & K_SHIFT) atarist_key(STK_LSHIFT, true);
    }
    ntyped = 0;
    if (return_frames > 0) {
        return_frames--;
        atarist_key(STK_RETURN, true);
    }
    unsigned joy = 0;
    if (in->left) joy |= ST_LEFT;
    if (in->right) joy |= ST_RIGHT;
    if (in->up) joy |= ST_UP;
    if (in->down) joy |= ST_DOWN;
    if (in->fire) joy |= ST_FIRE;
    atarist_joy(1, joy);
    // the panel: the arrow there first, then the button down
    touch_frames = in->touch ? touch_frames + 1 : 0;
    atarist_pointer p = { true, false, false, 0, 0, 0, 0, false, in->fire2 };
    static int last_x = ST_W / 2, last_y = ST_H / 2;
    if (in->touch) {
        int y = in->touch_y - TOP;
        y = y < 0 ? 0 : y > 199 ? 199 : y;
        last_x = in->touch_x * 2;
        last_y = y * 2;
    }
    p.x = last_x;
    p.y = last_y;
    p.left = touch_frames > 2;
    atarist_pointer_input(&p);
}

static int frame(const int16_t **audio)
{
    atarist_set_turbo(cyd_turbo);
    return atarist_run_frame(audio);
}

static void render(int y, uint16_t *row)
{
    const int ly = y - TOP;
    if (ly < 0 || ly >= 200) {
        for (int x = 0; x < 320; x++) row[x] = 0;
        return;
    }
    atarist_render_row320(ly, row);
}

static void colours(void) { atarist_set_rgb565(rgb565); }

static const char *const kExts[] = ST_MEDIA_EXTS;
static const comp_cyd_machine kMachine = {
    "atarist", "atarist", "ATARI ST", "MEDIA: RETURN / HOLD=SD", { 120, 200, 120 },
    kExts, ST_MEDIA_LABEL, ST_AUDIO_RATE, load, item, item_do, tap, input, frame, render,
    { NULL, "MOUSE", "MOUSE", NULL, "MOUSE", "MOUSE", "MOUSE", "MOUSE", "MOUSE", "MOUSE", "MOUSE", "MOUSE" },
    char_keys, key, keys_clear, media_name, colours,
};

void app_main(void)
{
    static const cyd_game_t info = { "atarist", "ATARI ST", "MEDIA: RETURN / HOLD=SD", { 120, 200, 120 } };
    cyd_boot(&info);
    // as much RAM as the heap has left, 256 KB down to 128 KB (EmuTOS boots
    // its desktop and runs programs in 128 KB)
    bool ok = false;
    for (uint32_t kb = 256; kb >= 128 && atarist_rom_tos && !ok; kb -= 32)
        ok = atarist_init(atarist_rom_tos, ST_ROM_SIZE, kb * 1024, ST_AUDIO_RATE);
    if (!ok) {
        ESP_LOGE("atarist", "%s", atarist_rom_tos ? "out of memory for the ST" : "no TOS built in (roms/atarist/)");
        abort();
    }
    ESP_LOGI("atarist", "%u KB of RAM, %u KB of heap left", (unsigned)(atarist_ram_size() / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    comp_cyd_start(&kMachine);
}
