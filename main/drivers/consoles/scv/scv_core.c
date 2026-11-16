// scv_core.c - Epoch Super Cassette Vision (1984): a C11 migration of
// dsp-cpp's drivers/consoles/scv (from scv.pas). The uPD7801 (cpu/upd7801)
// with its BIOS and character ROM (rom_data.c), the EPOCH TV-1 video (text,
// semigraphics, blocks and the 128 sprites, drawn once a frame as there) and
// the uPD1771C sound (sound/upd1771). Cartridges of 8 KB to 128 KB with the
// RAM and bank layouts dsp-cpp knows by CRC (Pole Position II's mapper too).
// The BIOS is needed: built in, or loaded as a 5 KB file (BIOS + characters).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/upd7801/upd7801.h"
#include "../../../sound/upd1771.h"
#include "rom_data.h"

#define CPU_CLOCK 2000000
#define LINES 262
#define FPS 59.922745
#define LINE_CYCLES ((int)((double)CPU_CLOCK / (FPS * LINES) + 0.5))
#define W 192
#define H 222
#define RATE 44100
#define MAX_CART 0x20000

static const char *const kExts[] = { ".bin", ".0", ".rom", NULL };
const cons_info cons_machine = {
    "scv", "Epoch Super Cassette Vision", "Epoch, 1984", kExts, "BIN", W, H, FPS, RATE,
    { "1", "2", NULL, NULL }, "PAUSE", NULL, CONS_KEYPAD, 2,
    "0-9: the keypad, Q: CL, W: EN, P: pause",
};

#define RGB(c) (uint16_t)((((c) >> 8) & 0xf800) | (((c) >> 5) & 0x07e0) | (((c) >> 3) & 0x001f))
static const uint16_t kPalette[16] = {
    RGB(0x00009B), RGB(0x000000), RGB(0x0000FF), RGB(0xA100FF), RGB(0x00FF00), RGB(0xA0FF9D),
    RGB(0x00FFFF), RGB(0x00A100), RGB(0xFF0000), RGB(0xFFA100), RGB(0xFF00FF), RGB(0xFFA09F),
    RGB(0xFFFF00), RGB(0xA3A000), RGB(0xA1A09D), RGB(0xFFFFFF),
};
static const uint8_t kSpr2ColLut0[16] = { 0, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 1, 1 };
static const uint8_t kSpr2ColLut1[16] = { 0, 1, 8, 11, 2, 3, 10, 9, 4, 5, 12, 13, 6, 7, 14, 15 };

static struct {
    upd7801 cpu;
    upd1771 sound;
    uint8_t *mem;                // 64 KB
    uint8_t chars[0x400];
    uint8_t *rom;                // 4 x 32 KB
    uint8_t rom_window, rom_bank_type;
    bool ram_bank, ram_bank2, pp2, bios;
    uint8_t porta, portc;
    uint8_t keys[9];
    bool held[CK_COUNT];
    unsigned pads[2];
    uint16_t *fb;
    int16_t audio[1024];
    int naudio, debt;
    bool loaded;
} S;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (S.pp2) {
        if (a <= 0x0fff || (a >= 0x2000 && a <= 0x3403)) return S.mem[a];
        if (a >= 0x8000 && a <= 0xefff) return S.rom[(S.rom_window & 3) * 0x8000 + (a & 0x7fff)];
        if (a >= 0xf000 && a <= 0xff7f) return S.mem[a];
        if (a >= 0xff80) return S.cpu.iram[a & 0x7f];
        return 0xff;
    }
    if (a <= 0x0fff || (a >= 0x2000 && a <= 0x3403)) return S.mem[a];
    if (a >= 0x6000 && a <= 0x7fff) return S.ram_bank ? S.mem[a] : 0xff;
    if (a >= 0x8000 && a <= 0xdfff) return S.rom[(S.rom_window & 3) * 0x8000 + (a & 0x7fff)];
    if (a >= 0xe000 && a <= 0xff7f) return S.ram_bank2 ? S.mem[a] : S.rom[(S.rom_window & 3) * 0x8000 + (a & 0x7fff)];
    if (a >= 0xff80) return S.cpu.iram[a & 0x7f];
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (S.pp2) {
        if (a <= 0x0fff || (a >= 0x8000 && a <= 0xefff)) return;
        if (a >= 0x2000 && a <= 0x3403) { S.mem[a] = v; return; }
        if (a == 0x3600) { upd1771_write(&S.sound, v); return; }
        if (a >= 0xf000 && a <= 0xff7f) { S.mem[a] = v; return; }
        if (a >= 0xff80) S.cpu.iram[a & 0x7f] = v;
        return;
    }
    if (a <= 0x0fff || (a >= 0x8000 && a <= 0xdfff)) return;
    if (a >= 0x2000 && a <= 0x3403) { S.mem[a] = v; return; }
    if (a == 0x3600) { upd1771_write(&S.sound, v); return; }
    if (a >= 0x6000 && a <= 0x7fff) { if (S.ram_bank) S.mem[a] = v; return; }
    if (a >= 0xe000 && a <= 0xff7f) { if (S.ram_bank2) S.mem[a] = v; return; }
    if (a >= 0xff80) S.cpu.iram[a & 0x7f] = v;
}

static uint8_t port_b_in(void *u, uint8_t mask)
{
    (void)u, (void)mask;
    uint8_t d = 0xff;
    for (int f = 0; f < 8; f++)
        if (!(S.porta & (1 << f))) d &= S.keys[f];
    return d;
}

static uint8_t port_c_in(void *u, uint8_t mask)
{
    (void)u, (void)mask;
    return (uint8_t)((S.portc & 0xfe) | (S.keys[8] & 1));
}

static void port_a_out(void *u, uint8_t v) { (void)u; S.porta = v; }

static void port_c_out(void *u, uint8_t v)
{
    (void)u;
    S.portc = v;
    upd1771_pcm_write(&S.sound, v & 0x08);
    if (S.rom_bank_type == 1) S.rom_window = (uint8_t)((v & 0x20) >> 5);
    else if (S.rom_bank_type == 2) S.rom_window = (uint8_t)((v >> 5) & 3);
}

static void cycles(void *u, int c) { (void)u; upd1771_run(&S.sound, c, CPU_CLOCK, S.audio, &S.naudio, 1024); }
static void ack(void *u, bool on) { (void)u; upd7801_set_input_line(&S.cpu, UPD7801_INTF1, on); }

// ---- the picture: dsp-cpp draws into a 256x256 plane and shows (24, 23)
// to (215, 244); here straight into that window ----
static inline void put_pixel(int x, int y, uint16_t c)
{
    if (x < 0 || x >= 256) return;
    const int fx = x - 24, fy = (y & 0xff) - 23;
    if (fx >= 0 && fx < W && fy >= 0 && fy < H) S.fb[fy * W + fx] = c;
}

static void draw_text(int x, int y, uint16_t cd, uint8_t fg, uint8_t bg)
{
    for (int f = 0; f < 8; f++) {
        uint8_t d = S.chars[(cd + f) & 0x3ff];
        for (int h = 0; h < 8; h++) {
            put_pixel(x + h, y + f, kPalette[(d & 0x80) ? fg : bg]);
            d = (uint8_t)(d << 1);
        }
    }
    for (int f = 8; f < 16; f++)
        for (int h = 0; h < 8; h++) put_pixel(x + h, y + f, kPalette[bg]);
}

static void draw_semi(int x, int y, uint8_t data, uint8_t fg)
{
    if (!data) return;
    for (int f = 0; f < 4; f++)
        for (int h = 0; h < 4; h++) put_pixel(x + h, y + f, kPalette[fg]);
}

static void draw_block(int x, int y, uint8_t col)
{
    for (int f = 0; f < 8; f++)
        for (int h = 0; h < 8; h++) put_pixel(x + h, y + f, kPalette[col & 15]);
}

static void plot_part(int x, int y, uint8_t pat, uint8_t col, int start_line)
{
    if (x < 4 || (y + 2) < start_line) return;
    x -= 4;
    const uint16_t c = kPalette[col & 15];
    if (pat & 8) put_pixel(x, y + 2, c);
    if ((pat & 4) && x < 255) put_pixel(x + 1, y + 2, c);
    if ((pat & 2) && x < 254) put_pixel(x + 2, y + 2, c);
    if ((pat & 1) && x < 253) put_pixel(x + 3, y + 2, c);
}

static void draw_sprite(int x, int y, uint8_t tile, uint8_t col, bool left, bool right, bool top, bool bottom,
                        uint8_t clip_y, int start_line)
{
    y += (int)clip_y * 2;
    for (int f = clip_y; f < 8; f++) {
        const int base = 0x2000 + tile * 32 + f * 4;
        const uint8_t p0 = S.mem[base & 0xffff], p1 = S.mem[(base + 1) & 0xffff];
        const uint8_t p2 = S.mem[(base + 2) & 0xffff], p3 = S.mem[(base + 3) & 0xffff];
        if ((top && f * 4 < 16) || (bottom && f * 4 >= 16)) {
            if (left) {
                plot_part(x, y, p0 >> 4, col, start_line);
                plot_part(x + 4, y, p1 >> 4, col, start_line);
            }
            if (right) {
                plot_part(x + 8, y, p2 >> 4, col, start_line);
                plot_part(x + 12, y, p3 >> 4, col, start_line);
            }
            if (left) {
                plot_part(x, y + 1, p0 & 0x0f, col, start_line);
                plot_part(x + 4, y + 1, p1 & 0x0f, col, start_line);
            }
            if (right) {
                plot_part(x + 8, y + 1, p2 & 0x0f, col, start_line);
                plot_part(x + 12, y + 1, p3 & 0x0f, col, start_line);
            }
        }
        y += 2;
    }
}

static void update_video(void)
{
    const uint8_t *m = S.mem;
    const uint8_t fg = m[0x3403] >> 4, bg = m[0x3403] & 0x0f, gr_fg = m[0x3401] >> 4, gr_bg = m[0x3401] & 0x0f;
    const int clip_x = (m[0x3402] & 0x0f) * 2, clip_y = m[0x3402] >> 4;
    for (int i = 0; i < W * H; i++) S.fb[i] = kPalette[gr_bg];
    for (int y = 0; y < 16; y++) {
        const bool text_y = (y < clip_y) ? !(m[0x3400] & 0x80) : (m[0x3400] & 0x80) != 0;
        for (int x = 0; x < 32; x++) {
            const uint8_t d = m[0x3000 + y * 32 + x];
            const bool text_x = (x < clip_x) ? !(m[0x3400] & 0x40) : (m[0x3400] & 0x40) != 0;
            if (text_x && text_y) {
                draw_text(x * 8, y * 16, (uint16_t)((d & 0x7f) * 8), fg, bg);
            } else if ((m[0x3400] & 3) == 1) {
                draw_semi(x * 8, y * 16, d & 0x80, gr_fg);
                draw_semi(x * 8 + 4, y * 16, d & 0x40, gr_fg);
                draw_semi(x * 8, y * 16 + 4, d & 0x20, gr_fg);
                draw_semi(x * 8 + 4, y * 16 + 4, d & 0x10, gr_fg);
                draw_semi(x * 8, y * 16 + 8, d & 0x08, gr_fg);
                draw_semi(x * 8 + 4, y * 16 + 8, d & 0x04, gr_fg);
                draw_semi(x * 8, y * 16 + 12, d & 0x02, gr_fg);
                draw_semi(x * 8 + 4, y * 16 + 12, d & 0x01, gr_fg);
            } else if ((m[0x3400] & 3) == 3) {
                draw_block(x * 8, y * 16, d >> 4);
                draw_block(x * 8, y * 16 + 8, d & 0x0f);
            }
        }
    }
    if (!(m[0x3400] & 0x10)) return;
    int start_line = 0;
    if ((m[0x3400] & 0xf7) == 0x17 && (m[0x3402] & 0xef) == 0x4f) start_line = 21 + 32;
    for (int f = 0; f < 128; f++) {
        int spr_y = m[0x3200 + f * 4] & 0xfe;
        bool y_32 = m[0x3200 + f * 4] & 1;
        uint8_t clip = m[0x3201 + f * 4] >> 4;
        const uint8_t col = m[0x3201 + f * 4] & 0x0f;
        int spr_x = m[0x3202 + f * 4] & 0xfe;
        bool x_32 = m[0x3202 + f * 4] & 1;
        const uint8_t tile = m[0x3203 + f * 4] & 0x7f;
        const bool half = (m[0x3203 + f * 4] & 0x80) != 0;
        bool left = true, right = true, top = true, bottom = true;
        if (col == 0 || spr_y == 0) continue;
        if (half) {
            if (tile & 0x40) {
                if (y_32) {
                    spr_y -= 8;
                    top = false;
                    bottom = true;
                    y_32 = false;
                } else {
                    top = true;
                    bottom = false;
                }
            }
            if (x_32) {
                spr_x -= 8;
                left = false;
                right = true;
                x_32 = false;
            } else {
                left = true;
                right = false;
            }
        }
        if ((m[0x3400] & 0x20) && (f & 0x20)) {
            draw_sprite(spr_x, spr_y, tile, col, left, right, top, bottom, clip, start_line);
            if (x_32 || y_32) {
                const uint8_t c2 = (f & 0x40) ? kSpr2ColLut1[col] : kSpr2ColLut0[col];
                draw_sprite(spr_x, spr_y, (uint8_t)(tile ^ (8 * (int)x_32 + (int)y_32)), c2, left, right, top, bottom,
                            clip, start_line);
            }
        } else {
            draw_sprite(spr_x, spr_y, tile, col, left, right, top, bottom, clip, start_line);
            if (x_32) draw_sprite(spr_x + 16, spr_y, (uint8_t)(tile | 8), col, true, true, top, bottom, clip, start_line);
            if (y_32) {
                clip = (clip & 8) ? (clip & 7) : 0;
                draw_sprite(spr_x, spr_y + 16, (uint8_t)(tile | 1), col, left, right, true, true, clip, start_line);
                if (x_32) draw_sprite(spr_x + 16, spr_y + 16, (uint8_t)(tile | 9), col, true, true, true, true, clip, start_line);
            }
        }
    }
}

// ---- the console ----
static void keys_update(void)
{
    memset(S.keys, 0xff, sizeof S.keys);
#define CLR(i, m) (S.keys[i] &= (uint8_t)~(m))
    static const struct { int key, idx, mask; } k[] = {
        { CK_0, 2, 0x40 }, { CK_1, 2, 0x80 }, { CK_2, 3, 0x40 }, { CK_3, 3, 0x80 }, { CK_4, 4, 0x40 },
        { CK_5, 4, 0x80 }, { CK_6, 5, 0x40 }, { CK_7, 5, 0x80 }, { CK_8, 6, 0x40 }, { CK_9, 6, 0x80 },
        { CK_Q, 7, 0x40 }, { CK_W, 7, 0x80 }, { CK_P, 8, 0x01 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (S.held[k[i].key]) CLR(k[i].idx, k[i].mask);
    const unsigned p1 = S.pads[0], p2 = S.pads[1];
    if (p1 & CP_START) CLR(8, 0x01);
    if (p1 & CP_LEFT) CLR(0, 0x01);
    if (p1 & CP_UP) CLR(0, 0x02);
    if (p1 & CP_B1) CLR(0, 0x04);
    if (p2 & CP_LEFT) CLR(0, 0x08);
    if (p2 & CP_UP) CLR(0, 0x10);
    if (p2 & CP_B1) CLR(0, 0x20);
    if (p1 & CP_DOWN) CLR(1, 0x01);
    if (p1 & CP_RIGHT) CLR(1, 0x02);
    if (p1 & CP_B2) CLR(1, 0x04);
    if (p2 & CP_DOWN) CLR(1, 0x08);
    if (p2 & CP_RIGHT) CLR(1, 0x10);
    if (p2 & CP_B2) CLR(1, 0x20);
#undef CLR
}

bool cons_init(void)
{
    S.mem = (uint8_t *)calloc(1, 0x10000);
    S.rom = (uint8_t *)calloc(4, 0x8000);
    S.fb = (uint16_t *)calloc(W * H, sizeof(uint16_t));
    if (!S.mem || !S.rom || !S.fb) return false;
    if (scv_rom_bios && scv_rom_chr) {
        memcpy(S.mem, scv_rom_bios, 0x1000);
        memcpy(S.chars, scv_rom_chr, 0x400);
        S.bios = true;
    }
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.port_b_in = port_b_in;
    S.cpu.port_c_in = port_c_in;
    S.cpu.port_a_out = port_a_out;
    S.cpu.port_c_out = port_c_out;
    S.cpu.cycles = cycles;
    upd1771_init(&S.sound, 6000000, 10.0f);
    S.sound.ack = ack;
    cons_reset();
    return true;
}

void cons_reset(void)
{
    upd7801_reset(&S.cpu);
    upd1771_reset(&S.sound);
    S.porta = S.portc = 0xff;
    memset(S.keys, 0xff, sizeof S.keys);
    S.rom_window = 0;
    S.debt = 0;
    for (int i = 0; i < W * H; i++) S.fb[i] = kPalette[1];
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || !size) {
        if (why) *why = "empty SCV cartridge";
        free(data);
        return false;
    }
    if (size == 0x1400) {                    // the BIOS + the characters
        memcpy(S.mem, data, 0x1000);
        memcpy(S.chars, data + 0x1000, 0x400);
        S.bios = true;
        free(data);
        cons_reset();
        return true;
    }
    if (!S.bios) {
        if (why) *why = "the SCV BIOS is needed first (upd7801g.s01 + epochtv.chr, 5 KB)";
        free(data);
        return false;
    }
    const uint32_t crc = cons_crc32(data, size);
    S.ram_bank = S.ram_bank2 = S.pp2 = false;
    switch (crc) {
    case 0x5971940f: case 0x84005c4c: case 0xca965c2b: S.ram_bank2 = true; break;
    case 0xcc4fb04d: S.ram_bank = true; break;
    case 0xcb69903d: case 0x5b3a04e0: S.pp2 = true; break;
    default: break;
    }
    memset(S.mem + 0x1000, 0, 0x10000 - 0x1000);
    memset(S.rom, 0, 4 * 0x8000);
    S.rom_bank_type = 0;
    S.rom_window = 0;
    const size_t n = size < MAX_CART ? size : MAX_CART;
    if (n <= 0x2000) {
        for (int m = 0; m < 4; m++) memcpy(S.rom + m * 0x2000, data, n);
    } else if (n <= 0x4000) {
        memcpy(S.rom, data, n);
        memcpy(S.rom + 0x4000, data, n < 0x4000 ? n : 0x4000);
    } else if (n <= 0x8000) {
        memcpy(S.rom, data, n);
    } else if (n <= 0x10000) {
        memcpy(S.rom, data, n);
        S.rom_bank_type = 1;
    } else {
        memcpy(S.rom, data, n);
        S.rom_bank_type = 2;
    }
    free(data);
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    memset(S.rom, 0, 4 * 0x8000);
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    if (!S.bios) {
        for (int i = 0; i < 735; i++) S.audio[i] = 0;
        return 735;
    }
    keys_update();
    for (int line = 0; line < LINES; line++) {
        if (line == 7) upd7801_set_input_line(&S.cpu, UPD7801_INTF2, 0);
        if (line == 240) {
            update_video();
            upd7801_set_input_line(&S.cpu, UPD7801_INTF2, 1);
        }
        const int want = LINE_CYCLES - S.debt;      // (the overrun carried to the next line)
        S.debt = want > 0 ? upd7801_run(&S.cpu, want) - want : -want;
    }
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = W;
    *h = H;
    *stride = W;
    return S.fb;
}

void cons_pad(int player, unsigned b) { S.pads[player & 1] = b; }
void cons_key(int key, bool down) { if (key >= 0 && key < CK_COUNT) S.held[key] = down; }
const char *cons_status(void) { return S.bios ? NULL : "no BIOS: load upd7801g.s01 + epochtv.chr"; }
