// cons_cyd.c - any console of main/drivers/consoles on the ESP32 CYD
// (computers/common/comp_cyd.h): the frontend all of them share (cons.h).
// The picture in the middle of the 320x240 panel (doubled when it fits;
// a bigger one with every other pixel / line).
//
// Media from the SD card (hold MEDIA). Touch zones (MEDIA: hold = SD menu,
// tap = SELECT):
//     MEDIA | BUTTON 3 | BUTTON 4 | HELP
//     LEFT  | UP       | DOWN     | RIGHT
//     START | BUTTON 2 | BUTTON 1 | BUTTON 1
// Nunchuk: the pad, Z = button 1, C = button 2. Serial console: w a s d the
// pad, k / j / u / i the buttons, Enter START, the keys' consoles their keys.
#include <stdlib.h>
#include <string.h>
#include "cons.h"
#include "../../computers/common/comp_sd.h"
#include "../../computers/common/comp_cyd.h"

static char media_name[40];
static int select_frames;
static unsigned held, typed;

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    cons_eject();                           // (the old one's memory first)
    uint8_t *data = ql_sd_load_file(name, &size);
    if (!data) {
        *why = "cannot read the file (out of memory?)";
        return false;
    }
    size_t un = 0;
    char inner[48];
    uint8_t *z = cons_unzip(data, size, cons_machine.exts, &un, inner, sizeof inner);
    if (z) {
        free(data);
        data = z;
        size = un;
    }
    const bool ok = cons_load(data, size, name, why);
    if (ok) snprintf(media_name, sizeof media_name, "%s", name);
    return ok;
}

static void tap(void) { select_frames = 6; }
static const char *item(int i) { return i == 0 ? "Reset" : i == 1 ? "Eject" : NULL; }
static void item_do(int i)
{
    if (i == 0) cons_reset();
    else { cons_eject(); media_name[0] = 0; }
}

static int char_keys(char c, int *k)
{
    switch (c) {
    case 'w': case 'W': k[0] = CP_UP; return 1;
    case 's': case 'S': k[0] = CP_DOWN; return 1;
    case 'a': case 'A': k[0] = CP_LEFT; return 1;
    case 'd': case 'D': k[0] = CP_RIGHT; return 1;
    case 'k': case 'K': case ' ': k[0] = CP_B1; return 1;
    case 'j': case 'J': k[0] = CP_B2; return 1;
    case 'u': case 'U': k[0] = CP_B3; return 1;
    case 'i': case 'I': k[0] = CP_B4; return 1;
    case '\r': k[0] = CP_START; return 1;
    default: return 0;
    }
}

static void key(int code, bool down) { if (down) typed |= (unsigned)code; }
static void keys_clear(void) { typed = 0; }

static void input(const comp_cyd_in *in)
{
    unsigned b = 0;
    if (in->zones & (1 << 1)) b |= CP_B3;
    if (in->zones & (1 << 2)) b |= CP_B4;
    if (in->zones & (1 << 4) || in->left) b |= CP_LEFT;
    if (in->zones & (1 << 5) || in->up) b |= CP_UP;
    if (in->zones & (1 << 6) || in->down) b |= CP_DOWN;
    if (in->zones & (1 << 7) || in->right) b |= CP_RIGHT;
    if (in->zones & (1 << 8)) b |= CP_START;
    if (in->zones & (1 << 9) || in->fire2) b |= CP_B2;
    if (in->zones & ((1 << 10) | (1 << 11)) || in->fire) b |= CP_B1;
    if (select_frames > 0) { select_frames--; b |= CP_SELECT; }
    held = b;
    typed = 0;
}

static int frame(const int16_t **audio)
{
    cons_pad(0, held | typed);
    return cons_run_frame(audio);
}

static void render(int y, uint16_t *row)
{
    int w, h, stride;
    const uint16_t *fb = cons_frame(&w, &h, &stride);
    const int s = (w * 2 <= 320 && h * 2 <= 240) ? 2 : 1;
    const int xs = w > 320 ? (w + 319) / 320 : 1, ys = h > 240 ? (h + 239) / 240 : 1;
    const int sw = w * s / xs, sh = h * s / ys;
    const int ox = (320 - sw) / 2, oy = (240 - sh) / 2;
    const int py = y - oy;
    if (py < 0 || py >= sh) {
        memset(row, 0, 320 * sizeof *row);
        return;
    }
    if (ox > 0) memset(row, 0, 320 * sizeof *row);
    const uint16_t *src = fb + (py / s) * ys * stride;
    uint16_t *d = row + (ox > 0 ? ox : 0);
    const int cw = sw < 320 ? sw : 320;
    for (int x = 0; x < cw; x++) {
        const uint16_t p = src[(x / s) * xs];
        d[x] = rgb565((uint8_t)((p >> 8) & 0xF8), (uint8_t)((p >> 3) & 0xFC), (uint8_t)(p << 3));
    }
}

static comp_cyd_machine kMachine = {
    "cons", "cons", "", "MEDIA: SELECT / HOLD=SD", { 120, 200, 255 },
    NULL, "", 0, load, item, item_do, tap, input, frame, render,
    { NULL, "BUTTON 3", "BUTTON 4", NULL, "LEFT", "UP", "DOWN", "RIGHT", "START", "BUTTON 2", "BUTTON 1", "BUTTON 1" },
    char_keys, key, keys_clear, media_name, NULL,
};

void app_main(void)
{
    static cyd_game_t info;
    info = (cyd_game_t){ cons_machine.id, cons_machine.title, "MEDIA: SELECT / HOLD=SD", { 120, 200, 255 } };
    cyd_boot(&info);
    if (!cons_init()) {
        ESP_LOGE("cons", "out of memory for %s", cons_machine.title);
        abort();
    }
    kMachine.tag = cons_machine.id;
    kMachine.nvs_ns = cons_machine.id;
    kMachine.title = cons_machine.title;
    kMachine.exts = cons_machine.exts;
    kMachine.ext_label = cons_machine.media_label;
    kMachine.audio_rate = cons_machine.audio_rate;
    if (cons_machine.button[0]) kMachine.zone_label[10] = kMachine.zone_label[11] = cons_machine.button[0];
    if (cons_machine.button[1]) kMachine.zone_label[9] = cons_machine.button[1];
    kMachine.zone_label[1] = cons_machine.button[2];
    kMachine.zone_label[2] = cons_machine.button[3];
    if (cons_machine.start) kMachine.zone_label[8] = cons_machine.start;
    comp_cyd_start(&kMachine);
}
