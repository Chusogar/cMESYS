// gb_ppu.c - Game Boy picture: a C11 port of dsp-cpp's video/gb_ppu; see gb_ppu.h.
#include <math.h>
#include <string.h>
#include "gb_ppu.h"

// gb.pas's green LCD: 9bbc0f 8bac0f 306230 0f380f
#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))
static const uint16_t kGreen[4] = {
    RGB565(0x9b, 0xbc, 0x0f), RGB565(0x8b, 0xac, 0x0f), RGB565(0x30, 0x62, 0x30), RGB565(0x0f, 0x38, 0x0f),
};

typedef struct { uint8_t oam_index, y, x, tile, attr; } sprite_hit;

void gb_ppu_reset(gb_ppu *p, bool is_cgb)
{
    p->is_cgb = is_cgb;
    p->oam_dma = false;
    memset(p->vram, 0, sizeof p->vram);
    memset(p->oam, 0, sizeof p->oam);
    p->lcdc = 0x91;
    p->scy = p->scx = p->wy = p->wx = 0;
    p->bgp = 0xfc;
    p->obp0 = p->obp1 = 0xff;
    p->vbk = 0;
    p->bgp_index = p->obp_index = 0;
    p->bgp_inc = p->obp_inc = false;
    for (int i = 0; i < 32; i++) {
        p->bgc_pal[i] = 0x7fff;
        p->spc_pal[i] = 0;
    }
    p->window_line = 0;
    memset(p->scroll_y, 0, sizeof p->scroll_y);
    p->scroll_y_pos = 0;
}

void gb_ppu_set_scy(gb_ppu *p, uint8_t v)
{
    p->scy = v;
    memset(p->scroll_y, v, sizeof p->scroll_y);
    p->scroll_y_pos = 0;
}

void gb_ppu_write_scy_mid_line(gb_ppu *p, uint8_t v, int sample)
{
    if (sample < 0) sample = 0;
    if (sample > 256) sample = 256;
    for (int i = p->scroll_y_pos; i < sample && i < 256; i++) p->scroll_y[i] = v;
    p->scroll_y_pos = sample > 255 ? 255 : sample;
    p->scy = v;
}

static void finish_scroll_y(gb_ppu *p)
{
    for (int i = p->scroll_y_pos; i <= 113 && i < 256; i++) p->scroll_y[i] = p->scy;
    p->scroll_y_pos = 0;
}

static uint8_t scy_for_map_column(const gb_ppu *p, int map_col)
{
    int idx = (int)lround((double)map_col * 3.5625);
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    return p->scroll_y[idx];
}

uint8_t gb_ppu_read_bg_pal(const gb_ppu *p)
{
    const uint16_t e = p->bgc_pal[p->bgp_index >> 1];
    return (p->bgp_index & 1) ? (uint8_t)(e >> 8) : (uint8_t)e;
}

void gb_ppu_write_bg_pal(gb_ppu *p, uint8_t v, bool locked)
{
    if (!locked) {
        uint16_t *e = &p->bgc_pal[p->bgp_index >> 1];
        *e = (p->bgp_index & 1) ? (uint16_t)((*e & 0x00ff) | (v << 8)) : (uint16_t)((*e & 0xff00) | v);
    }
    if (p->bgp_inc) p->bgp_index = (p->bgp_index + 1) & 0x3f;
}

uint8_t gb_ppu_read_obj_pal(const gb_ppu *p)
{
    const uint16_t e = p->spc_pal[p->obp_index >> 1];
    return (p->obp_index & 1) ? (uint8_t)(e >> 8) : (uint8_t)e;
}

void gb_ppu_write_obj_pal(gb_ppu *p, uint8_t v, bool locked)
{
    if (!locked) {
        uint16_t *e = &p->spc_pal[p->obp_index >> 1];
        *e = (p->obp_index & 1) ? (uint16_t)((*e & 0x00ff) | (v << 8)) : (uint16_t)((*e & 0xff00) | v);
    }
    if (p->obp_inc) p->obp_index = (p->obp_index + 1) & 0x3f;
}

bool gb_ppu_window_visible_on(const gb_ppu *p, int ly)
{
    return (p->lcdc & 0x20) && ly >= p->wy && p->wx <= 166 && p->wx != 0;
}

static inline uint16_t dmg_color(uint8_t pal, uint8_t idx) { return kGreen[(pal >> (idx * 2)) & 3]; }

static inline uint16_t cgb_color(const uint16_t *pal, uint8_t entry)
{
    const uint16_t v = pal[entry & 0x1f];
    // (r, g, b) << 3 as dsp-cpp, then RGB565
    return (uint16_t)(((v & 0x1f) << 11) | (((v >> 5) & 0x1f) << 6) | ((v >> 10) & 0x1f));
}

static int gather_sprites(const gb_ppu *p, int ly, sprite_hit *out)
{
    const int size = (p->lcdc & 4) ? 16 : 8;
    int count = 0;
    for (int i = 0; i < 40 && count < 10; i++) {
        const uint8_t y = p->oam[i * 4];
        if (y == 0 || y >= 160) continue;
        const int lis = ly - ((int)y - 16);
        if (lis < 0 || lis >= size) continue;
        out[count].oam_index = (uint8_t)i;
        out[count].y = y;
        out[count].x = p->oam[i * 4 + 1];
        out[count].tile = p->oam[i * 4 + 2];
        out[count].attr = p->oam[i * 4 + 3];
        count++;
    }
    if (!p->is_cgb)                     // DMG: by X, ties in OAM order (a stable sort)
        for (int i = 1; i < count; i++) {
            const sprite_hit h = out[i];
            int j = i - 1;
            while (j >= 0 && out[j].x > h.x) {
                out[j + 1] = out[j];
                j--;
            }
            out[j + 1] = h;
        }
    return count;
}

int gb_ppu_count_sprites_on_line(const gb_ppu *p, int ly)
{
    if (!(p->lcdc & 2)) return 0;
    sprite_hit h[10];
    return gather_sprites(p, ly, h);
}

int gb_ppu_sprite_mode3_penalty(const gb_ppu *p, int ly, int speed)
{
    sprite_hit h[10];
    const int count = gather_sprites(p, ly, h);
    return count * (p->is_cgb ? (8 >> (speed & 1)) : 8);
}

static void cgb_tile_pixel(const gb_ppu *p, uint16_t map_base, int px, int py, uint8_t *index, uint8_t *attr)
{
    px &= 0xff;
    py &= 0xff;
    const uint16_t map_addr = (uint16_t)((map_base + (py / 8) * 32 + (px / 8)) & 0x1fff);
    const uint8_t tile_num = gb_ppu_vram_read_bank(p, 0, map_addr);
    const uint8_t a = gb_ppu_vram_read_bank(p, 1, map_addr);
    const bool fx = (a & 0x20) != 0, fy = (a & 0x40) != 0;
    const int row = fy ? 7 - (py & 7) : (py & 7);
    const int col = fx ? 7 - (px & 7) : (px & 7);
    const int n2 = !(p->lcdc & 0x10) ? (int)(int8_t)tile_num : (int)tile_num;
    const int tile_base = !(p->lcdc & 0x10) ? 0x1000 : 0;
    const int bank = (a & 0x08) ? 1 : 0;
    const uint16_t addr = (uint16_t)((n2 * 16 + tile_base + row * 2) & 0x1fff);
    const uint8_t lo = gb_ppu_vram_read_bank(p, bank, addr), hi = gb_ppu_vram_read_bank(p, bank, (uint16_t)((addr + 1) & 0x1fff));
    const int bit = 7 - col;
    *index = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
    *attr = a;
}

static void draw_cgb_sprites(const gb_ppu *p, int ly, uint16_t *out, uint8_t *bg_prio, uint8_t pri)
{
    sprite_hit hits[10];
    const int count = gather_sprites(p, ly, hits);
    const int size = (p->lcdc & 4) ? 16 : 8;
    for (int i = count - 1; i >= 0; i--) {
        const sprite_hit *s = &hits[i];
        if ((s->attr & 0x80) != pri) continue;
        const int sx = (int)s->x - 8;
        if (s->x == 0 || s->x >= 168) continue;
        const bool fx = (s->attr & 0x20) != 0, fy = (s->attr & 0x40) != 0;
        const int lis = ly - ((int)s->y - 16);
        int row = fy ? size - 1 - lis : lis;
        uint8_t tile = s->tile;
        if (size == 16) {
            if (fy) tile = (uint8_t)((tile & 0xfe) + ((~(uint8_t)(lis >> 3)) & 1));
            else tile = (uint8_t)((tile & 0xfe) + ((lis >> 3) & 1));
            row &= 7;
        }
        const int bank = (s->attr & 0x08) ? 1 : 0;
        const uint16_t ta = (uint16_t)(tile * 16 + row * 2);
        const uint8_t lo = gb_ppu_vram_read_bank(p, bank, ta), hi = gb_ppu_vram_read_bank(p, bank, (uint16_t)(ta + 1));
        const uint8_t pal = (uint8_t)((s->attr & 7) * 4);
        for (int col = 0; col < 8; col++) {
            const int px = sx + col, bit = fx ? col : 7 - col;
            const uint8_t idx = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            if (!idx) continue;
            const uint8_t pi = (uint8_t)(px & 0xff);
            if (bg_prio[pi] & 4) continue;
            if (px >= 0 && px < GB_W) out[px] = cgb_color(p->spc_pal, (uint8_t)((idx + pal) & 0x1f));
            bg_prio[pi] |= 1;
        }
    }
}

static void render_cgb(const gb_ppu *p, int ly, uint16_t *out)
{
    for (int x = 0; x < GB_W; x++) out[x] = 0;
    if (!(p->lcdc & 0x80)) return;
    uint8_t bg_prio[256], bg_index[GB_W], bg_attr[GB_W];
    memset(bg_prio, 0, sizeof bg_prio);
    const uint16_t bg_map = (uint16_t)(0x1800 + ((p->lcdc & 0x08) ? 0x400 : 0));
    for (int x = 0; x < GB_W; x++) {
        const int bx = (x + p->scx) & 0xff;
        const int my = (ly + scy_for_map_column(p, bx / 8)) & 0xff;
        uint8_t idx = 0, attr = 0;
        cgb_tile_pixel(p, bg_map, bx, my, &idx, &attr);
        bg_index[x] = idx;
        bg_attr[x] = attr;
        out[x] = cgb_color(p->bgc_pal, (uint8_t)(idx + (attr & 7) * 4));
        if ((attr & 0x80) && idx) bg_prio[x] |= 4;
    }
    const bool window_here = gb_ppu_window_visible_on(p, ly);
    const uint16_t win_map = (uint16_t)(0x1800 + ((p->lcdc & 0x40) ? 0x400 : 0));
    if (window_here) {
        const int wo = (int)p->wx - 7;
        for (int x = 0; x < GB_W; x++) {
            if (x < wo) continue;
            uint8_t idx = 0, attr = 0;
            cgb_tile_pixel(p, win_map, x - wo, p->window_line, &idx, &attr);
            out[x] = cgb_color(p->bgc_pal, (uint8_t)(idx + (attr & 7) * 4));
            bg_prio[x] |= 2;
            bg_index[x] = idx;
            bg_attr[x] = attr;
        }
    }
    const bool sprites_on = (p->lcdc & 2) && !p->oam_dma;
    if (sprites_on) draw_cgb_sprites(p, ly, out, bg_prio, 0x80);
    if (p->lcdc & 1) {
        for (int x = 0; x < GB_W; x++) {
            const bool in_window = window_here && x >= (int)p->wx - 7;
            const uint8_t idx = bg_index[x], pal = (uint8_t)((bg_attr[x] & 7) * 4);
            if (in_window) {
                if (idx) out[x] = cgb_color(p->bgc_pal, (uint8_t)(idx + pal));
                else if (bg_prio[x] == 0) out[x] = cgb_color(p->bgc_pal, pal);
            } else if (idx && !(bg_prio[x] & 2)) {
                out[x] = cgb_color(p->bgc_pal, (uint8_t)(idx + pal));
            }
        }
    }
    if (sprites_on) draw_cgb_sprites(p, ly, out, bg_prio, 0);
}

static void render_dmg(const gb_ppu *p, int ly, uint16_t *out)
{
    if (!(p->lcdc & 0x80)) {
        for (int x = 0; x < GB_W; x++) out[x] = kGreen[0];
        return;
    }
    uint8_t bg_index[GB_W];
    memset(bg_index, 0, sizeof bg_index);
    const bool window_here = gb_ppu_window_visible_on(p, ly), bg_enabled = (p->lcdc & 1) != 0;
    if (bg_enabled) {
        for (int x = 0; x < GB_W; x++) {
            const bool use_window = window_here && x >= p->wx - 7;
            int mx, my;
            uint16_t mb;
            if (use_window) {
                mx = x - (p->wx - 7);
                my = p->window_line;
                mb = (uint16_t)(0x1800 + ((p->lcdc & 0x40) ? 0x400 : 0));
            } else {
                mx = (x + p->scx) & 0xff;
                my = (ly + scy_for_map_column(p, mx / 8)) & 0xff;
                mb = (uint16_t)(0x1800 + ((p->lcdc & 0x08) ? 0x400 : 0));
            }
            const uint16_t ma = (uint16_t)(mb + (my / 8) * 32 + (mx / 8));
            const uint8_t tn = gb_ppu_vram_read_bank(p, 0, ma);
            const int row = my & 7, col = mx & 7;
            const uint16_t ta = (p->lcdc & 0x10) ? (uint16_t)(tn * 16) : (uint16_t)(0x1000 + (int8_t)tn * 16);
            const uint8_t lo = gb_ppu_vram_read_bank(p, 0, (uint16_t)(ta + row * 2));
            const uint8_t hi = gb_ppu_vram_read_bank(p, 0, (uint16_t)(ta + row * 2 + 1));
            const int bit = 7 - col;
            bg_index[x] = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        }
    }
    uint8_t obj_index[GB_W], obj_attr[GB_W];
    bool obj_present[GB_W];
    memset(obj_present, 0, sizeof obj_present);
    if ((p->lcdc & 2) && !p->oam_dma) {
        sprite_hit hits[10];
        const int count = gather_sprites(p, ly, hits);
        const int size = (p->lcdc & 4) ? 16 : 8;
        for (int i = count - 1; i >= 0; i--) {
            const sprite_hit *s = &hits[i];
            const int sx = (int)s->x - 8;
            if (sx <= -8 || sx >= GB_W) continue;
            const bool fx = (s->attr & 0x20) != 0, fy = (s->attr & 0x40) != 0;
            const int lis = ly - ((int)s->y - 16);
            const int row = fy ? size - 1 - lis : lis;
            uint8_t tile = s->tile;
            if (size == 16) tile = (uint8_t)((tile & 0xfe) + (row >= 8 ? 1 : 0));
            const uint16_t ta = (uint16_t)(tile * 16 + (row & 7) * 2);
            const uint8_t lo = gb_ppu_vram_read_bank(p, 0, ta), hi = gb_ppu_vram_read_bank(p, 0, (uint16_t)(ta + 1));
            for (int col = 0; col < 8; col++) {
                const int px = sx + col;
                if (px < 0 || px >= GB_W) continue;
                const int bit = fx ? col : 7 - col;
                const uint8_t idx = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
                if (!idx) continue;
                obj_index[px] = idx;
                obj_attr[px] = s->attr;
                obj_present[px] = true;
            }
        }
    }
    for (int x = 0; x < GB_W; x++) {
        const uint8_t bidx = bg_enabled ? bg_index[x] : 0;
        if (obj_present[x] && !((obj_attr[x] & 0x80) && bidx)) {
            out[x] = dmg_color((obj_attr[x] & 0x10) ? p->obp1 : p->obp0, obj_index[x]);
            continue;
        }
        out[x] = dmg_color(p->bgp, bidx);
    }
}

void gb_ppu_render_scanline(gb_ppu *p, int ly, uint16_t *out)
{
    finish_scroll_y(p);
    if (p->is_cgb) render_cgb(p, ly, out);
    else render_dmg(p, ly, out);
}
