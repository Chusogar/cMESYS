// cpc_core.c - Amstrad CPC 464 / 664 / 6128, see cpc_core.h. A C11 port of
// dsp-cpp's drivers/computers/amstrad_cpc.cpp (the CRTC, Gate Array, PPI,
// memory and I/O maps as there); the picture as line records (cpc_core.h).
#include "cpc_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80dsp/z80dsp.h"
#include "ay8910.h"
#include "zx_nec765.h"
#include "zx_tape.h"

#define CPU_CLOCK 4000000
#define AY_CLOCK 1000000
#define CYCLES_PER_FRAME 79872          // 312 lines * 64 chars * 4 T-states
#define SW CPC_FULL_W
#define SH CPC_FULL_H

// CRTC register write masks (write_crtc of amstrad_cpc.pas)
static const uint8_t kCrtcMasks[18] = { 0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 3, 0x1f,
                                        0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff, 0xff, 0xff };

// Hardware colours (colour monitor) and the green monitor's brightness
static const uint32_t kCpcPalette[32] = {
    0x808080, 0x808080, 0x00FF80, 0xFFFF80, 0x000080, 0xFF0080, 0x008080, 0xFF8080,
    0xFF0080, 0xFFFF80, 0xFFFF00, 0xFFFFFF, 0xFF0000, 0xFF00FF, 0xFF8000, 0xFF80FF,
    0x000080, 0x00FF80, 0x00FF00, 0x00FFFF, 0x000000, 0x0000FF, 0x008000, 0x0080FF,
    0x800080, 0x80FF80, 0x80FF00, 0x80FFFF, 0x800000, 0x8000FF, 0x808000, 0x8080FF,
};
static const uint8_t kGreen[32] = {     // kGreenClassic * 255
    144, 144, 192, 240, 48, 96, 120, 168, 96, 240, 232, 248, 88, 104, 160, 176,
    48, 192, 184, 200, 40, 56, 112, 128, 72, 216, 208, 224, 64, 80, 136, 152,
};

// Gate Array wait states: almost every opcode stretched to a multiple of 4
// T-states so the CRTC (a character every 4 T) stays locked to the CPU
static const uint8_t kCpcMain[256] = {
    4, 12, 8, 8, 4, 4, 8, 4, 4, 12, 8, 8, 4, 4, 8, 4,
    12, 12, 8, 8, 4, 4, 8, 4, 12, 12, 8, 8, 4, 4, 8, 4,
    8, 12, 20, 8, 4, 4, 8, 4, 8, 12, 20, 8, 4, 4, 8, 4,
    8, 12, 16, 8, 12, 12, 12, 4, 8, 12, 16, 8, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    8, 12, 12, 12, 12, 16, 8, 16, 8, 12, 12, 4, 12, 20, 8, 16,
    8, 12, 12, 12, 12, 16, 8, 16, 8, 4, 12, 12, 12, 4, 8, 16,
    8, 12, 12, 24, 12, 16, 8, 16, 8, 4, 12, 4, 12, 4, 8, 16,
    8, 12, 12, 4, 12, 16, 8, 16, 8, 8, 12, 4, 12, 4, 8, 16};
static const uint8_t kCpcCb[256] = {
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4,
    4, 4, 4, 4, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 12, 4};
static const uint8_t kCpcEd[256] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    12, 12, 12, 20, 4, 12, 4, 8, 12, 12, 12, 20, 4, 12, 4, 8,
    12, 12, 12, 20, 4, 12, 4, 8, 12, 12, 12, 20, 4, 12, 4, 8,
    12, 12, 12, 20, 4, 12, 4, 16, 12, 12, 12, 20, 4, 12, 4, 16,
    12, 12, 12, 20, 4, 12, 4, 4, 12, 12, 12, 20, 4, 12, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    16, 12, 16, 16, 4, 4, 4, 4, 16, 12, 16, 16, 4, 4, 4, 4,
    16, 12, 16, 16, 4, 4, 4, 4, 16, 12, 16, 16, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
static const uint8_t kCpcIndex[256] = {
    4, 12, 8, 8, 4, 4, 8, 4, 4, 12, 8, 8, 4, 4, 8, 4,
    12, 12, 8, 8, 4, 4, 8, 4, 12, 12, 8, 8, 4, 4, 8, 4,
    8, 12, 20, 8, 4, 4, 8, 4, 8, 12, 20, 8, 4, 4, 8, 4,
    8, 12, 16, 8, 20, 20, 20, 4, 8, 12, 16, 8, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    16, 16, 16, 16, 16, 16, 4, 16, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    4, 4, 4, 4, 4, 4, 16, 4, 4, 4, 4, 4, 4, 4, 16, 4,
    8, 12, 12, 12, 12, 16, 8, 16, 8, 12, 12, 8, 12, 20, 8, 16,
    8, 12, 12, 12, 12, 16, 8, 16, 8, 4, 12, 12, 12, 4, 8, 16,
    8, 12, 12, 24, 12, 16, 8, 16, 8, 4, 12, 4, 12, 4, 8, 16,
    8, 12, 12, 4, 12, 16, 8, 16, 8, 8, 12, 4, 12, 4, 8, 16};
static const uint8_t kCpcIndexCb[256] = {
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
static const uint8_t kCpcExtra[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 8, 4, 4, 0, 0, 0, 0, 4, 8, 4, 4, 0, 0, 0, 0,
    8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
    8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
    8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
    8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0};

// CRTC 6845 state (tcpc_crt)
typedef struct {
    uint8_t regs[18], reg;
    uint16_t char_total, pixel_visible, borde, pant_x, pant_addr;
    bool was_hsync, was_vsync, line_is_visible;
    uint8_t character_counter, hsync_counter, vsync_counter;
    bool next_line_no_visible, next_line_is_visible, state_hsync, is_in_adjustment_period, state_vsync;
    uint8_t state_row_address, adj_count;
    uint16_t end_of_line_address, state_refresh_address, line_address;
    uint8_t char_crt;
} crtc_t;

// Gate Array state (tcpc_ga)
typedef struct {
    uint8_t pen, video_mode, nvideo, pal[17], lines_count, lines_sync, rom_selected;
    bool rom_low, rom_high, change_video;
    uint8_t marco[4], marco_latch;
} ga_t;

// PPI 8255 latches (tcpc_ppi) and the chip's own
typedef struct {
    uint8_t port_a_read_latch, port_a_write_latch;
    bool tape_motor;
    uint8_t ay_control, keyb_line, port_c;
    uint8_t keyb_val[16];
    uint8_t control, latch_a, latch_b, latch_c;     // the 8255
} ppi_t;

// One line of the picture as the beam drew it (see cpc_core.h)
typedef struct {
    uint16_t addr;              // video address of its first byte
    int16_t x;                  // column of its first byte
    uint8_t n;                  // bytes shown (4 pixels each)
    uint8_t mode, border;       // video mode; border hardware colour
    uint8_t pal[16];            // hardware colour of each pen
} cpc_line;

struct cpc {
    cpc_model_t model;
    int ram_pages, audio_rate;
    zd_z80 cpu;
    ay8910 ay;
    zx_nec765 fdc;
    zx_tape tape;
    uint8_t *ram[8];                    // 16 KB pages (the ESP32 has no 128 KB block)
    const uint8_t *lower_rom;
    const uint8_t *upper_roms[16];
    crtc_t crt;
    ga_t ga;
    ppi_t ppi;
    uint8_t keys[16], joy;          // what the frontend holds (active low / bits)
    bool mod_address, color_monitor, tape_ear;
    int video_acc, cpc_line_;
    cpc_line lines[2][SH];          // [0] being drawn, [1] the last frame
    bool frame_presented;
    int cycle_debt;
    int64_t tape_acc, audio_acc;
    int16_t audio[CPC_AUDIO_MAX];
    int naudio;
    uint32_t palette[32];           // 0xRRGGBB
    uint16_t palette565[32];
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
    const char *error;
    char error_buf[96];
};

// ---------------------------------------------------------------------------
// palette
// ---------------------------------------------------------------------------
static void build_palette(cpc *m)
{
    for (int i = 0; i < 32; i++) {
        uint32_t rgb = m->color_monitor ? kCpcPalette[i] : (uint32_t)kGreen[i] << 8;
        m->palette[i] = rgb;
        m->palette565[i] = m->rgb565 ? m->rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb)
                                     : (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x1f));
    }
}

void cpc_set_rgb565(cpc *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b))
{
    m->rgb565 = fn;
    build_palette(m);
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
static uint8_t mem_read(void *user, uint16_t a)
{
    cpc *m = (cpc *)user;
    if (a <= 0x3fff) {
        if (m->ga.rom_low) return m->lower_rom ? m->lower_rom[a] : 0xff;
        return m->ram[m->ga.marco[0]][a];
    }
    if (a <= 0x7fff) return m->ram[m->ga.marco[1]][a & 0x3fff];
    if (a <= 0xbfff) return m->ram[m->ga.marco[2]][a & 0x3fff];
    if (m->ga.rom_high && m->upper_roms[m->ga.rom_selected]) return m->upper_roms[m->ga.rom_selected][a & 0x3fff];
    return m->ram[m->ga.marco[3]][a & 0x3fff];
}

static void mem_write(void *user, uint16_t a, uint8_t v)
{
    cpc *m = (cpc *)user;
    m->ram[m->ga.marco[a >> 14]][a & 0x3fff] = v;
}

static inline uint8_t vram(const cpc *m, uint16_t addr)      // the base 64K always
{
    return m->ram[addr >> 14][addr & 0x3fff];
}

// ---------------------------------------------------------------------------
// Gate Array / CRTC
// ---------------------------------------------------------------------------
static void write_ram_banking(cpc *m, uint8_t value)
{
    if (m->model != CPC_6128) return;   // only the 6128 has the bank switcher
    m->ga.marco[0] = 0; m->ga.marco[1] = 1; m->ga.marco[2] = 2; m->ga.marco[3] = 3;
    const uint8_t P = 4;                // the extra 64K
    switch (value & 7) {
    case 1: m->ga.marco[3] = 3 + P; break;
    case 2: m->ga.marco[0] = 0 + P; m->ga.marco[1] = 1 + P; m->ga.marco[2] = 2 + P; m->ga.marco[3] = 3 + P; break;
    case 3: m->ga.marco[1] = 3; m->ga.marco[3] = 3 + P; break;
    case 4: m->ga.marco[1] = 0 + P; break;
    case 5: m->ga.marco[1] = 1 + P; break;
    case 6: m->ga.marco[1] = 2 + P; break;
    case 7: m->ga.marco[1] = 3 + P; break;
    default: break;
    }
    m->ga.marco_latch = value & 7;
}

static void write_ga(cpc *m, uint8_t value)
{
    switch (value >> 6) {
    case 0: m->ga.pen = (value & 0x10) == 0 ? (uint8_t)(value & 0x0f) : 0x10; break;
    case 1: m->ga.pal[m->ga.pen] = value & 0x1f; break;
    case 2:
        if (m->ga.nvideo != (value & 3)) {
            m->ga.nvideo = value & 3;
            m->ga.change_video = true;
        }
        m->ga.rom_low = (value & 4) == 0;
        m->ga.rom_high = (value & 8) == 0;
        if (value & 0x10) {
            m->ga.lines_count = 0;
            zd_set_irq(&m->cpu, ZD_CLEAR, 0xff);
        }
        break;
    default: write_ram_banking(m, value); break;
    }
}

static void write_crtc(cpc *m, uint8_t port, uint8_t value)
{
    crtc_t *c = &m->crt;
    switch (port & 3) {
    case 0: c->reg = value & 0x1f; break;
    case 1: {
        if (c->reg >= 18) break;
        const uint8_t v = value & kCrtcMasks[c->reg];
        if (c->regs[c->reg] == v) break;
        c->regs[c->reg] = v;
        switch (c->reg) {
        case 0: c->char_total = (uint16_t)((c->regs[0] + 1) * 8); break;
        case 1:
            c->pixel_visible = (uint16_t)((c->regs[1] < 50 ? c->regs[1] : 49) * 8);
            c->borde = (uint16_t)((SW - c->pixel_visible) / 2);
            break;
        case 5: c->is_in_adjustment_period = false; c->adj_count = 0; break;
        case 9: m->mod_address = true; break;
        default: break;
        }
        break;
    }
    default: break;
    }
}

static uint8_t read_crtc(const cpc *m, uint8_t port)
{
    switch (port & 3) {
    case 2: return 0x80;
    case 3: return m->crt.reg >= 12 && m->crt.reg <= 17 ? m->crt.regs[m->crt.reg] : 0;
    default: return 0;
    }
}

// ---------------------------------------------------------------------------
// PPI 8255 and the AY on its port A
// ---------------------------------------------------------------------------
static uint8_t keyboard_scan(const cpc *m)
{
    return m->ppi.keyb_line < 16 ? m->ppi.keyb_val[m->ppi.keyb_line] : 0xff;
}

static void update_ay(cpc *m)
{
    switch (m->ppi.ay_control) {
    case 0: m->ppi.port_a_read_latch = 0xff; break;
    case 1: m->ppi.port_a_read_latch = m->ay.latch == 14 ? keyboard_scan(m) : ay8910_data_r(&m->ay); break;
    case 2: ay8910_data_w(&m->ay, m->ppi.port_a_write_latch); break;
    case 3: ay8910_address_w(&m->ay, m->ppi.port_a_write_latch); break;
    default: break;
    }
}

static void tape_motor(cpc *m, bool on)
{
    if (!zt_is_loaded(&m->tape)) return;
    if (on) zt_play(&m->tape, !zt_is_paused(&m->tape));
    else if (zt_is_playing(&m->tape)) zt_pause(&m->tape);
}

static void port_c_write(cpc *m, uint8_t value)
{
    const uint8_t old = m->ppi.port_c;
    m->ppi.port_c = value;
    m->ppi.keyb_line = value & 0x0f;                     // PC0-3 keyboard row
    const bool motor = (value & 0x10) != 0;              // PC4 cassette motor
    if (motor != m->ppi.tape_motor) {
        m->ppi.tape_motor = motor;
        tape_motor(m, motor);
    }
    const uint8_t ayc = (value >> 6) & 3;                // PC6-7 AY control
    if (ayc != m->ppi.ay_control || ((old ^ value) & 0xc0)) {
        m->ppi.ay_control = ayc;
        update_ay(m);
    }
}

static uint8_t ppi_read(cpc *m, int port)
{
    switch (port & 3) {
    case 0: update_ay(m); return m->ppi.port_a_read_latch;
    case 1: {
        // bit 0 VSYNC, 1-6 fixed ($7e: Amstrad, 50 Hz, ...), bit 7 cassette data
        uint8_t v = 0x7e;
        if (m->crt.state_vsync) v |= 0x01;
        if (zt_is_playing(&m->tape) && m->tape_ear) v |= 0x80;
        return v;
    }
    case 2: return m->ppi.port_c;
    default: return m->ppi.control;
    }
}

static void ppi_write(cpc *m, int port, uint8_t value)
{
    switch (port & 3) {
    case 0:
        m->ppi.latch_a = value;
        m->ppi.port_a_write_latch = value;
        update_ay(m);
        break;
    case 1: m->ppi.latch_b = value; break;
    case 2: m->ppi.latch_c = value; port_c_write(m, value); break;
    default:
        if (value & 0x80) {
            m->ppi.control = value;                     // mode set
        } else {                                        // bit set / reset of port C
            const uint8_t mask = (uint8_t)(1u << ((value >> 1) & 7));
            if (value & 1) m->ppi.latch_c |= mask;
            else m->ppi.latch_c &= (uint8_t)~mask;
            port_c_write(m, m->ppi.latch_c);
        }
        break;
    }
}

static void ppi_reset(cpc *m)
{
    m->ppi.control = 0x9b;
    m->ppi.latch_a = m->ppi.latch_b = m->ppi.latch_c = 0xff;
    m->ppi.port_a_write_latch = 0xff;
    update_ay(m);
    port_c_write(m, 0xff);
}

// ---------------------------------------------------------------------------
// I/O: several devices can answer one access (cpc_inbyte / cpc_outbyte)
// ---------------------------------------------------------------------------
static uint8_t port_in(void *user, uint16_t port)
{
    cpc *m = (cpc *)user;
    uint8_t r = 0xff;
    if ((port & 0x4000) == 0) r = read_crtc(m, (uint8_t)(port >> 8));
    if ((port & 0x0800) == 0) r = ppi_read(m, (port >> 8) & 3);
    if ((port & 0x0400) == 0 && (port & 0x80) == 0 && m->model != CPC_464) {   // FDC
        switch (port & 0x101) {
        case 0x100: r = nec_read_status(&m->fdc); break;
        case 0x101: r = nec_read_data(&m->fdc); break;
        default: break;
        }
    }
    return r;
}

static void port_out(void *user, uint16_t port, uint8_t value)
{
    cpc *m = (cpc *)user;
    if ((port & 0xc000) == 0x4000) write_ga(m, value);
    else if ((port & 0x4000) == 0) write_crtc(m, (uint8_t)(port >> 8), value);
    if ((port & 0x2000) == 0) {
        const uint8_t slot = value & 0x0f;
        m->ga.rom_selected = m->upper_roms[slot] ? slot : 0;
    }
    if ((port & 0x1000) == 0) return;               // printer
    if ((port & 0x0800) == 0) ppi_write(m, (port >> 8) & 3, value);
    if ((port & 0x0400) == 0 && (port & 0x80) == 0 && m->model != CPC_464) {   // FDC
        switch (port & 0x101) {
        case 0: case 1: nec_write_motor(&m->fdc, value); break;
        case 0x100: case 0x101: nec_write_data(&m->fdc, value); break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// video: the CRTC and Gate Array a character clock (4 T) at a time
// ---------------------------------------------------------------------------
static void adjust_vertical_total(cpc *m)
{
    crtc_t *c = &m->crt;
    if (c->adj_count == c->regs[5]) {
        c->is_in_adjustment_period = false;
        c->line_address = (uint16_t)((c->regs[12] << 8) | c->regs[13]);
        c->state_refresh_address = c->line_address;
        c->adj_count = 0;
        c->next_line_is_visible = true;
    } else {
        c->adj_count = (c->adj_count + 1) & 0x1f;
    }
}

static void present_frame(cpc *m)
{
    memcpy(m->lines[1], m->lines[0], sizeof m->lines[0]);
    m->frame_presented = true;
}

static void do_end_of_line(cpc *m)
{
    crtc_t *c = &m->crt;
    if (m->ga.change_video) {
        m->ga.video_mode = m->ga.nvideo;
        m->ga.change_video = false;
        if (m->mod_address) {
            c->line_address = (uint16_t)((c->regs[12] << 8) | c->regs[13]);
            c->state_refresh_address = c->line_address;
            m->mod_address = false;
        }
    }
    m->cpc_line_ = (m->cpc_line_ + 1) % SH;
    c->pant_x = 0;
    c->pant_addr = (uint16_t)(((c->line_address & 0x3ff) << 1) | ((c->state_row_address & 7) << 11) |
                              ((c->line_address & 0x3000) << 2));
    if (c->next_line_is_visible) { c->line_is_visible = true; c->next_line_is_visible = false; }
    if (c->next_line_no_visible) { c->line_is_visible = false; c->next_line_no_visible = false; }

    if (c->state_vsync) {
        uint8_t t = c->regs[3] >> 4;
        if (t == 0) t = 16;
        if (c->vsync_counter == t) c->state_vsync = false;
        else c->vsync_counter++;
    }
    if (c->regs[4] >= c->regs[7] && c->char_crt == c->regs[7]) {
        c->state_vsync = true;
        c->vsync_counter = 0;
    }
    if (c->is_in_adjustment_period) {
        adjust_vertical_total(m);
    } else {
        if (c->state_row_address == c->regs[9]) {
            c->state_row_address = 0;
            c->line_address = c->end_of_line_address;
            if (c->char_crt == c->regs[4]) {
                c->is_in_adjustment_period = true;
                c->adj_count = 0;
                c->char_crt = 0;
                adjust_vertical_total(m);
            } else {
                c->char_crt = (c->char_crt + 1) & 0x7f;
            }
        } else {
            c->state_row_address = (c->state_row_address + 1) & 0x1f;
        }
        if (c->char_crt == c->regs[6]) c->next_line_no_visible = true;
    }
    if (!c->was_vsync && c->state_vsync) m->ga.lines_sync = 2;
    if (c->was_vsync && !c->state_vsync) {
        present_frame(m);
        m->cpc_line_ = 0;
    }
    c->was_vsync = c->state_vsync;
    // the new line: the border until the beam draws on it
    cpc_line *l = &m->lines[0][m->cpc_line_];
    l->n = 0;
    l->border = m->ga.pal[0x10];
}

// draw_pixels: the two bytes of a character (only recorded: cpc_render_row draws them)
static void draw_pixels(cpc *m)
{
    crtc_t *c = &m->crt;
    for (int g = 0; g < 2; g++) {
        if (c->pant_x < c->pixel_visible) {
            cpc_line *l = &m->lines[0][m->cpc_line_];
            if (l->n == 0) {
                l->addr = c->pant_addr;
                l->x = (int16_t)(c->borde + c->pant_x);
                l->mode = m->ga.video_mode;
                memcpy(l->pal, m->ga.pal, 16);
            }
            if (l->n < 255) l->n++;
            if ((c->pant_addr & 0x7ff) == 0x7ff) c->pant_addr &= 0xf800;
            else c->pant_addr++;
        }
        c->pant_x += 4;
    }
}

static void advance_video(cpc *m, int cycles)
{
    crtc_t *c = &m->crt;
    m->video_acc += cycles;
    while (m->video_acc >= 4) {
        m->video_acc -= 4;
        if (c->pant_x < c->char_total && c->line_is_visible && !c->state_vsync && !c->state_hsync) draw_pixels(m);
        if (c->state_hsync) {
            uint8_t t = c->regs[3] & 0x0f;
            if (t == 0) t = 16;
            if (c->hsync_counter == t) c->state_hsync = false;
            else c->hsync_counter++;
        }
        if (c->regs[0] >= c->regs[2] && c->character_counter == c->regs[2]) {
            c->hsync_counter = 0;
            c->state_hsync = true;
        }
        if (c->character_counter == c->regs[1]) c->end_of_line_address = c->state_refresh_address;
        else c->state_refresh_address = (c->state_refresh_address + 1) & 0x3fff;
        if (c->character_counter == c->regs[0]) {
            do_end_of_line(m);
            c->character_counter = 0;
            c->state_refresh_address = c->line_address;
        } else {
            c->character_counter++;
        }
        if (c->was_hsync && !c->state_hsync) {      // the Gate Array's 300 Hz interrupt
            m->ga.lines_count++;
            if (m->ga.lines_sync > 0) {
                if (--m->ga.lines_sync == 0) {
                    if (m->ga.lines_count >= 32) zd_set_irq(&m->cpu, ZD_ASSERT, 0xff);
                    m->ga.lines_count = 0;
                }
            } else if (m->ga.lines_count == 52) {
                m->ga.lines_count = 0;
                zd_set_irq(&m->cpu, ZD_ASSERT, 0xff);
            }
        }
        c->was_hsync = c->state_hsync;
    }
}

// ---------------------------------------------------------------------------
// the CPU's cycles: tape, video, sound
// ---------------------------------------------------------------------------
static void on_cycles(void *user, int cycles)
{
    cpc *m = (cpc *)user;
    if (zt_is_playing(&m->tape)) {
        // the tape player counts 3.5 MHz T-states: x 7/8
        m->tape_acc += (int64_t)cycles * 7;
        const int t = (int)(m->tape_acc / 8);
        m->tape_acc -= (int64_t)t * 8;
        m->tape_ear = zt_advance(&m->tape, t) != 0;
    }
    advance_video(m, cycles);
    m->audio_acc += (int64_t)cycles * m->audio_rate;
    while (m->audio_acc >= CPU_CLOCK) {
        m->audio_acc -= CPU_CLOCK;
        int32_t s = ay8910_render(&m->ay);
        if (zt_is_playing(&m->tape) && m->tape_ear) s += 4000;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (m->naudio < CPC_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)s;
    }
}

static void on_irq_ack(void *user)
{
    cpc *m = (cpc *)user;
    m->ga.lines_count &= 0x1f;
    zd_set_irq(&m->cpu, ZD_CLEAR, 0xff);
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
const char *cpc_title(cpc_model_t model)
{
    switch (model) {
    case CPC_464: return "Amstrad CPC 464";
    case CPC_664: return "Amstrad CPC 664";
    default: return "Amstrad CPC 6128";
    }
}

cpc_model_t cpc_model(const cpc *m) { return m->model; }

cpc *cpc_create(cpc_model_t model, int audio_rate)
{
    cpc *m = (cpc *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->model = model;
    m->audio_rate = audio_rate > 0 ? audio_rate : CPC_AUDIO_RATE;
    m->ram_pages = model == CPC_6128 ? 8 : 4;
    for (int i = 0; i < m->ram_pages; i++)
        if (!(m->ram[i] = (uint8_t *)calloc(1, 0x4000))) {
            cpc_destroy(m);
            return NULL;
        }
    m->color_monitor = true;
    build_palette(m);
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.in = port_in;
    m->cpu.out = port_out;
    m->cpu.cycles = on_cycles;
    m->cpu.irq_ack = on_irq_ack;
    m->cpu.user = m;
    m->cpu.t_main = kCpcMain;
    m->cpu.t_cb = kCpcCb;
    m->cpu.t_ed = kCpcEd;
    m->cpu.t_index = kCpcIndex;
    m->cpu.t_index_cb = kCpcIndexCb;
    m->cpu.t_extra = kCpcExtra;
    m->cpu.irq_align = 4;
    zd_init(&m->cpu);
    ay8910_init(&m->ay, AY_CLOCK, m->audio_rate);
    ay8910_set_amplitude(&m->ay, 0.8f);
    nec_init(&m->fdc);
    zt_init(&m->tape);
    memset(m->keys, 0xff, sizeof m->keys);
    cpc_reset(m);
    return m;
}

void cpc_destroy(cpc *m)
{
    if (!m) return;
    nec_free(&m->fdc);
    zt_free(&m->tape);
    for (int i = 0; i < 8; i++) free(m->ram[i]);
    free(m);
}

void cpc_set_roms(cpc *m, const uint8_t *os, const uint8_t *amsdos)
{
    m->lower_rom = os;
    m->upper_roms[0] = os ? os + 0x4000 : NULL;
    m->upper_roms[7] = m->model != CPC_464 ? amsdos : NULL;
    cpc_reset(m);
}

static void reset_chips(cpc *m)
{
    zd_reset(&m->cpu);
    ay8910_reset(&m->ay);
    nec_reset(&m->fdc);
    memset(&m->ga, 0, sizeof m->ga);
    m->ga.rom_low = m->ga.rom_high = true;
    for (int i = 0; i < 4; i++) m->ga.marco[i] = (uint8_t)i;
    memset(&m->ppi, 0, sizeof m->ppi);
    m->ppi.port_a_read_latch = 0xff;
    m->ppi.port_c = 0xff;
    memset(m->ppi.keyb_val, 0xff, sizeof m->ppi.keyb_val);
    ppi_reset(m);
    m->mod_address = false;
    m->cpc_line_ = 0;
    m->video_acc = 0;
    memset(&m->crt, 0, sizeof m->crt);
    m->crt.char_total = 64 * 8;
    m->crt.pixel_visible = 48 * 8;
    m->crt.borde = (uint16_t)((SW - m->crt.pixel_visible) / 2);
    memset(m->lines, 0, sizeof m->lines);
    for (int i = 0; i < SH; i++) m->lines[0][i].border = m->lines[1][i].border = 20;    // black
    m->cycle_debt = 0;
    m->naudio = 0;
    m->audio_acc = m->tape_acc = 0;
}

void cpc_reset(cpc *m)
{
    reset_chips(m);
    if (zt_is_loaded(&m->tape)) zt_stop(&m->tape);
}

int cpc_run_frame(cpc *m, const int16_t **audio)
{
    memcpy(m->ppi.keyb_val, m->keys, sizeof m->keys);
    if (m->joy) {
        uint8_t j = m->joy;
        m->ppi.keyb_val[9] &= (uint8_t)~(j & 0x3f);
    }
    m->naudio = 0;
    m->frame_presented = false;
    const int want = CYCLES_PER_FRAME - m->cycle_debt;
    const int ran = want > 0 ? zd_run(&m->cpu, want) : 0;
    m->cycle_debt = ran - want;
    if (m->cycle_debt < 0 || m->cycle_debt > CYCLES_PER_FRAME) m->cycle_debt = 0;
    if (!m->frame_presented) present_frame(m);       // no VSYNC yet: the beam's lines
    if (audio) *audio = m->audio;
    return m->naudio;
}

void cpc_keys_clear(cpc *m) { memset(m->keys, 0xff, sizeof m->keys); }

void cpc_key(cpc *m, int code, bool down)
{
    if (code < 0 || code >= 16 * 8) return;
    if (down) m->keys[code >> 3] &= (uint8_t)~(1u << (code & 7));
    else m->keys[code >> 3] |= (uint8_t)(1u << (code & 7));
}

void cpc_joy(cpc *m, uint8_t bits) { m->joy = bits; }

void cpc_set_color_monitor(cpc *m, bool color)
{
    m->color_monitor = color;
    build_palette(m);
}

// ---------------------------------------------------------------------------
// the picture: a recorded line drawn from the RAM
// ---------------------------------------------------------------------------
#define RENDER_ROW(NAME, T, PAL)                                                                \
void NAME(const cpc *m, int y, int x0, int w, T *out)                                           \
{                                                                                               \
    if (y < 0 || y >= SH) { for (int i = 0; i < w; i++) out[i] = 0; return; }                    \
    const cpc_line *l = &m->lines[1][y];                                                        \
    const T bc = m->PAL[l->border & 31];                                                        \
    for (int i = 0; i < w; i++) out[i] = bc;                                                    \
    uint16_t a = l->addr;                                                                       \
    int x = l->x - x0;                                                                          \
    T pen[16];                                                                                  \
    for (int i = 0; i < 16; i++) pen[i] = m->PAL[l->pal[i] & 31];                               \
    for (int b = 0; b < l->n; b++, x += 4) {                                                    \
        const uint8_t v = vram(m, a);                                                           \
        if ((a & 0x7ff) == 0x7ff) a &= 0xf800;                                                  \
        else a++;                                                                               \
        if (x + 4 <= 0 || x >= w) continue;                                                     \
        T s[4];                                                                                 \
        switch (l->mode) {                                                                      \
        case 0: {                                                                               \
            const uint8_t p1 = (uint8_t)(((v & 2) << 2) | ((v & 0x20) >> 3) | ((v & 8) >> 2) | ((v & 0x80) >> 7)); \
            const uint8_t p2 = (uint8_t)(((v & 1) << 3) | ((v & 0x10) >> 2) | ((v & 4) >> 1) | ((v & 0x40) >> 6)); \
            s[0] = s[1] = pen[p1];                                                              \
            s[2] = s[3] = pen[p2];                                                              \
            break;                                                                              \
        }                                                                                       \
        case 1:                                                                                 \
            s[0] = pen[((v & 0x80) >> 7) + ((v & 8) >> 2)];                                     \
            s[1] = pen[((v & 0x40) >> 6) + ((v & 4) >> 1)];                                     \
            s[2] = pen[((v & 0x20) >> 5) + (v & 2)];                                            \
            s[3] = pen[((v & 0x10) >> 4) + ((v & 1) << 1)];                                     \
            break;                                                                              \
        default:    /* mode 2: two pixels blended into each of the 4 */                         \
            for (int p = 0; p < 4; p++) s[p] = BLEND_##T(pen[(v >> (7 - 2 * p)) & 1], pen[(v >> (6 - 2 * p)) & 1]); \
            break;                                                                              \
        }                                                                                       \
        for (int i = 0; i < 4; i++)                                                             \
            if (x + i >= 0 && x + i < w) out[x + i] = s[i];                                     \
    }                                                                                           \
}

static inline uint32_t BLEND_uint32_t(uint32_t a, uint32_t b)
{
    return (((a & 0xfefefe) >> 1) + ((b & 0xfefefe) >> 1));
}
static inline uint16_t BLEND_uint16_t(uint16_t a, uint16_t b)
{
    return (uint16_t)(((a & 0xf7de) >> 1) + ((b & 0xf7de) >> 1));
}
RENDER_ROW(cpc_render_row, uint32_t, palette)
RENDER_ROW(cpc_render_row565, uint16_t, palette565)

// ---------------------------------------------------------------------------
// media
// ---------------------------------------------------------------------------
static bool ends_with_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)s[n - k + i]) != ext[i]) return false;
    return true;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// .sna (abrir_sna_cpc of the DSP emulator, versions 1-3, uncompressed)
static bool load_sna(cpc *m, const uint8_t *h, size_t size)
{
    if (size < 0x100 || memcmp(h, "MV - SNA", 8) != 0) { m->error = "not a CPC snapshot"; return false; }
    const uint8_t version = h[0x10];
    if (version < 1 || version > 3) { m->error = "unsupported CPC SNA version"; return false; }
    const uint16_t mem_kb = rd16(h + 0x6b);
    const size_t mem_size = (size_t)mem_kb * 1024;
    if (mem_kb != 64 && mem_kb != 128) { m->error = "unsupported SNA memory size (compressed v3?)"; return false; }
    if (size < 0x100 + mem_size) { m->error = "truncated snapshot"; return false; }
    if (mem_size > (size_t)m->ram_pages * 0x4000) { m->error = "the snapshot needs a CPC 6128"; return false; }

    reset_chips(m);
    zd_z80 *z = &m->cpu;
    z->f = h[0x11]; z->a = h[0x12]; z->c = h[0x13]; z->b = h[0x14];
    z->e = h[0x15]; z->d = h[0x16]; z->l = h[0x17]; z->h = h[0x18];
    z->r = h[0x19]; z->i = h[0x1a];
    z->iff1 = h[0x1b] & 1; z->iff2 = h[0x1c] & 1;
    z->ix = rd16(h + 0x1d); z->iy = rd16(h + 0x1f); z->sp = rd16(h + 0x21); z->pc = rd16(h + 0x23);
    z->im = h[0x25] & 3;
    z->f2 = h[0x26]; z->a2 = h[0x27]; z->c2 = h[0x28]; z->b2 = h[0x29];
    z->e2 = h[0x2a]; z->d2 = h[0x2b]; z->l2 = h[0x2c]; z->h2 = h[0x2d];
    z->halted = false;
    zd_set_irq(z, ZD_CLEAR, 0xff);

    m->ga.pen = h[0x2e] & 0x1f;
    for (int i = 0; i < 17; i++) m->ga.pal[i] = h[0x2f + i] & 0x1f;
    const uint8_t ga_ctrl = h[0x40];
    write_ga(m, (uint8_t)(0x80 | (ga_ctrl & 0x3f)));
    m->ga.change_video = false;
    m->ga.lines_count = m->ga.lines_sync = 0;
    m->ga.video_mode = m->ga.nvideo = ga_ctrl & 3;
    m->ga.rom_low = (ga_ctrl & 4) == 0;
    m->ga.rom_high = (ga_ctrl & 8) == 0;
    for (int i = 0; i < 4; i++) m->ga.marco[i] = (uint8_t)i;
    write_ram_banking(m, (uint8_t)(0xc0 | (h[0x41] & 7)));

    memset(&m->crt, 0, sizeof m->crt);
    for (int r = 0; r < 18; r++) {
        m->crt.reg = (uint8_t)r;
        write_crtc(m, 1, h[0x43 + r]);
    }
    m->crt.reg = h[0x42] & 0x1f;
    m->crt.pixel_visible = (uint16_t)((m->crt.regs[1] < 50 ? m->crt.regs[1] : 49) * 8);
    m->crt.borde = (uint16_t)((SW - m->crt.pixel_visible) / 2);
    m->crt.char_total = (uint16_t)((m->crt.regs[0] + 1) * 8);
    m->crt.line_address = (uint16_t)((m->crt.regs[12] << 8) | m->crt.regs[13]);
    m->crt.state_refresh_address = m->crt.end_of_line_address = m->crt.line_address;

    const uint8_t slot = h[0x55] & 0x0f;
    m->ga.rom_selected = m->upper_roms[slot] ? slot : 0;

    m->ppi.port_a_read_latch = m->ppi.port_a_write_latch = h[0x56];
    ppi_write(m, 3, h[0x59]);
    m->ppi.latch_a = h[0x56];
    m->ppi.latch_b = h[0x57];
    port_c_write(m, h[0x58]);

    for (int r = 0; r < 16; r++) ay8910_write_reg(&m->ay, (uint8_t)r, h[0x5b + r]);
    ay8910_address_w(&m->ay, h[0x5a] & 0x0f);
    m->ppi.ay_control = 3;
    update_ay(m);
    ay8910_address_w(&m->ay, h[0x5a] & 0x0f);

    if (version == 3) {
        m->ga.lines_sync = h[0xb2];
        m->ga.lines_count = h[0xb3];
        zd_set_irq(z, h[0xb4] ? ZD_ASSERT : ZD_CLEAR, 0xff);
    }
    for (int i = 0; i < m->ram_pages; i++) {
        const size_t o = (size_t)i * 0x4000, k = o < mem_size ? (mem_size - o < 0x4000 ? mem_size - o : 0x4000) : 0;
        memset(m->ram[i], 0, 0x4000);
        if (k) memcpy(m->ram[i], h + 0x100 + o, k);
    }
    return true;
}

bool cpc_load_pbuf(cpc *m, zx_pbuf *pb, const char *name)
{
    m->error = NULL;
    bool ok = false;
    if (!name) name = "";
    if (ends_with_ci(name, ".sna") || zx_pb_eq(pb, 0, "MV - SNA", 8)) {
        uint8_t *flat = zx_pb_flatten(pb, 0, pb->size);
        if (!flat) m->error = "out of memory for the snapshot";
        else {
            ok = load_sna(m, flat, pb->size);
            free(flat);
        }
    } else if (ends_with_ci(name, ".dsk") || ends_with_ci(name, ".edsk") || zx_pb_eq(pb, 0, "MV - CPC", 8) ||
               zx_pb_eq(pb, 0, "EXTENDED", 8)) {
        if (m->model == CPC_464) m->error = "the CPC 464 has no disk drive: use the 664 / 6128";
        else ok = nec_load_pbuf(&m->fdc, 0, pb, &m->error);
    } else {
        ok = zt_load_pbuf(&m->tape, pb, &m->error);
        if (ok) zt_stop(&m->tape);      // the motor starts it (or F6)
    }
    zx_pb_free(pb);                     // (what was not taken over)
    if (!ok && !m->error) m->error = "cannot load the file";
    return ok;
}

bool cpc_load_mem(cpc *m, const uint8_t *data, size_t n, const char *name)
{
    zx_pbuf pb;
    if (!data || !n || !zx_pb_from_mem(&pb, data, n)) {
        m->error = "out of memory";
        return false;
    }
    return cpc_load_pbuf(m, &pb, name);
}

bool cpc_load_file(cpc *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->error = "cannot open the file";
        return false;
    }
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t buf[4096];
    size_t k;
    bool ok = true;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0)
        if (!(ok = zx_pb_append(&pb, buf, k))) break;
    fclose(f);
    if (!ok || !zx_pb_finish(&pb) || pb.size == 0) {
        zx_pb_free(&pb);
        m->error = ok ? "empty file" : "out of memory";
        return false;
    }
    return cpc_load_pbuf(m, &pb, path);
}

const char *cpc_last_error(const cpc *m) { return m->error ? m->error : ""; }

void cpc_tape_toggle(cpc *m)
{
    if (!zt_is_loaded(&m->tape)) return;
    if (zt_is_playing(&m->tape)) zt_pause(&m->tape);
    else zt_play(&m->tape, !zt_is_paused(&m->tape));
}

bool cpc_tape_loaded(const cpc *m) { return zt_is_loaded(&m->tape); }
bool cpc_tape_playing(const cpc *m) { return zt_is_playing(&m->tape); }
