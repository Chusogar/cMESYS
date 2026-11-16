// gen_core.c - Sega Mega Drive / Genesis (1988): the console API over
// md_machine (a C11 migration of dsp-cpp's drivers/consoles/genesis). The
// picture is 320 wide (H32 modes centred, as dsp-cpp) by 224 / 240 lines.
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "md_machine.h"

static const char *const kExts[] = { ".md", ".gen", ".bin", ".smd", NULL };
const cons_info cons_machine = {
    "genesis", "Sega Mega Drive / Genesis", "Sega, 1988", kExts, "MD BIN", MD_VDP_W, MD_VDP_H,
    MD_FPS_NTSC, MD_RATE, { "A", "B", "C", NULL }, "START", NULL, CONS_NO_KEYS, 2, NULL,
};

static uint16_t *fb;

bool cons_init(void)
{
    fb = (uint16_t *)calloc(MD_VDP_W * MD_VDP_H, sizeof(uint16_t));
    if (!fb || !md_alloc()) return false;
    MD.fb = fb;
    md_init(MD_USA);
    md_reset();
    return true;
}

void cons_reset(void)
{
    memset(fb, 0, MD_VDP_W * MD_VDP_H * sizeof(uint16_t));
    md_reset();
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    // the region from the header ('J' / 'U' / 'E' at $1F0): Japan / USA / Europe
    int region = MD_USA;
    if (data && size > 0x1f3) {
        const char *r = (const char *)data + 0x1f0;
        if (memchr(r, 'U', 3) || memchr(r, '4', 1)) region = MD_USA;
        else if (memchr(r, 'J', 3) || r[0] == '1') region = MD_JAPAN;
        else if (memchr(r, 'E', 3) || r[0] == '8') region = MD_EUROPE;
    }
    md_set_region(region);
    return md_load(data, size, why);
}

void cons_eject(void) { md_eject(); }
bool cons_loaded(void) { return MD.loaded; }

int cons_run_frame(const int16_t **audio)
{
    *audio = MD.audio;
    if (!MD.loaded) {
        MD.naudio = (int)(MD_RATE / md_fps());
        memset(MD.audio, 0, MD.naudio * sizeof(int16_t));
        return MD.naudio;
    }
    return md_run_frame();
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = MD_VDP_W;
    *h = md_vdp_height(&MD.vdp);
    *stride = MD_VDP_W;
    return fb;
}

void cons_pad(int player, unsigned bits)
{
    if (player < 0 || player > 1) return;
    MD.pad[player] = bits;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return MD.pal ? "PAL 50 Hz" : NULL; }
