// nes_ppu.c - Ricoh 2C02 PPU: a C11 port of dsp-cpp's video/nes_ppu
// (nes_ppu.pas); see nes_ppu.h.
#include <math.h>
#include <string.h>
#include "nes_ppu.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint16_t pal565[8][64];      // [emphasis bits 5-7 of $2001][colour]

static const uint8_t kMirror[9][4] = {
    {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 1, 0, 1}, {0, 0, 0, 0}, {1, 1, 1, 1},
    {0, 1, 2, 3}, {1, 1, 0, 0}, {0, 0, 0, 1}, {0, 1, 1, 1},
};

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void nes_ppu_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    static const float kBright[3][4] = {
        {0.50f, 0.75f, 1.00f, 1.00f}, {0.29f, 0.45f, 0.73f, 0.90f}, {0.00f, 0.24f, 0.47f, 0.77f},
    };
    const float tint = 0.22f, hue = 287.0f, Kr = 0.2989f, Kb = 0.1145f, Ku = 2.029f, Kv = 1.140f;
    int pos = 0;
    for (int in = 0; in < 4; in++)
        for (int c = 0; c < 16; c++) {
            float sat = 0, rad = 0, y;
            if (c == 0) y = kBright[0][in];
            else if (c == 13) y = kBright[2][in];
            else if (c == 14 || c == 15) y = 0;
            else {
                sat = tint;
                rad = (float)M_PI * (((float)c * 30.0f + hue) / 180.0f);
                y = kBright[1][in];
            }
            const float u = sat * cosf(rad), v = sat * sinf(rad);
            const float r = (y + Kv * v) * 255.0f;
            const float g = (y - (Kb * Ku * u + Kr * Kv * v) / (1.0f - Kb - Kr)) * 255.0f;
            const float b = (y + Ku * u) * 255.0f;
            const int R0 = clamp8((int)floorf(r + 0.5f)), G0 = clamp8((int)floorf(g + 0.5f)), B0 = clamp8((int)floorf(b + 0.5f));
            for (int e = 0; e < 8; e++) {
                int R = R0, G = G0, B = B0;
                if (e & 4) { R = R * 3 / 4; G = G * 3 / 4; }     // blue
                if (e & 2) { R = R * 3 / 4; B = B * 3 / 4; }     // green
                if (e & 1) { G = G * 3 / 4; B = B * 3 / 4; }     // red
                pal565[e][pos] = (uint16_t)(((R & 0xf8) << 8) | ((G & 0xfc) << 3) | (B >> 3));
            }
            pos++;
        }
}

void nes_ppu_reset(nes_ppu *p)
{
    p->control1 = p->control2 = 0;
    p->pal_mask = 0x3f;
    p->status = 0;
    p->sprite_ram_pos = 0;
    p->address = p->address_temp = 0;
    p->dir_first = true;
    p->sprite0_hit = p->sprite_over_flow = false;
    p->sprite_size = 8;
    p->pos_bg = 0;
    p->pos_spt = 1;
    p->disable_chr = false;
    p->buffer_read = 0;
    p->tile_x_offset = 0;
    p->linea = 0;
    memset(p->pal_ram, 0, sizeof p->pal_ram);
    memset(p->sprite_ram, 0, sizeof p->sprite_ram);
}

static inline int pal_index(uint16_t a)
{
    int i = a & 0x1f;
    if ((i & 0x13) == 0x10) i &= 0x0f;
    return i;
}

static inline int nt_index(const nes_ppu *p, uint16_t a)
{
    const int m = (p->mirror >= 1 && p->mirror <= 8) ? p->mirror : NM_VERTICAL;
    return kMirror[m][(a >> 10) & 3];
}

static inline uint8_t chr_read(const nes_ppu *p, uint16_t a)
{
    if (p->disable_chr) return (uint8_t)(a & 0xff);
    const int half = (a >> 12) & 1;
    const int bank = p->chr_map ? (p->chr_map[half] & 3) : half;
    return p->chr[bank][a & 0xfff];
}

static inline uint8_t read_mem(const nes_ppu *p, uint16_t a)
{
    a &= 0x3fff;
    if (a <= 0x1fff) return chr_read(p, a);
    if (a <= 0x3eff) return p->name_table[nt_index(p, a)][a & 0x3ff];
    return p->pal_ram[pal_index(a)];
}

void nes_ppu_end_y_coarse(nes_ppu *p)
{
    if (!(p->control2 & 0x18)) return;
    p->address = (uint16_t)(p->address + 0x1000);
    if (p->line_ack) p->line_ack(p->user, false);
    if (p->address & 0x8000) {
        const uint16_t tmp = (uint16_t)((p->address & 0x03e0) + 0x20);
        p->address &= 0x7c1f;
        if (tmp == 0x03c0) p->address ^= 0x0800;
        else p->address |= tmp & 0x03e0;
    }
    p->address = (uint16_t)((p->address & 0x7be0) | (p->address_temp & 0x41f));
}

static void advance_vram(nes_ppu *p)
{
    if (p->linea >= 240 || !(p->control2 & 0x18)) {
        p->address = (uint16_t)((p->address + ((p->control1 & 0x04) ? 32 : 1)) & 0x7fff);
    } else {
        if ((p->address & 0x1f) == 0x1f) p->address ^= 0x41f;
        else p->address++;
        nes_ppu_end_y_coarse(p);
    }
}

uint8_t nes_ppu_read(nes_ppu *p)
{
    const uint16_t a = p->address & 0x3fff;
    uint8_t ret;
    if (a >= 0x3f00) {
        ret = (uint8_t)((read_mem(p, a) & 0x3f) | (p->open_bus & 0xc0));
        p->buffer_read = read_mem(p, (uint16_t)(a - 0x1000));
    } else {
        ret = p->disable_chr ? (uint8_t)(p->address & 0xff) : p->buffer_read;
        p->buffer_read = read_mem(p, a);
    }
    advance_vram(p);
    return ret;
}

void nes_ppu_write(nes_ppu *p, uint8_t value)
{
    const uint16_t a = p->address & 0x3fff;
    if (a <= 0x1fff) {
        if (!p->disable_chr && p->write_chr) {
            const int half = (a >> 12) & 1;
            const int bank = p->chr_map ? (p->chr_map[half] & 3) : half;
            p->chr[bank][a & 0xfff] = value;
        }
    } else if (a <= 0x3eff) {
        p->name_table[nt_index(p, a)][a & 0x3ff] = value;
    } else {
        p->pal_ram[pal_index(a)] = value & 0x3f;
    }
    advance_vram(p);
}

void nes_ppu_dma(nes_ppu *p, const uint8_t *page)
{
    const int pos = p->sprite_ram_pos;
    memcpy(p->sprite_ram + pos, page, (size_t)(0x100 - pos));
    if (pos) memcpy(p->sprite_ram, page, (size_t)pos);     // (as dsp-cpp: from the page's start)
}

static void eval_sprites(nes_ppu *p, int line, uint8_t *spr, bool *behind, bool *zero)
{
    int found = 0;
    for (int f = 0; f < 64; f++) {
        const uint8_t pos_y = p->sprite_ram[f * 4];
        const int row = line - ((int)pos_y + 1);
        if (row < 0 || row >= p->sprite_size) continue;
        if (++found > 8) {
            p->status |= 0x20;
            break;
        }
        const uint8_t tile = p->sprite_ram[f * 4 + 1], attr = p->sprite_ram[f * 4 + 2], pos_x = p->sprite_ram[f * 4 + 3];
        const bool flipx = (attr & 0x40) != 0, flipy = (attr & 0x80) != 0;
        uint16_t pattern;
        if (p->sprite_size == 8) {
            const int y = flipy ? 7 - row : row;
            pattern = (uint16_t)(p->pos_spt * 0x1000 + tile * 16 + y);
        } else {
            const int y = flipy ? 15 - row : row;
            const int t = (tile & 0xfe) + (y >> 3);
            pattern = (uint16_t)((tile & 1) * 0x1000 + t * 16 + (y & 7));
        }
        const uint8_t lo = read_mem(p, pattern);
        if (p->ppu_read) p->ppu_read(p->user, pattern);
        const uint8_t hi = read_mem(p, (uint16_t)(pattern + 8));
        if (p->ppu_read) p->ppu_read(p->user, (uint16_t)(pattern + 8));
        const uint8_t palette = (uint8_t)(0x10 | ((attr & 0x03) << 2));
        for (int i = 0; i < 8; i++) {
            const int bit = flipx ? i : 7 - i;
            const uint8_t color = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            const int px = pos_x + i;
            if (color == 0 || px >= NES_W) continue;
            if (!(p->control2 & 0x04) && px < 8) continue;
            if (spr[px]) continue;
            spr[px] = (uint8_t)(palette | color);
            behind[px] = (attr & 0x20) != 0;
            zero[px] = f == 0;
        }
    }
}

static void put_background(nes_ppu *p, uint8_t *bg)
{
    uint8_t scratch[33 * 8];
    uint16_t attrib_table = (uint16_t)(0x2000 + (p->address & 0xc00) + 0x3c0 +
                                       ((((p->address & 0x3e0) / 0x20) & 0xfffc) * 2) + ((p->address & 0x1f) / 4));
    int pos_x = 0;
    const int tile_y = (p->address & 0x7000) >> 12;
#define ATTR() ((uint8_t)(((read_mem(p, attrib_table) >> (((p->address & 0x40) ? 4 : 0) + ((p->address & 0x02) ? 2 : 0))) & 3) << 2))
    uint8_t attrib_val = ATTR();
    for (int tiles = 0; tiles < 33; tiles++) {
        const uint16_t pattern = (uint16_t)(p->pos_bg * 0x1000 + read_mem(p, (uint16_t)(0x2000 + (p->address & 0xfff))) * 16 + tile_y);
        if (p->ppu_read) p->ppu_read(p->user, pattern);
        const uint8_t lo = read_mem(p, pattern), hi = read_mem(p, (uint16_t)(pattern + 8));
        for (int x = 7; x >= 0; x--) {
            const uint8_t col = (uint8_t)(((lo >> x) & 1) | (((hi >> x) & 1) << 1));
            scratch[pos_x++] = col ? (uint8_t)(attrib_val | col) : 0;
        }
        if ((p->address & 0x1f) == 0x1f) {
            attrib_table = (uint16_t)((attrib_table ^ 0x400) - 8);
            p->address ^= 0x41f;
        } else {
            p->address++;
        }
        if (!(p->address & 0x03)) attrib_table++;
        if (!(p->address & 0x01)) attrib_val = ATTR();
    }
#undef ATTR
    memcpy(bg, scratch + p->tile_x_offset, NES_W);
    if (!(p->control2 & 0x02)) memset(bg, 0, 8);
}

void nes_ppu_draw_line(nes_ppu *p, int line, uint16_t *out)
{
    uint8_t bg[NES_W], spr[NES_W];
    bool behind[NES_W], zero[NES_W];
    memset(bg, 0, sizeof bg);
    memset(spr, 0, sizeof spr);
    memset(behind, 0, sizeof behind);
    memset(zero, 0, sizeof zero);
    const bool show_bg = (p->control2 & 0x08) != 0, show_spr = (p->control2 & 0x10) != 0;
    if (show_bg) put_background(p, bg);
    if (show_spr) eval_sprites(p, line, spr, behind, zero);
    uint8_t backdrop = p->pal_ram[0];
    if (!show_bg && !show_spr && (p->address & 0x3f00) == 0x3f00) backdrop = p->pal_ram[pal_index(p->address)];
    const uint16_t *pal = pal565[p->control2 >> 5];
    for (int x = 0; x < NES_W; x++) {
        if (zero[x] && bg[x] && show_bg && show_spr && x != 255) p->status |= 0x40;
        uint8_t v;
        if (spr[x] && (!behind[x] || !bg[x])) v = p->pal_ram[spr[x] & 0x1f];
        else if (bg[x]) v = p->pal_ram[bg[x]];
        else v = backdrop;
        out[x] = pal[v & p->pal_mask];
    }
}
