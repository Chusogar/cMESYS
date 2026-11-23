// arc_cyd.c - any arcade game of arc.h on the ESP32 CYD and the TTGO VGA32
// (computers/common/comp_cyd.h): the frontend all of them share. The
// picture in the middle of the 320x240 panel (a vertical game's 256 lines
// lose 8 at the top and 8 at the bottom; one up to 360 wide, its sides;
// a wider one, every other column).
// The ROMs come from the SD card (roms/<set>/ or <set>.zip) when they are
// not built in.
//
// Touch zones (MEDIA: tap = COIN):
//     COIN  | START    | BUTTON 3 | HELP
//     LEFT  | UP       | DOWN     | RIGHT
//     BUTTON 2 | BUTTON 2 | BUTTON 1 | BUTTON 1
// Nunchuk: the joystick, Z = button 1, C = button 2. Serial console: w a s d
// the joystick, k / j / u the buttons, 5 coin, Enter start.
#include <stdlib.h>
#include <string.h>
#include "arc.h"
#include "../../computers/common/comp_sd.h"
#include "../../computers/common/comp_cyd.h"

static int coin_frames;
static unsigned held, typed;

static bool load(const char *name, const char **why) { (void)name; *why = "no media"; return false; }
static void tap(void) { coin_frames = 4; }
static const char *item(int i) { return i == 0 ? "Reset" : NULL; }
static void item_do(int i) { if (i == 0) arc_reset(); }

static int char_keys(char c, int *k)
{
    switch (c) {
    case 'w': case 'W': k[0] = AP_UP; return 1;
    case 's': case 'S': k[0] = AP_DOWN; return 1;
    case 'a': case 'A': k[0] = AP_LEFT; return 1;
    case 'd': case 'D': k[0] = AP_RIGHT; return 1;
    case 'k': case 'K': case ' ': k[0] = AP_B1; return 1;
    case 'j': case 'J': k[0] = AP_B2; return 1;
    case 'u': case 'U': k[0] = AP_B3; return 1;
    case '\r': k[0] = AP_START; return 1;
    case '5': k[0] = 0x10000; return 1;     // coin
    default: return 0;
    }
}

static void key(int code, bool down) { if (down) typed |= (unsigned)code; }
static void keys_clear(void) { typed = 0; }

static void input(const comp_cyd_in *in)
{
    unsigned b = 0;
    if (in->zones & (1 << 1)) b |= AP_START;
    if (in->zones & (1 << 2)) b |= AP_B3;
    if (in->zones & (1 << 4) || in->left) b |= AP_LEFT;
    if (in->zones & (1 << 5) || in->up) b |= AP_UP;
    if (in->zones & (1 << 6) || in->down) b |= AP_DOWN;
    if (in->zones & (1 << 7) || in->right) b |= AP_RIGHT;
    if (in->zones & ((1 << 8) | (1 << 9)) || in->fire2) b |= AP_B2;
    if (in->zones & ((1 << 10) | (1 << 11)) || in->fire) b |= AP_B1;
    held = b;
    typed = 0;
}

static int frame(const int16_t **audio)
{
    arc_in in;
    memset(&in, 0, sizeof in);
    in.p[0] = (held | typed) & 0xffff;
    in.coin1 = coin_frames > 0 || (typed & 0x10000);
    if (coin_frames > 0) coin_frames--;
    arc_input(&in);
    return arc_run_frame(audio);
}

static void render(int y, uint16_t *row)
{
    const int w = arc_game.w, h = arc_game.h;
    // up to 360 wide: the sides cropped; wider: every other column
    const int xs = w > 360 ? 2 : 1, sw = w / xs, ox = (320 - sw) / 2, oy = (240 - h) / 2;
    const int sy = y - oy;
    if (sy < 0 || sy >= h) {
        memset(row, 0, 320 * sizeof *row);
        return;
    }
    if (ox > 0) memset(row, 0, 320 * sizeof *row);
    const uint16_t *src = arc_frame_row(sy) + (ox < 0 ? -ox * xs : 0);
    uint16_t *d = row + (ox > 0 ? ox : 0);
    for (int x = 0; x < sw && x < 320; x++) {
        const uint16_t p = src[x * xs];
        d[x] = rgb565((uint8_t)((p >> 8) & 0xF8), (uint8_t)((p >> 3) & 0xFC), (uint8_t)(p << 3));
    }
}

static const char *const kNoExts[] = { NULL };
static comp_cyd_machine kMachine = {
    "arc", "arc", "", "MEDIA: COIN", { 255, 200, 80 },
    kNoExts, "", 0, load, item, item_do, tap, input, frame, render,
    { NULL, "START", "BUTTON 3", NULL, "LEFT", "UP", "DOWN", "RIGHT", "BUTTON 2", "BUTTON 2", "BUTTON 1", "BUTTON 1" },
    char_keys, key, keys_clear, "", NULL,
};

void app_main(void)
{
    static cyd_game_t info;
    info = (cyd_game_t){ arc_game.id, arc_game.title, "MEDIA: COIN", { 255, 200, 80 } };
    cyd_boot(&info);
    if (!arc_init()) cyd_rom_error();
    kMachine.tag = arc_game.id;
    kMachine.nvs_ns = arc_game.id;
    kMachine.title = arc_game.title;
    kMachine.audio_rate = arc_game.audio_rate;
    if (arc_game.button[0]) kMachine.zone_label[10] = kMachine.zone_label[11] = arc_game.button[0];
    if (arc_game.button[1]) kMachine.zone_label[8] = kMachine.zone_label[9] = arc_game.button[1];
    kMachine.zone_label[2] = arc_game.button[2];
    comp_cyd_start(&kMachine);
}
