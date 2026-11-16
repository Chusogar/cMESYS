// sega_315_5313.c - Sega 315-5313 VDP: a C11 port of dsp-cpp's
// video/sega_315_5313 (see the header).
#include "sega_315_5313.h"
#include <string.h>

static const uint8_t expand3[8] = {0, 36, 73, 109, 146, 182, 219, 255};

static uint32_t cram_to_rgb(uint16_t c) {
    return ((uint32_t)expand3[(c >> 1) & 7] << 16) | ((uint32_t)expand3[(c >> 5) & 7] << 8) |
           expand3[(c >> 9) & 7];
}

static inline uint16_t to565(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb & 0xff) >> 3));
}

static uint32_t shade_ch(uint32_t rgb, int shift, bool shadow, bool highlight) {
    int v = (int)((rgb >> shift) & 0xff);
    if (shadow && !highlight) v /= 2;
    if (highlight && !shadow) { v = v + (255 - v) / 2; if (v > 255) v = 255; }
    return (uint32_t)v;
}

static uint32_t palette_rgb(md_vdp *v, uint8_t index, bool shadow, bool highlight) {
    uint32_t rgb = v->palette[index & 0x3f];
    if (!shadow && !highlight) return rgb;
    return (shade_ch(rgb, 16, shadow, highlight) << 16) | (shade_ch(rgb, 8, shadow, highlight) << 8) |
           shade_ch(rgb, 0, shadow, highlight);
}

static void raise_hint(md_vdp *v, bool s) { if (v->hint) v->hint(v->user, s); }
static void raise_vint(md_vdp *v, bool s) { if (v->vint) v->vint(v->user, s); }
static void raise_z80(md_vdp *v, bool s) { if (v->z80_irq) v->z80_irq(v->user, s); }

void md_vdp_init(md_vdp *v, bool pal, int psg_rate) {
    v->pal = pal;
    sn76496_init(&v->psg, MD_PSG_CLOCK, psg_rate, 1.0f);
    md_vdp_reset(v);
}

void md_vdp_reset(md_vdp *v) {
    memset(v->regs, 0, sizeof v->regs);
    memset(v->vram, 0, sizeof v->vram);
    memset(v->cram, 0, sizeof v->cram);
    memset(v->vsram, 0, sizeof v->vsram);
    memset(v->palette, 0, sizeof v->palette);
    memset(v->line, 0, sizeof v->line);
    memset(v->line_backdrop, 1, sizeof v->line_backdrop);
    v->vblank_flag = v->sprite_collision = v->sprite_overflow = false;
    v->command_pending = v->irq4_pending = v->irq6_pending = false;
    v->vram_fill_pending = v->dma_active = false;
    v->imode = v->imode_odd_frame = v->vdp_code = 0;
    v->command_part1 = v->command_part2 = v->vdp_address = 0;
    v->vram_fill_length = v->data_read_buffer = 0;
    v->total_scanlines = v->pal ? MD_LINES_PAL : MD_LINES_NTSC;
    v->visible_scanlines = 224;
    v->irq6_scanline = 224;
    v->z80irq_scanline = 226;
    v->hint_counter = v->hpos_cycles = v->current_line = 0;
    v->h40_ = true;
    sn76496_reset(&v->psg);
    raise_hint(v, false);
    raise_vint(v, false);
    raise_z80(v, false);
}

void md_vdp_acknowledge(md_vdp *v, int level) {
    if (level == 6) { v->irq6_pending = false; raise_vint(v, false); }
    else if (level == 4) { v->irq4_pending = false; raise_hint(v, false); }
}

static uint16_t read_vram_word(const md_vdp *v, uint16_t addr) {
    addr &= 0xfffe;
    return (uint16_t)((v->vram[addr] << 8) | v->vram[(uint16_t)(addr + 1)]);
}

static void increment_address(md_vdp *v) {
    v->vdp_address = (uint16_t)(v->vdp_address + (v->regs[0x0f] & 0xff));
}

static void write_vram_word(md_vdp *v, uint16_t value) {
    uint16_t addr = v->vdp_address & 0xfffe;
    if (v->vdp_address & 1) {
        v->vram[addr] = (uint8_t)value;
        v->vram[(uint16_t)(addr + 1)] = (uint8_t)(value >> 8);
    } else {
        v->vram[addr] = (uint8_t)(value >> 8);
        v->vram[(uint16_t)(addr + 1)] = (uint8_t)value;
    }
    increment_address(v);
}

static void write_cram_word(md_vdp *v, uint16_t value) {
    int i = (v->vdp_address >> 1) & 0x3f;
    v->cram[i] = value & 0x0eee;
    v->palette[i] = cram_to_rgb(v->cram[i]);
    increment_address(v);
}

static void write_vsram_word(md_vdp *v, uint16_t value) {
    v->vsram[(v->vdp_address >> 1) % 40] = value & 0x07ff;
    increment_address(v);
}

static void update_code_and_address(md_vdp *v) {
    v->vdp_code = (uint8_t)(((v->command_part1 & 0xc000) >> 14) | ((v->command_part2 & 0x00f0) >> 2));
    v->vdp_address = (uint16_t)((v->command_part1 & 0x3fff) | ((v->command_part2 & 0x0003) << 14));
}

static void set_register(md_vdp *v, int regnum, uint8_t value) {
    v->regs[regnum & 0x1f] = value;
    if (regnum == 0 && v->irq4_pending) raise_hint(v, (v->regs[0] & 0x10) != 0);
    if (regnum == 1 && v->irq6_pending) raise_vint(v, (v->regs[1] & 0x20) != 0);
}

static uint16_t control_port_read(md_vdp *v) {
    v->command_pending = false;
    bool vblank = v->vblank_flag;
    if ((v->regs[1] & 0x40) == 0) vblank = true;
    unsigned hblank = 0;
    if (v->hpos_cycles > 400) hblank = 1;
    if (v->hpos_cycles > 460) hblank = 0;
    unsigned odd = (v->imode & 1) ? (v->imode_odd_frame ^ 1u) : 0u;
    uint16_t ret = (uint16_t)((1 << 13) | (1 << 12) | (1 << 10) | (1 << 9) |
                              ((unsigned)v->irq6_pending << 7) | ((unsigned)v->sprite_overflow << 6) |
                              ((unsigned)v->sprite_collision << 5) | (odd << 4) | ((unsigned)vblank << 3) |
                              (hblank << 2) | ((unsigned)v->dma_active << 1) | (unsigned)v->pal);
    v->irq6_pending = false;
    v->sprite_collision = false;
    v->sprite_overflow = false;
    return ret;
}

static uint16_t data_port_read(md_vdp *v) {
    v->command_pending = false;
    uint16_t value;
    switch (v->vdp_code & 0x0f) {
    case 0x00: value = read_vram_word(v, v->vdp_address); break;
    case 0x04: value = v->vsram[(v->vdp_address >> 1) % 40]; break;
    case 0x08: value = v->cram[(v->vdp_address >> 1) & 0x3f]; break;
    default: value = v->data_read_buffer; break;
    }
    v->data_read_buffer = value;
    increment_address(v);
    return value;
}

static void dma_fill(md_vdp *v, uint16_t value) {
    write_vram_word(v, value);
    uint32_t length = (uint32_t)(v->regs[0x13] | ((uint32_t)v->regs[0x14] << 8));
    if (length == 0) length = 0x10000;
    for (uint32_t i = 0; i < length; i++) {
        v->vram[v->vdp_address ^ 1] = (uint8_t)(value >> 8);
        increment_address(v);
    }
    v->regs[0x13] = 0;
    v->regs[0x14] = 0;
    v->vram_fill_pending = false;
}

static void dma_68k_copy(md_vdp *v) {
    if (!v->dma_read) return;
    uint32_t source = ((uint32_t)v->regs[0x15] | ((uint32_t)v->regs[0x16] << 8) |
                       ((uint32_t)(v->regs[0x17] & 0x7f) << 16)) << 1;
    uint32_t length = (uint32_t)(v->regs[0x13] | ((uint32_t)v->regs[0x14] << 8));
    if (length == 0) length = 0x10000;
    int cd = v->vdp_code & 0x0f;
    v->dma_active = true;
    for (uint32_t i = 0; i < length; i++) {
        uint16_t data = v->dma_read(v->user, source);
        if (cd == 0x01) write_vram_word(v, data);
        else if (cd == 0x03) write_cram_word(v, data);
        else if (cd == 0x05) write_vsram_word(v, data);
        source = (source & 0xfe0000) | ((source + 2) & 0x1ffff);
    }
    v->dma_active = false;
    v->regs[0x13] = 0;
    v->regs[0x14] = 0;
    source >>= 1;
    v->regs[0x15] = (uint16_t)(source & 0xff);
    v->regs[0x16] = (uint16_t)((source >> 8) & 0xff);
}

static void dma_vram_copy(md_vdp *v) {
    uint16_t source = (uint16_t)(v->regs[0x15] | (v->regs[0x16] << 8));
    uint32_t length = (uint32_t)(v->regs[0x13] | ((uint32_t)v->regs[0x14] << 8));
    if (length == 0) length = 0x10000;
    v->dma_active = true;
    for (uint32_t i = 0; i < length; i++) {
        v->vram[v->vdp_address] = v->vram[source];
        source = (uint16_t)(source + 1);
        increment_address(v);
    }
    v->dma_active = false;
    v->regs[0x13] = 0;
    v->regs[0x14] = 0;
}

static void handle_dma(md_vdp *v) {
    if ((v->regs[1] & 0x10) == 0) return;
    int mode = (v->regs[0x17] & 0xc0) >> 6;
    if (mode == 0 || mode == 1) dma_68k_copy(v);
    else if (mode == 2) {
        int cd = v->vdp_code & 0x0f;
        if (cd == 0x01 || cd == 0x03 || cd == 0x05) {
            v->vram_fill_pending = true;
            v->vram_fill_length = (uint16_t)(v->regs[0x13] | (v->regs[0x14] << 8));
        }
    } else dma_vram_copy(v);
}

static void data_port_write(md_vdp *v, uint16_t value) {
    v->command_pending = false;
    if (v->vram_fill_pending) { dma_fill(v, value); return; }
    switch (v->vdp_code & 0x0f) {
    case 0x01: write_vram_word(v, value); break;
    case 0x03: write_cram_word(v, value); break;
    case 0x05: write_vsram_word(v, value); break;
    default: break;
    }
}

static void control_port_write(md_vdp *v, uint16_t value) {
    v->vram_fill_pending = false;
    if (v->command_pending) {
        v->command_pending = false;
        v->command_part2 = value;
        update_code_and_address(v);
        if (v->vdp_code & 0x20) handle_dma(v);
        return;
    }
    if ((value & 0xc000) == 0x8000) {
        set_register(v, (value >> 8) & 0x1f, (uint8_t)value);
        v->vdp_code = 0;
        v->vdp_address = 0;
        return;
    }
    v->command_pending = true;
    v->command_part1 = value;
    update_code_and_address(v);
}

static uint8_t v_counter(const md_vdp *v, int line) {
    if (!v->pal) return (uint8_t)(line > 0xea ? line - 6 : line);
    if (line >= 0x103) return (uint8_t)(line - 0x103 + 0xca);
    return (uint8_t)line;
}

uint16_t md_vdp_read(md_vdp *v, uint8_t address) {
    switch (address & 0x1f) {
    case 0x00: case 0x02: return data_port_read(v);
    case 0x04: case 0x06: return control_port_read(v);
    case 0x08: case 0x0a: case 0x0c: case 0x0e: {
        uint8_t vc = v_counter(v, v->current_line);
        uint8_t hc = (uint8_t)((v->hpos_cycles * 2) & 0xff);
        return (uint16_t)((vc << 8) | hc);
    }
    case 0x10: case 0x12: case 0x14: case 0x16: return 0;
    default: return 0xffff;
    }
}

void md_vdp_write(md_vdp *v, uint8_t address, uint16_t value) {
    switch (address & 0x1f) {
    case 0x00: case 0x02: data_port_write(v, value); break;
    case 0x04: case 0x06: control_port_write(v, value); break;
    case 0x10: case 0x12: case 0x14: case 0x16: sn76496_write(&v->psg, (uint8_t)value); break;
    default: break;
    }
}

void md_vdp_write_byte(md_vdp *v, uint8_t address, uint8_t value) {
    uint8_t port = address & 0x1f;
    if (port >= 0x10 && port <= 0x17) { sn76496_write(&v->psg, value); return; }
    md_vdp_write(v, port, (uint16_t)((value << 8) | value));
}

static int plane_dim(int bits) { return bits == 0 ? 32 : bits == 1 ? 64 : 128; }

static uint32_t tile_row_bits(const md_vdp *v, uint16_t tile, int row) {
    uint32_t a = ((uint32_t)(tile & 0x7ff) << 5) + (uint32_t)(row << 2);
    return ((uint32_t)v->vram[a & 0xffff] << 24) | ((uint32_t)v->vram[(a + 1) & 0xffff] << 16) |
           ((uint32_t)v->vram[(a + 2) & 0xffff] << 8) | v->vram[(a + 3) & 0xffff];
}

static void blit_tile_row(int x, uint32_t row, int palette, bool hflip, bool priority,
                          uint8_t *color, uint8_t *pri, int width) {
    for (int px = 0; px < 8; px++) {
        int dest = x + px;
        if (dest < 0 || dest >= width) continue;
        int src = hflip ? px : (7 - px);
        uint8_t pix = (uint8_t)((row >> (src * 4)) & 0x0f);
        if (pix == 0) continue;
        color[dest] = (uint8_t)((palette << 4) | pix);
        pri[dest] = priority ? 1 : 0;
    }
}

static void draw_plane(md_vdp *v, int line, bool plane_a, uint8_t *color, uint8_t *pri) {
    int width = v->h40_ ? 320 : 256;
    int pw = plane_dim(v->regs[0x10] & 3), ph = plane_dim((v->regs[0x10] >> 4) & 3);
    uint32_t nt = plane_a ? ((uint32_t)(v->regs[0x02] & 0x38) << 10) : ((uint32_t)(v->regs[0x04] & 0x07) << 13);
    int hscroll_mode = v->regs[0x0b] & 3;
    uint32_t hbase = (uint32_t)(v->regs[0x0d] & 0x3f) << 10;
    int hindex = 0;
    if (hscroll_mode == 2) hindex = line & ~7;
    else if (hscroll_mode == 3 || hscroll_mode == 1) hindex = line;
    uint32_t hoff = hbase + (uint32_t)(hindex * 4) + (plane_a ? 0u : 2u);
    uint16_t hscroll = read_vram_word(v, (uint16_t)hoff);
    bool column_scroll = (v->regs[0x0b] & 4) != 0;
    int vs_index = plane_a ? 0 : 1;

    for (int x = 0; x < width;) {
        int vscroll = v->vsram[vs_index] & 0x3ff;
        if (column_scroll) {
            int col = ((x - (int)(hscroll & 15) + 16) / 16) % 20;
            vscroll = v->vsram[((col * 2) + vs_index) % 40] & 0x3ff;
        }
        int plane_y = (line + vscroll) & ((ph * 8) - 1);
        int tile_y = plane_y >> 3, row_in_tile = plane_y & 7;
        int plane_x = (x - (int)hscroll) & ((pw * 8) - 1);
        int tile_x = plane_x >> 3, fine_x = plane_x & 7;
        uint16_t attr = read_vram_word(v, (uint16_t)(nt + (uint32_t)(((tile_y * pw) + tile_x) * 2)));
        bool hflip = (attr & 0x0800) != 0, vflip = (attr & 0x1000) != 0;
        int palette = (attr >> 13) & 3;
        bool priority = (attr & 0x8000) != 0;
        uint32_t bits = tile_row_bits(v, attr & 0x7ff, vflip ? (7 - row_in_tile) : row_in_tile);
        for (int px = fine_x; px < 8 && x < width; px++, x++) {
            int src = hflip ? px : (7 - px);
            uint8_t pix = (uint8_t)((bits >> (src * 4)) & 0x0f);
            if (pix == 0) continue;
            color[x] = (uint8_t)((palette << 4) | pix);
            pri[x] = priority ? 1 : 0;
        }
    }
}

static void draw_window(md_vdp *v, int line, uint8_t *color, uint8_t *pri) {
    int width = v->h40_ ? 320 : 256;
    uint8_t whp = (uint8_t)v->regs[0x11], wvp = (uint8_t)v->regs[0x12];
    int window_h = (whp & 0x1f) * 8, window_v = (wvp & 0x1f) * 8;
    bool window_right = (whp & 0x80) != 0, window_down = (wvp & 0x80) != 0;
    bool line_in_window = window_down ? line >= window_v : (window_v != 0 && line < window_v);
    uint32_t nt = (uint32_t)(v->regs[0x03] & (v->h40_ ? 0x3c : 0x3e)) << 10;
    int cells = v->h40_ ? 64 : 32;
    for (int x = 0; x < width; x += 8) {
        bool in_window = line_in_window;
        if (!in_window) in_window = window_right ? x >= window_h : (window_h != 0 && x < window_h);
        if (!in_window) continue;
        uint16_t attr = read_vram_word(v, (uint16_t)(nt + (uint32_t)((((line >> 3) * cells) + (x >> 3)) * 2)));
        bool hflip = (attr & 0x0800) != 0, vflip = (attr & 0x1000) != 0;
        int row = vflip ? (7 - (line & 7)) : (line & 7);
        blit_tile_row(x, tile_row_bits(v, attr & 0x7ff, row), (attr >> 13) & 3, hflip,
                      (attr & 0x8000) != 0, color, pri, width);
    }
}

static void draw_sprites(md_vdp *v, int line, uint8_t *color, uint8_t *pri) {
    int width = v->h40_ ? 320 : 256;
    int max_sprites = v->h40_ ? 80 : 64, max_per_line = v->h40_ ? 20 : 16;
    uint32_t sat = (uint32_t)(v->regs[0x05] & (v->h40_ ? 0x7e : 0x7f)) << 9;
    int ybase = ((v->imode & 3) == 3) ? 256 : 128;
    int link = 0, drawn = 0, pixels = 0;
    bool masked = false;
    uint8_t occ[MD_VDP_W];
    memset(occ, 0, sizeof occ);

    for (int n = 0; n < max_sprites; n++) {
        uint32_t base = sat + (uint32_t)(link * 8);
        uint16_t yword = read_vram_word(v, (uint16_t)base);
        uint16_t size_link = read_vram_word(v, (uint16_t)(base + 2));
        uint16_t attr = read_vram_word(v, (uint16_t)(base + 4));
        uint16_t xword = read_vram_word(v, (uint16_t)(base + 6));
        int ypos = yword & 0x3ff;
        int height = ((size_link >> 8) & 3) + 1, spr_width = ((size_link >> 10) & 3) + 1;
        int y = line + ybase - ypos;
        int next = size_link & 0x7f;
        if (y < 0 || y >= height * 8) {
            if (next == 0 || next == link) break;
            link = next;
            continue;
        }
        if (drawn >= max_per_line) { v->sprite_overflow = true; break; }
        int xpos = xword & 0x1ff;
        if (xpos == 0) masked = true;
        if (!masked) {
            bool hflip = (attr & 0x0800) != 0, vflip = (attr & 0x1000) != 0;
            int palette = (attr >> 13) & 3;
            bool priority = (attr & 0x8000) != 0;
            int y_in = vflip ? (height * 8 - 1 - y) : y;
            int tile_row = y_in >> 3, row = y_in & 7;
            uint16_t tile_base = attr & 0x7ff;
            for (int col = 0; col < spr_width; col++) {
                int tile_col = hflip ? (spr_width - 1 - col) : col;
                uint16_t tile = (uint16_t)(tile_base + tile_row + tile_col * height);
                int x = xpos - 128 + col * 8;
                uint32_t bits = tile_row_bits(v, tile, row);
                for (int px = 0; px < 8; px++) {
                    int dest = x + px;
                    if (dest < 0 || dest >= width) continue;
                    int src = hflip ? px : (7 - px);
                    uint8_t pix = (uint8_t)((bits >> (src * 4)) & 0x0f);
                    if (pix == 0) continue;
                    if (occ[dest]) v->sprite_collision = true;
                    else {
                        occ[dest] = 1;
                        color[dest] = (uint8_t)((palette << 4) | pix);
                        pri[dest] = priority ? 1 : 0;
                    }
                }
            }
        }
        drawn++;
        pixels += spr_width * 8;
        if (pixels >= width) break;
        if (next == 0 || next == link) break;
        link = next;
    }
}

static void render_line(md_vdp *v, int line) {
    int width = v->h40_ ? 320 : 256;
    uint8_t backdrop = (uint8_t)(v->regs[0x07] & 0x3f);
    bool sh = (v->regs[0x0c] & 0x08) != 0;
    uint8_t a_col[MD_VDP_W], a_pri[MD_VDP_W], b_col[MD_VDP_W], b_pri[MD_VDP_W], s_col[MD_VDP_W], s_pri[MD_VDP_W];
    memset(a_col, 0, sizeof a_col); memset(a_pri, 0, sizeof a_pri);
    memset(b_col, 0, sizeof b_col); memset(b_pri, 0, sizeof b_pri);
    memset(s_col, 0, sizeof s_col); memset(s_pri, 0, sizeof s_pri);
    if (v->regs[1] & 0x40) {
        draw_plane(v, line, false, b_col, b_pri);
        draw_plane(v, line, true, a_col, a_pri);
        draw_window(v, line, a_col, a_pri);
        draw_sprites(v, line, s_col, s_pri);
    }
    int left = v->h40_ ? 0 : 32;
    uint16_t bd = to565(v->palette[backdrop]);
    for (int x = 0; x < MD_VDP_W; x++) v->line[x] = bd;
    memset(v->line_backdrop, 1, sizeof v->line_backdrop);
    for (int x = 0; x < width; x++) {
        uint8_t color = backdrop, layer_pri = 0;
        bool is_sprite = false, drawn = false;
#define PLOT(c, p, s) do { if (c) { drawn = true; color = (c); layer_pri = (p); is_sprite = (s); } } while (0)
        PLOT(b_col[x], b_pri[x], false);
        if (a_pri[x] == 0) PLOT(a_col[x], a_pri[x], false);
        if (s_pri[x] == 0) PLOT(s_col[x], s_pri[x], true);
        if (b_pri[x] != 0) PLOT(b_col[x], b_pri[x], false);
        if (a_pri[x] != 0) PLOT(a_col[x], a_pri[x], false);
        if (s_pri[x] != 0) PLOT(s_col[x], s_pri[x], true);
#undef PLOT
        bool shadow = false, highlight = false;
        if (sh) {
            shadow = layer_pri == 0;
            if (is_sprite && (s_col[x] & 0x3f) == 0x3e) {
                highlight = true;
                shadow = false;
                color = backdrop;
                layer_pri = 0;
                if (b_col[x]) { color = b_col[x]; layer_pri = b_pri[x]; }
                if (a_col[x] && (a_pri[x] >= layer_pri || color == backdrop)) color = a_col[x];
            } else if (is_sprite && (s_col[x] & 0x3f) == 0x3f) {
                shadow = true;
                highlight = false;
                color = backdrop;
                if (b_col[x]) color = b_col[x];
                if (a_col[x]) color = a_col[x];
            }
        }
        v->line[left + x] = to565(palette_rgb(v, color, shadow, highlight));
        v->line_backdrop[left + x] = drawn ? 0 : 1;
    }
}

void md_vdp_scanline(md_vdp *v, int line) {
    v->current_line = line;
    v->h40_ = (v->regs[0x0c] & 0x81) != 0;
    raise_z80(v, false);
    if (line == 0) v->hint_counter = (int)v->regs[0x0a];
    if (line < v->visible_scanlines) {
        render_line(v, line);
        if (v->hint_counter == 0) {
            v->irq4_pending = true;
            if (v->regs[0] & 0x10) raise_hint(v, true);
            v->hint_counter = (int)v->regs[0x0a];
        } else v->hint_counter--;
    } else {
        uint16_t bd = to565(v->palette[v->regs[0x07] & 0x3f]);
        for (int x = 0; x < MD_VDP_W; x++) v->line[x] = bd;
        memset(v->line_backdrop, 1, sizeof v->line_backdrop);
    }
    if (line == v->irq6_scanline) {
        v->vblank_flag = true;
        v->irq6_pending = true;
        if (v->regs[1] & 0x20) raise_vint(v, true);
    }
    if (line == v->z80irq_scanline) raise_z80(v, true);
}

void md_vdp_eof(md_vdp *v) {
    v->vblank_flag = false;
    v->sprite_collision = false;
    v->sprite_overflow = false;
    v->imode = (uint8_t)((v->regs[0x0c] & 0x06) >> 1);
    v->imode_odd_frame ^= 1;
    raise_z80(v, false);
    v->total_scanlines = v->pal ? MD_LINES_PAL : MD_LINES_NTSC;
    if (v->regs[1] & 0x08) {
        v->visible_scanlines = 240;
        v->irq6_scanline = 240;
        v->z80irq_scanline = 240;
    } else {
        v->visible_scanlines = 224;
        v->irq6_scanline = 224;
        v->z80irq_scanline = v->pal ? 224 : 226;
    }
}
