// sega16d.c - see sega16d.h. Translated from dsp-cpp's video/sega16.
#include <stdlib.h>
#include <string.h>
#include "sega16d.h"
#include "../drivers/arcades/common/arc.h"

#define S16MAX(a, b) ((a) > (b) ? (a) : (b))
#define S16MIN(a, b) ((a) < (b) ? (a) : (b))

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) { return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
uint32_t s16_argb(uint8_t r, uint8_t g, uint8_t b) { return pack_rgb(r, g, b); }

uint32_t s16_mix_shadow(uint32_t dest, uint32_t shadow)
{
    const uint32_t r = (((dest >> 16) & 0xff) + ((shadow >> 16) & 0xff)) / 2;
    const uint32_t g = (((dest >> 8) & 0xff) + ((shadow >> 8) & 0xff)) / 2;
    const uint32_t b = ((dest & 0xff) + (shadow & 0xff)) / 2;
    return pack_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static void put_sprite_pixel(uint32_t *dest, int x, int y, uint32_t color, uint32_t shadow_color, bool is_shadow)
{
    if (x < 0 || x >= 320 || y < 0 || y >= 224) return;
    uint32_t *pixel = &dest[(size_t)(y * 320 + x)];
    *pixel = is_shadow ? s16_mix_shadow(*pixel, shadow_color) : color;
}

static int tile_code_16a(uint16_t data) { return (int)(((data >> 1) & 0x1000) | (data & 0xfff)); }

static inline const uint8_t *s16_tile(const s16video *v, int n)
{
    return v->tiles + (size_t)(((n % v->tiles_total) + v->tiles_total) % v->tiles_total) * 64;
}

void s16_build_palette_luts(uint8_t normal[32], uint8_t shadow[32], uint8_t hilight[32])
{
    const arc_resnet n = {6, {3900, 2000, 1000, 500, 250, 0}, 0, 0}, sh = {6, {3900, 2000, 1000, 500, 250, 470}, 0, 0};
    double wn[1][8], ws[1][8];
    arc_resistor_weights(0, 255, -1.0, &n, 1, wn);
    arc_resistor_weights(0, 255, -1.0, &sh, 1, ws);
    for (int f = 0; f < 32; f++) {
        normal[f] = (uint8_t)arc_combine(wn[0], 6, (unsigned)f);
        shadow[f] = (uint8_t)arc_combine(ws[0], 6, (unsigned)f);
        hilight[f] = (uint8_t)arc_combine(ws[0], 6, (unsigned)f | 0x20);
    }
}

uint8_t *s16_decode_tiles(const uint8_t *rom, int n)
{
    static const int x[8] = {0, 1, 2, 3, 4, 5, 6, 7}, y[8] = {0, 8, 16, 24, 32, 40, 48, 56};
    const arc_layout l = {8, 8, n * 0x1000, 3, 64, false, false, false, {n * 0x10000 * 8, n * 0x8000 * 8, 0}, x, y};
    return arc_gfx_decode(&l, rom, (size_t)n * 0x18000);
}

void s16_init(s16video *v)
{
    memset(v, 0, sizeof *v);
    v->bank_size = 0x1000;
    v->cram_words = 0x800;
    s16_build_palette_luts(v->normal, v->shadow, v->hilight);
    s16_reset(v);
}

void s16_reset(s16video *v)
{
    memset(v->pal_ram, 0, sizeof v->pal_ram);
    memset(v->tile_ram, 0, sizeof v->tile_ram);
    memset(v->char_ram, 0, sizeof v->char_ram);
    memset(v->sprite_ram, 0, sizeof v->sprite_ram);
    memset(v->palette, 0, sizeof v->palette);
    memset(v->screens, 0, sizeof v->screens);
    for (int i = 0; i < 8; i++) v->tile_bank[i] = (uint8_t)i;
    for (int i = 0; i < 16; i++) v->sprite_bank[i] = (uint8_t)i;
    memset(v->tile_dirty, 1, sizeof v->tile_dirty);
    memset(v->text_dirty, 1, sizeof v->text_dirty);
    v->screen_enabled = true;
}

int s16_resolve_tile_code(const s16video *v, int code) {
    const int size = S16MAX(v->bank_size, 1);
    const int slot = (code / size) & 7;
    return (int)(v->tile_bank[(size_t)(slot)]) * size + (code % size);
}


void s16_set_palette_entry(s16video *v, int index, uint16_t value, bool split_shadow) {
    v->pal_ram[(size_t)(index) & 0xfff] = value;
    const int r = ((value >> 12) & 1) | ((value << 1) & 0x1e);
    const int g = ((value >> 13) & 1) | ((value >> 3) & 0x1e);
    const int b = ((value >> 14) & 1) | ((value >> 7) & 0x1e);
    v->palette[(size_t)(index)] = pack_rgb(v->normal[(size_t)(r)], v->normal[(size_t)(g)], v->normal[(size_t)(b)]);
    if (split_shadow) {
        // Bit 15 picks shadow or highlight for the second bank, which sits right
        // above the colour RAM of this machine.
        const uint32_t alternate = (value & 0x8000)
                                       ? pack_rgb(v->shadow[(size_t)(r)], v->shadow[(size_t)(g)],
                                                  v->shadow[(size_t)(b)])
                                       : pack_rgb(v->hilight[(size_t)(r)], v->hilight[(size_t)(g)],
                                                  v->hilight[(size_t)(b)]);
        v->palette[(size_t)(index + v->cram_words)] = alternate;
    } else {
        v->palette[(size_t)(index + 0x800)] =
            pack_rgb(v->shadow[(size_t)(r)], v->shadow[(size_t)(g)], v->shadow[(size_t)(b)]);
        v->palette[(size_t)(index + 0x1000)] =
            pack_rgb(v->hilight[(size_t)(r)], v->hilight[(size_t)(g)], v->hilight[(size_t)(b)]);
    }
}

void s16_mark_tile(s16video *v, uint16_t word_offset) {
    const int page = word_offset >> 11;
    const int pos = word_offset & 0x7ff;
    for (int s = 0; s < 16; s++) {
        if (v->screens[(size_t)(s)] == page) v->tile_dirty[(size_t)(s)][(size_t)(pos)] = true;
    }
}

static void select_page(s16video *v, int slot, uint8_t page)
{
    if (v->screens[slot] != page) {
        v->screens[slot] = page;
        memset(v->tile_dirty[slot], 1, sizeof v->tile_dirty[slot]);
    }
}

void s16_screen_select_16b(s16video *v, uint16_t char_offset)
{
    // $740 FG, $741 BG, $742 / $743 the alternate sets; nibbles TL, TR, BL, BR
    static const struct { uint16_t addr; int base; } kRegs[4] = {{0x740, 4}, {0x741, 0}, {0x742, 12}, {0x743, 8}};
    for (int r = 0; r < 4; r++) {
        if (char_offset != kRegs[r].addr) continue;
        const uint16_t value = v->char_ram[kRegs[r].addr];
        for (int i = 0; i < 4; i++) select_page(v, kRegs[r].base + i, (uint8_t)((value >> (i * 4)) & 0xf));
    }
}

static void screen_select_16a(s16video *v, uint16_t char_offset, int mask)
{
    static const struct { uint16_t addr; int base; } kRegs[2] = {{0x74e, 0}, {0x74f, 4}};
    for (int r = 0; r < 2; r++) {
        if (char_offset != kRegs[r].addr) continue;
        const uint16_t value = v->char_ram[kRegs[r].addr];
        for (int i = 0; i < 4; i++) select_page(v, kRegs[r].base + i, (uint8_t)((value >> (12 - i * 4)) & mask));
    }
}
void s16_screen_select_16a(s16video *v, uint16_t char_offset) { screen_select_16a(v, char_offset, 7); }
void s16_screen_select_hangon(s16video *v, uint16_t char_offset) { screen_select_16a(v, char_offset, 3); }

void s16_render_tile_pages(s16video *v, uint32_t *low, uint32_t *high, int first_page, bool transparent,
                           int color_shift, int code_mask, int pri_mask, bool use_tile_bank, bool extra_code_bit) {
    memset(low, 0, sizeof(uint32_t) * S16_MAP_W * S16_MAP_H);
    if (high) memset(high, 0, sizeof(uint32_t) * S16_MAP_W * S16_MAP_H);
    // v->screens[base+0..3] = {TL, TR, BL, BR}
    const int page_x[4] = {0, 512, 0, 512};
    const int page_y[4] = {0, 0, 256, 256};
    for (int p = 0; p < 4; p++) {
        const int num = first_page + p;
        const uint16_t pos = (uint16_t)(v->screens[(size_t)(num)] * 0x800);
        for (int f = 0; f < 0x800; f++) {
            const uint16_t data = v->tile_ram[(size_t)((pos + f) & 0x7fff)];
            int nchar = extra_code_bit ? tile_code_16a(data) : (int)(data & (uint16_t)(code_mask));
            if (use_tile_bank) nchar = s16_resolve_tile_code(v, nchar);
            const int color = (data >> color_shift) & 0x7f;
            const bool pri = (data & (uint16_t)(pri_mask)) != 0;
            const int x = ((f & 0x3f) << 3) + page_x[p];
            const int y = ((f >> 6) << 3) + page_y[p];
            const uint8_t *pixels = s16_tile(v, nchar);
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    const uint8_t pen = pixels[row * 8 + col];
                    const int dx = x + col;
                    const int dy = y + row;
                    if (dx < 0 || dx >= S16_MAP_W || dy < 0 || dy >= S16_MAP_H) continue;
                    uint32_t pixel = 0u;
                    if (pen != 0 || !transparent) {
                        pixel = v->palette[(size_t)((color << 3) + pen)];
                    }
                    const size_t index = (size_t)(dy * S16_MAP_W + dx);
                    if (pen != 0 || !transparent) low[index] = pixel;
                    if (pri && pen != 0 && high) high[index] = pixel;
                }
            }
        }
    }
}

void s16_render_text(s16video *v, uint32_t *low, uint32_t *high, int color_shift, int code_mask, int pri_mask,
                     bool use_tile_bank) {
    memset(low, 0, sizeof(uint32_t) * S16_TEXT_W * 256);
    memset(high, 0, sizeof(uint32_t) * S16_TEXT_W * 256);
    for (int f = 0; f <= 0x6ff; f++) {
        const uint16_t atrib = v->char_ram[(size_t)(f)];
        const int color = (atrib >> color_shift) & 7;
        int nchar = atrib & code_mask;
        if (use_tile_bank) nchar = (int)(v->tile_bank[0]) * v->bank_size + nchar;
        const int x = (f & 0x3f) << 3;
        const int y = (f >> 6) << 3;
        const uint8_t *pixels = s16_tile(v, nchar);
        const bool pri = (atrib & (uint16_t)(pri_mask)) != 0;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = pixels[row * 8 + col];
                if (pen == 0) continue;
                const uint32_t pixel = v->palette[(size_t)((color << 3) + pen)];
                const size_t index = (size_t)((y + row) * S16_TEXT_W + (x + col));
                if (index >= (size_t)S16_TEXT_W * 256) continue;
                low[index] = pixel;
                if (pri) high[index] = pixel;
            }
        }
    }
}

void s16_draw_tilemap_16b(const s16video *v, uint32_t *dest, uint8_t *priority, int which, int category,
                          uint8_t pri_mark, bool opaque, int color_shift, int code_mask, bool use_tile_bank) {
    // MAME tilemap_16b_draw_layer: which 0 = FG (char $74c/$748/$740),
    // which 1 = BG (char $74d/$749/$741). Alternate set uses which+2 registers.
    const uint16_t xscroll = v->char_ram[(size_t)(0x74c + which)];
    const uint16_t yscroll = v->char_ram[(size_t)(0x748 + which)];
    const int page_base = (which == 0) ? 4 : 0;
    const int alt_base = page_base + 8;
    const bool colscroll = (yscroll & 0x8000) != 0;
    const int row_table = (which == 0) ? 0x7c0 : 0x7e0;
    const int col_table = (which == 0) ? 0x78b : 0x7ab;


    for (int y = 0; y < S16_H; y++) {
        const int row_i = (y >> 3) & 0x3f;
        const uint16_t rowscroll = v->char_ram[(size_t)(row_table + row_i)];
        for (int x0 = colscroll ? -8 : 0; x0 < S16_W; x0 += colscroll ? 16 : S16_W) {
            uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            uint16_t effy = colscroll ? v->char_ram[(size_t)(col_table + ((x0 + 8) / 16))] : yscroll;
            int pages = page_base;
            if (rowscroll & 0x8000) {
                effx = v->char_ram[(size_t)(0x74c + which + 2)];
                effy = v->char_ram[(size_t)(0x748 + which + 2)];
                pages = alt_base;
            }
            // MAME tilemap_16b_draw_layer: (0xc0 - xscroll + xoffs) & 0x3ff.
            const int scroll_x = (0xc0 - (effx & 0x3ff)) & 0x3ff;
            const int scroll_y = effy & 0x1ff;
            const int xs = S16MAX(x0, 0);
            const int xe = S16MIN(colscroll ? x0 + 16 : S16_W, S16_W);
            for (int x = xs; x < xe; x++) {
                const int vx = (x + scroll_x) & 0x3ff;
                const int vy = (y + scroll_y) & 0x1ff;
                // v->screens[base+0..3] = {TL, TR, BL, BR}
                const int quad = ((vy >> 8) << 1) | (vx >> 9);  // TL=0,TR=1,BL=2,BR=3
                const int page = v->screens[(size_t)(pages + quad)];
                const uint16_t data =
                    v->tile_ram[(size_t)(((page * 0x800) + ((vy & 0xff) >> 3) * 64 + ((vx & 0x1ff) >> 3)) &
                                    0x7fff)];
                const int tile_cat = (data >> 15) & 1;
                if (category >= 0 && tile_cat != category) continue;
                int nchar = (int)(data & (uint16_t)(code_mask));
                if (use_tile_bank) nchar = s16_resolve_tile_code(v, nchar);
                const int color = (data >> color_shift) & 0x7f;
                const uint8_t pen = s16_tile(v, nchar)[(vy & 7) * 8 + (vx & 7)];
                if (pen == 0 && !opaque) continue;
                dest[(size_t)(y * S16_W + x)] = v->palette[(size_t)((color << 3) + pen)];
                if (priority && pri_mark) priority[(size_t)(y * S16_W + x)] |= pri_mark;
            }
        }
    }
}

void s16_draw_text_16b(const s16video *v, uint32_t *dest, uint8_t *priority, int category, uint8_t pri_mark,
                       int color_shift, int code_mask, bool use_tile_bank) {
    for (int y = 0; y < S16_H; y++) {
        for (int x = 0; x < S16_W; x++) {
            const int tx = x + 192;
            const uint16_t data = v->char_ram[(size_t)((y >> 3) * 64 + (tx >> 3))];
            const int tile_cat = (data >> 15) & 1;
            if (category >= 0 && tile_cat != category) continue;
            int nchar = data & code_mask;
            if (use_tile_bank) nchar = (int)(v->tile_bank[0]) * v->bank_size + nchar;
            const uint8_t pen = s16_tile(v, nchar)[(y & 7) * 8 + (tx & 7)];
            if (pen == 0) continue;
            const int color = (data >> color_shift) & 7;
            dest[(size_t)(y * S16_W + x)] = v->palette[(size_t)((color << 3) + pen)];
            if (priority && pri_mark) priority[(size_t)(y * S16_W + x)] |= pri_mark;
        }
    }
}

void s16_blit_scrolled(const s16video *v, uint32_t *dest, const uint32_t *source, int scroll_x, int scroll_y,
                       int src_width, int src_height) {
    (void)v;
    for (int y = 0; y < S16_H; y++) {
        const int sy = (y + scroll_y) & (src_height - 1);
        for (int x = 0; x < S16_W; x++) {
            const int sx = (x + scroll_x) & (src_width - 1);
            const uint32_t pixel = source[(size_t)(sy * src_width + sx)];
            if (pixel != 0u) dest[y * S16_W + x] = pixel;
        }
    }
}

void s16_blit_text(const s16video *v, uint32_t *dest, const uint32_t *source) {
    (void)v;
    for (int y = 0; y < S16_H; y++) {
        for (int x = 0; x < S16_W; x++) {
            const uint32_t pixel = source[(size_t)(y * S16_TEXT_W + (x + 192))];
            if (pixel != 0u) dest[y * S16_W + x] = pixel;
        }
    }
}

void s16_draw_sprites_16a(s16video *v, uint32_t *dest, const uint16_t *sprite_rom, uint32_t rom_size, int banks, int pri,
                          int pal_base, uint32_t shadow_index) {
    const uint32_t shadow = v->palette[shadow_index];
    for (int f = 0; f < 0x80; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        const int bottom = (ram[0] >> 8) + 1;
        if (bottom > 0xf0) return;
        const int sprpri = ram[4] & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bank = v->sprite_bank[(ram[4] >> 4) & 7];
        const int top = (ram[0] & 0xff) + 1;
        if (top >= bottom || bank == 255) continue;
        const int xpos = (int)(ram[1] & 0x1ff) - 0xbd;
        const int pitch = (int16_t)(ram[2]);
        const int color = ((ram[4] >> 8) & 0x3f) << 4;
        const uint32_t spritedata = (uint32_t)(0x8000 * (bank % S16MAX(banks, 1)));
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            if (y >= 256) continue;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    for (int g = 3; g >= 0; g--) {
                        const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                        const int pen = pix & 0xf;
                        if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                            const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                            put_sprite_pixel(dest, x, y,
                                             v->palette[(size_t)((pix & 0x3ff) + pal_base)], shadow,
                                             is_shadow);
                        }
                        x++;
                    }
                    if ((pixels & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    uint16_t last = 0;
                    for (int g = 0; g < 4; g++) {
                        const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                        last = pix & 0xf;
                        if (x < 320 && y < 224 && last != 0 && last != 15) {
                            const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                            put_sprite_pixel(dest, x, y,
                                             v->palette[(size_t)((pix & 0x3ff) + pal_base)], shadow,
                                             is_shadow);
                        }
                        x++;
                    }
                    if (last == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void s16_draw_sprites_hangon(s16video *v, uint32_t *dest, const uint16_t *sprite_rom, uint32_t rom_size,
                             const uint8_t *zoom, uint32_t zoom_size, int banks, int pri) {
    for (int f = 0; f < 0x80; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        const int sprpri = ram[4] & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = (ram[0] >> 8) + 1;
        if (bottom > 0xf0) break;
        const int top = (ram[0] & 0xff) + 1;
        const int bank = v->sprite_bank[(ram[1] >> 12) & 0xf];
        if (top >= bottom || bank == 255) continue;
        const int xpos = (int)(ram[1] & 0x1ff) - 0xbd;
        const int pitch = (int16_t)(ram[2]);
        const int color = ((ram[4] >> 8) & 0x3f) << 4;
        const uint16_t vzoom = (ram[4] >> 2) & 0x3f;
        const uint16_t hzoom = (uint16_t)(vzoom << 1);
        const uint32_t spritedata = (uint32_t)(0x8000 * (bank % S16MAX(banks, 1)));
        uint16_t zaddr = (uint16_t)((vzoom & 0x38) << 5);
        const uint16_t zmask = (uint16_t)(1 << (vzoom & 7));
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            if (zoom_size != 0 && (zoom[zaddr % zoom_size] & zmask) != 0) {
                addr = (uint16_t)(addr + pitch);
            }
            zaddr++;
            if (y >= 256) continue;
            uint16_t xacc = 0;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    for (int g = 3; g >= 0; g--) {
                        xacc = (uint16_t)((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                                dest[(size_t)(y * 320 + x)] =
                                    v->palette[(size_t)((pix & 0x3ff) + 0x400)];
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    for (int g = 0; g < 4; g++) {
                        xacc = (uint16_t)((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                                dest[(size_t)(y * 320 + x)] =
                                    v->palette[(size_t)((pix & 0x3ff) + 0x400)];
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 12) & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void s16_draw_sprites_sharrier(s16video *v, uint32_t *dest, const uint32_t *sprite_rom, uint32_t rom_size,
                               const uint8_t *zoom, uint32_t zoom_size, int banks, int pri) {
    const uint32_t shadow = v->palette[0x800];
    for (int f = 0; f < 0x100; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        const int sprpri = (ram[2] >> 14) & 1;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = ram[0] >> 8;
        if (bottom > 0xf0) break;
        const int top = ram[0] & 0xff;
        const int bank = v->sprite_bank[(ram[1] >> 12) & 0xf];
        if (top >= bottom || bank == 255) continue;
        const int xpos = (int)(ram[1] & 0x1ff) - 0xbd;
        int pitch = ram[2] & 0x7f;
        if (pitch > 0x3f) pitch = -(pitch & 0x3f);
        const int color = (ram[2] >> 8) << 4;
        const uint16_t vzoom = ram[4] & 0x3f;
        const uint16_t hzoom = (uint16_t)(((ram[4] >> 8) & 0x3f) << 1);
        const uint32_t spritedata = (uint32_t)(0x8000 * (bank % S16MAX(banks, 1)));
        uint16_t zaddr = (uint16_t)((vzoom & 0x38) << 5);
        const uint16_t zmask = (uint16_t)(1 << (vzoom & 7));
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)((int)(addr) + pitch);
            if (zoom_size != 0 && (zoom[zaddr % zoom_size] & zmask) != 0) {
                addr = (uint16_t)((int)(addr) + pitch);
            }
            zaddr++;
            if (y >= 256) continue;
            uint16_t xacc = 0;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint32_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    for (int g = 7; g >= 0; g--) {
                        xacc = (uint16_t)((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x >= 0 && x < 320 && y < 224 && pen != 0 && pen != 15) {
                                if ((pix & 0x80f) == 0xa) {
                                    dest[(size_t)(y * 320 + x)] =
                                        s16_mix_shadow(dest[(size_t)(y * 320 + x)], shadow);
                                } else {
                                    dest[(size_t)(y * 320 + x)] =
                                        v->palette[(size_t)((pix & 0x3ff) + 0x400)];
                                }
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint32_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % rom_size];
                    for (int g = 0; g < 8; g++) {
                        xacc = (uint16_t)((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x >= 0 && x < 320 && y < 224 && pen != 0 && pen != 15) {
                                if ((pix & 0x80f) == 0xa) {
                                    dest[(size_t)(y * 320 + x)] =
                                        s16_mix_shadow(dest[(size_t)(y * 320 + x)], shadow);
                                } else {
                                    dest[(size_t)(y * 320 + x)] =
                                        v->palette[(size_t)((pix & 0x3ff) + 0x400)];
                                }
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 28) & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void s16_draw_sprites_16b(s16video *v, uint32_t *dest, const uint16_t *sprite_rom, uint32_t rom_size, int banks, int pri,
                          uint32_t shadow_index) {
    const uint32_t shadow = v->palette[shadow_index];
    for (int f = 0; f < 0x80; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        if (ram[2] & 0x8000) return;
        const int sprpri = (ram[4] & 0xff) >> 6;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = ram[0] >> 8;
        const int top = ram[0] & 0xff;
        const bool hide = (ram[2] & 0x4000) != 0;
        const int bank = v->sprite_bank[(ram[4] >> 8) & 0xf];
        if (hide || top >= bottom || bank == 255) continue;
        const int xpos = (int)(ram[1] & 0x1ff) - 0xb7;
        const int pitch = (int8_t)(ram[2] & 0xff);
        const int color = (ram[4] & 0x3f) << 4;
        const bool flip = (ram[2] & 0x100) != 0;
        const uint8_t vzoom = (uint8_t)((ram[5] >> 5) & 0x1f);
        const uint8_t hzoom = (uint8_t)(ram[5] & 0x1f);
        const uint32_t spritedata = (uint32_t)(0x10000 * (bank % S16MAX(banks, 1)));
        ram[5] &= 0x3ff;
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            ram[5] = (uint16_t)(ram[5] + ((uint16_t)(vzoom) << 10));
            if (ram[5] & 0x8000) {
                addr = (uint16_t)(addr + pitch);
                ram[5] &= 0x7fff;
            }
            if (y >= 256) continue;
            uint16_t xacc = (uint16_t)(4 * hzoom);
            uint16_t data_7 = addr;
            int x = xpos;
            if (!flip) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % rom_size];
                    for (int g = 3; g >= 0; g--) {
                        xacc = (uint16_t)((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (pen != 0 && pen != 15) {
                                const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                                put_sprite_pixel(dest, x, y, v->palette[(size_t)((pix & 0x3ff) + 0x400)],
                                                 shadow, is_shadow);
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % rom_size];
                    for (int g = 0; g < 4; g++) {
                        xacc = (uint16_t)((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (pen != 0 && pen != 15) {
                                const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                                put_sprite_pixel(dest, x, y, v->palette[(size_t)((pix & 0x3ff) + 0x400)],
                                                 shadow, is_shadow);
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 12) & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

static void mix_plot(const s16video *v, uint32_t *dest, const uint8_t *priority, int y, int px, uint16_t pix,
                     int pri_mask, uint32_t shadow)
{
    if (px < 0 || px >= 320) return;
    const int pen = pix & 0xf;
    if (pen == 0 || pen == 15) return;
    if (pri_mask <= (int)priority[(size_t)(y * 320 + px)]) return;
    const bool is_shadow = (pix & 0x3f0) == 0x3f0;
    put_sprite_pixel(dest, px, y, v->palette[(size_t)((pix & 0x3ff) + 0x400)], shadow, is_shadow);
}

void s16_mix_sprites_16b(s16video *v, uint32_t *dest, uint8_t *priority, const uint16_t *sprite_rom, uint32_t rom_size,
                         int banks, uint32_t shadow_index) {
    // MAME System 18 path: sprite shows when (1 << sprpri) > tilemap priority.
    const uint32_t shadow = v->palette[shadow_index];
    for (int f = 0; f < 0x80; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        if (ram[2] & 0x8000) return;
        const int sprpri = (ram[4] & 0xff) >> 6;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = ram[0] >> 8;
        const int top = ram[0] & 0xff;
        const bool hide = (ram[2] & 0x4000) != 0;
        const int bank = v->sprite_bank[(ram[4] >> 8) & 0xf];
        if (hide || top >= bottom || bank == 255) continue;
        const int xpos = (int)(ram[1] & 0x1ff) - 0xb7;
        const int pitch = (int8_t)(ram[2] & 0xff);
        const int color = (ram[4] & 0x3f) << 4;
        const bool flip = (ram[2] & 0x100) != 0;
        const uint8_t vzoom = (uint8_t)((ram[5] >> 5) & 0x1f);
        const uint8_t hzoom = (uint8_t)(ram[5] & 0x1f);
        const uint32_t spritedata = (uint32_t)(0x10000 * (bank % S16MAX(banks, 1)));
        const int pri_mask = 1 << sprpri;
        ram[5] &= 0x3ff;
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            ram[5] = (uint16_t)(ram[5] + ((uint16_t)(vzoom) << 10));
            if (ram[5] & 0x8000) {
                addr = (uint16_t)(addr + pitch);
                ram[5] &= 0x7fff;
            }
            if (y < 0 || y >= 224) continue;
            uint16_t xacc = (uint16_t)(4 * hzoom);
            uint16_t data_7 = addr;
            int x = xpos;
            if (!flip) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % rom_size];
                    for (int g = 3; g >= 0; g--) {
                        xacc = (uint16_t)((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            mix_plot(v, dest, priority, y, x, (uint16_t)(((pixels >> (g * 4)) & 0xf) | color), pri_mask, shadow);
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % rom_size];
                    for (int g = 0; g < 4; g++) {
                        xacc = (uint16_t)((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            mix_plot(v, dest, priority, y, x, (uint16_t)(((pixels >> (g * 4)) & 0xf) | color), pri_mask, shadow);
                            x++;
                        }
                    }
                    if (((pixels >> 12) & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void s16_draw_sprites_outrun(s16video *v, uint32_t *dest, const uint32_t *sprite_rom, uint32_t rom_size, int banks,
                             int pri, uint32_t shadow_index) {
    const uint32_t shadow = v->palette[shadow_index];
    for (int f = 0; f < 0x100; f++) {
        uint16_t *ram = &v->sprite_ram[(size_t)(f * 8)];
        if (ram[0] & 0x8000) return;
        const int sprpri = (ram[3] >> 12) & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[1];
        ram[7] = addr;
        if (ram[0] & 0x5000) continue;
        const int top = (int)(ram[0] & 0x1ff) - 0x100;
        const int bank = ((ram[0] >> 9) & 7) % S16MAX(banks, 1);
        int xpos = ram[2] & 0x1ff;
        const int xdelta = (ram[4] & 0x2000) ? 1 : -1;
        if (xpos < 0x80 && xdelta < 0) xpos += 0x149;
        else xpos -= 0xb7;
        uint16_t vzoom = ram[3] & 0x7ff;
        uint16_t hzoom = ram[4] & 0x7ff;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        const uint16_t color = (uint16_t)(((ram[5] & 0x7f) << 4) | (ram[3] & 0x4000));
        const uint32_t spritedata = (uint32_t)(0x10000 * bank);
        const bool flip = ((~ram[4] >> 14) & 1) != 0;
        const int pitch =
            (int16_t)((ram[2] >> 1) | ((ram[4] & 0x1000) << 3)) / 256;
        const int height = (ram[5] >> 8) + 1;
        const int ydelta = (ram[4] & 0x8000) ? 1 : -1;
        int yacc = 0;
        int y = top;
        const int ytarget = top + ydelta * height;
        while (y != ytarget) {
            if (y >= 0 && y < 256) {
                int xacc = 0;
                uint16_t data_7 = addr;
                int x = xpos;
                if (!flip) {
                    while ((xdelta > 0 && x < 512) || (xdelta < 0 && x >= 0)) {
                        const uint32_t pixels =
                            sprite_rom[(spritedata + data_7) % rom_size];
                        for (int g = 7; g >= 0; g--) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            while (xacc < 0x200) {
                                const int pen = pix & 0xf;
                                if (pen != 0 && pen != 15) {
                                    const bool is_shadow = (pix & 0x400f) == 0x400a;
                                    put_sprite_pixel(dest, x, y,
                                                     v->palette[(size_t)((pix & 0x7ff) + 0x800)],
                                                     shadow, is_shadow);
                                }
                                x += xdelta;
                                xacc += hzoom;
                            }
                            xacc -= 0x200;
                        }
                        if ((pixels & 0xf0) == 0xf0) {
                            ram[7] = data_7;
                            break;
                        }
                        data_7++;
                    }
                } else {
                    while ((xdelta > 0 && x < 512) || (xdelta < 0 && x >= 0)) {
                        const uint32_t pixels =
                            sprite_rom[(spritedata + data_7) % rom_size];
                        for (int g = 0; g < 8; g++) {
                            const uint16_t pix = (uint16_t)(((pixels >> (g * 4)) & 0xf) | color);
                            while (xacc < 0x200) {
                                const int pen = pix & 0xf;
                                if (pen != 0 && pen != 15) {
                                    const bool is_shadow = (pix & 0x400f) == 0x400a;
                                    put_sprite_pixel(dest, x, y,
                                                     v->palette[(size_t)((pix & 0x7ff) + 0x800)],
                                                     shadow, is_shadow);
                                }
                                x += xdelta;
                                xacc += hzoom;
                            }
                            xacc -= 0x200;
                        }
                        if ((pixels & 0x0f000000) == 0x0f000000) {
                            ram[7] = data_7;
                            break;
                        }
                        data_7--;
                    }
                }
                yacc += vzoom;
                addr = (uint16_t)(addr + pitch * (yacc >> 9));
                yacc &= 0x1ff;
            }
            y += ydelta;
        }
    }
}

void s16_decode_outrun_road(uint8_t *road_gfx, const uint8_t *rom, uint32_t rom_size) {
    const uint32_t len = 0x8000 * 2;
    memset(road_gfx, 3, 256 * 2 * 512 + 512);
    for (int y = 0; y < 512; y++) {
        const uint32_t src = (uint32_t)(((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % len);
        const uint32_t dst = (uint32_t)(y * 512);
        for (int x = 0; x < 512; x++) {
            const uint8_t a = (rom[(src + (uint32_t)(x / 8)) % rom_size] >> ((~x) & 7)) & 1;
            const uint8_t b =
                (rom[(src + (uint32_t)(x / 8) + 0x4000) % rom_size] >> ((~x) & 7)) & 1;
            uint8_t pix = (uint8_t)(a | (b << 1));
            if (x >= 256 - 8 && x < 256 && pix == 3) pix |= 4;
            road_gfx[dst + (uint32_t)(x)] = pix;
        }
    }
    memset(road_gfx + 256 * 2 * 512, 3, 512);
}

void s16_decode_hangon_road(uint8_t *road_gfx, const uint8_t *rom, uint32_t rom_size) {
    memset(road_gfx, 0, 256 * 512);
    for (int y = 0; y < 256; y++) {
        const uint32_t src = (uint32_t)((y * 0x40) % 0x8000);
        const uint32_t dst = (uint32_t)(y * 512);
        for (int x = 0; x < 512; x++) {
            const uint8_t a = (rom[(src + (uint32_t)(x >> 3)) % rom_size] >> ((~x) & 7)) & 1;
            const uint8_t b =
                (rom[(src + (uint32_t)(x >> 3) + 0x4000) % rom_size] >> ((~x) & 7)) & 1;
            road_gfx[dst + (uint32_t)(x)] = (uint8_t)(a | (b << 1));
        }
    }
}

void s16_draw_outrun_road(uint32_t* dest, const uint32_t* palette, const uint16_t* buffer,
                      const uint8_t *road_gfx, uint8_t control, uint16_t colorbase1,
                      uint16_t colorbase2, uint16_t colorbase3, uint16_t xoff, int pri) {
    static const uint8_t kPriorityMap[2][8] = {
        {0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0},
        {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80},
    };
    for (int y = 0; y < 224; y++) {
        const uint16_t data0 = buffer[y];
        const uint16_t data1 = buffer[0x100 + y];
        if (pri == 0) {
            uint16_t color0 = 0xff;
            switch (control & 3) {
                case 0:
                    if (data0 & 0x800) color0 = data0 & 0x7f;
                    break;
                case 1:
                    if (data0 & 0x800) color0 = data0 & 0x7f;
                    else if (data1 & 0x800) color0 = data1 & 0x7f;
                    break;
                case 2:
                    if (data1 & 0x800) color0 = data1 & 0x7f;
                    else if (data0 & 0x800) color0 = data0 & 0x7f;
                    break;
                case 3:
                    if (data1 & 0x800) color0 = data1 & 0x7f;
                    break;
            }
            const uint32_t fill =
                (color0 != 0xff) ? palette[color0 | colorbase3] : palette[0x2000];
            for (int x = 0; x < 320; x++) dest[y * 320 + x] = fill;
            continue;
        }
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        uint32_t src0 = (data0 & 0x800) ? (uint32_t)(256 * 2 * 512)
                                        : (uint32_t)(((data0 >> 1) & 0xff) * 512);
        int hpos0 = (int)(((control & 4) ? buffer[0x200 + y] : buffer[0x200 + (data0 & 0x1ff)]) & 0xfff);
        uint16_t color0 = (control & 4) ? buffer[0x600 + y] : buffer[0x600 + (data0 & 0x1ff)];
        uint32_t src1 = (data1 & 0x800) ? (uint32_t)(256 * 2 * 512)
                                        : (uint32_t)((0x100 + ((data1 >> 1) & 0xff)) * 512);
        int hpos1 = (int)(((control & 4) ? buffer[0x400 + (0x100 + y)] : buffer[0x400 + (data1 & 0x1ff)]) &
                        0xfff);
        uint16_t color1 = (control & 4) ? buffer[0x600 + (0x100 + y)] : buffer[0x600 + (data1 & 0x1ff)];
        uint16_t table[32] = {};
        table[0x00] = (uint16_t)(colorbase1 ^ 0x00 ^ ((color0 >> 0) & 1));
        table[0x01] = (uint16_t)(colorbase1 ^ 0x02 ^ ((color0 >> 1) & 1));
        table[0x02] = (uint16_t)(colorbase1 ^ 0x04 ^ ((color0 >> 2) & 1));
        const uint8_t bg0 = (uint8_t)((color0 >> 8) & 0xf);
        table[0x03] = (data0 & 0x200) ? table[0x00] : (uint16_t)(colorbase2 ^ 0x00 ^ bg0);
        table[0x07] = (uint16_t)(colorbase1 ^ 0x06 ^ ((color0 >> 3) & 1));
        table[0x10] = (uint16_t)(colorbase1 ^ 0x08 ^ ((color1 >> 4) & 1));
        table[0x11] = (uint16_t)(colorbase1 ^ 0x0a ^ ((color1 >> 5) & 1));
        table[0x12] = (uint16_t)(colorbase1 ^ 0x0c ^ ((color1 >> 6) & 1));
        const uint8_t bg1 = (uint8_t)((color1 >> 8) & 0xf);
        table[0x13] = (data1 & 0x200) ? table[0x10] : (uint16_t)(colorbase2 ^ 0x10 ^ bg1);
        table[0x17] = (uint16_t)(colorbase1 ^ 0x0e ^ ((color1 >> 7) & 1));
        switch (control & 3) {
            case 0:
                if (data0 & 0x800) break;
                hpos0 = (hpos0 - (0x5f8 + xoff)) & 0xfff;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix0 = (hpos0 < 0x200) ? road_gfx[src0 + (uint32_t)(hpos0)] : 3;
                    dest[y * 320 + x] = palette[table[pix0]];
                    hpos0 = (hpos0 + 1) & 0xfff;
                }
                break;
            case 1:
            case 2: {
                hpos0 = (hpos0 - (0x5f8 + xoff)) & 0xfff;
                hpos1 = (hpos1 - (0x5f8 + xoff)) & 0xfff;
                const int map = (control & 3) == 1 ? 0 : 1;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix0 = (hpos0 < 0x200) ? road_gfx[src0 + (uint32_t)(hpos0)] : 3;
                    const uint8_t pix1 = (hpos1 < 0x200) ? road_gfx[src1 + (uint32_t)(hpos1)] : 3;
                    if ((kPriorityMap[map][pix0] >> pix1) & 1) {
                        dest[y * 320 + x] = palette[table[0x10 + pix1]];
                    } else {
                        dest[y * 320 + x] = palette[table[pix0]];
                    }
                    hpos0 = (hpos0 + 1) & 0xfff;
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            }
            case 3:
                if (data1 & 0x800) break;
                hpos1 = (hpos1 - (0x5f8 + xoff)) & 0xfff;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix1 = (hpos1 < 0x200) ? road_gfx[src1 + (uint32_t)(hpos1)] : 3;
                    dest[y * 320 + x] = palette[table[0x10 + pix1]];
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
        }
    }
}

void s16_draw_hangon_road(uint32_t* dest, const uint32_t* palette, const uint16_t* road_ram,
                      const uint8_t *road_gfx, uint16_t colorbase1, uint16_t colorbase2, int pri,
                      bool sharrier) {
    for (int y = 0; y < 224; y++) {
        const uint16_t control = road_ram[y];
        const bool plycont = ((control >> 10) & 3) != 0;
        if (!plycont && pri != 0) continue;
        if (plycont && pri == 0) continue;
        const uint16_t hpos = road_ram[0x100 + (control & 0xff)];
        const uint16_t color0 = road_ram[0x200 + (control & 0xff)];
        const uint16_t color1 = road_ram[0x300 + (control & 0xff)];
        const uint32_t src = (uint32_t)(control & 0xff) * 512;
        uint8_t ctr9m = (uint8_t)(hpos & 7);
        uint8_t ctr9n9p = (uint8_t)(hpos >> 3);
        bool ff9j1 = ((hpos >> 11) & 1) != 0;
        bool ff9j2 = true;
        uint8_t ss8j = 0;
        for (int x = -24; x <= 511; x++) {
            const bool ctr9n9p_ena = (ctr9m == 7);
            if (ctr9n9p == 0xff) ff9j1 = false;
            if ((control & 0x100) == 0) ff9j1 = true;
            if (!sharrier && (control & 0x200) == 0) ff9j2 = true;
            uint8_t md = 3;
            if ((!sharrier || (control & 0x200) == 0) && (ctr9n9p & 0xc0) == 0xc0) {
                const uint32_t index =
                    src + (uint32_t)(((ctr9n9p & 0x3f) << 3) | ((ss8j & 1) ? ctr9m : (ctr9m ^ 7)));
                md = road_gfx[index];
            }
            const uint8_t select = (uint8_t)((ss8j >> 3) & 1);
            uint16_t color;
            if (ff9j2 && md == 3) {
                color = select ? (uint16_t)((color0 & 0x3f) | colorbase2)
                               : (uint16_t)(((color0 >> 8) & 0x3f) | colorbase2);
            } else {
                if ((color1 & 0x80) && md == 3) md = 0;
                color = (uint16_t)((color1 >> ((md << 1) | select)) & 1);
                color = (uint16_t)(color | (select << 3) | (md << 1) | colorbase1);
            }
            if (x >= 0 && x < 320) dest[y * 320 + x] = palette[color];
            ctr9m = (uint8_t)((ctr9m + 1) & 7);
            if (ctr9n9p_ena) {
                if (ff9j1) ctr9n9p++;
                else ctr9n9p--;
            }
            ff9j2 = !((!ff9j1) && ((ss8j & 0x80) != 0));
            ss8j = (uint8_t)((ss8j << 1) | (uint8_t)(ff9j1));
        }
    }
}

