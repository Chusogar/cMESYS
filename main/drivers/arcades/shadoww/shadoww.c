// shadoww.c - Shadow Warriors (Tecmo, 1988) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/shadow_warriors_hw (from
// dsp-emulator's shadow_warriors_hw.pas). A 68000 at 9.2 MHz (Musashi here),
// a Z80 with two YM2203 and an OKI M6295, two scrolling 1024x512 layers of
// 16x16 tiles, 8x8 text and 8x8-built sprites in four priorities, some of
// them blended half and half with what is under them. The graphics are read
// straight from their ROMs, and the picture is built in place (no layers).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68k/m68k.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/okim6295dsp.h"

#define MAIN_CLOCK (18432000 / 2)
#define SOUND_CLOCK 4000000
#define OKI_CLOCK 1000000
#define SCANLINES 256
#define FPS 59.169998
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "shadoww", NULL };
static const arc_rom kHigh[] = {{"shadowa_1.3s", 0x20000, 0, 0x8290d567}};
static const arc_rom kLow[] = {{"shadowa_2.4s", 0x20000, 0, 0xf3f08921}};
static const arc_rom kSound[] = {{"gaiden_3.4b", 0x10000, 0, 0x75fd3e6a}};
static const arc_rom kOki[] = {{"4.4a", 0x20000, 0, 0xb0e0faf9}};
static const arc_rom kChars[] = {{"gaiden_5.7a", 0x10000, 0, 0x8d4035f7}};
static const arc_rom kBg[] = {
    {"14.3a", 0x20000, 0x00000, 0x1ecfddaa}, {"15.3b", 0x20000, 0x20000, 0x1291a696},
    {"16.1a", 0x20000, 0x40000, 0x140b47ca}, {"17.1b", 0x20000, 0x60000, 0x7638cccb}};
static const arc_rom kFg[] = {
    {"18.6a", 0x20000, 0x00000, 0x3fadafd6}, {"19.6b", 0x20000, 0x20000, 0xddae9d5b},
    {"20.4b", 0x20000, 0x40000, 0x08cf7a93}, {"21.4b", 0x20000, 0x60000, 0x1ac892f5}};
// the sprites: byte pairs of 16-bit words (the even file the high byte)
static const arc_rom kSprHigh[] = {
    {"6.3m", 0x20000, 0x00000, 0xe7ccdf9f}, {"8.3n", 0x20000, 0x20000, 0x7ef7f880},
    {"10.3r", 0x20000, 0x40000, 0xa6451dec}, {"12.3s", 0x20000, 0x60000, 0x94a836d8}};
static const arc_rom kSprLow[] = {
    {"7.1m", 0x20000, 0x00000, 0x016bec95}, {"9.1n", 0x20000, 0x20000, 0x6e9b7fd3},
    {"11.1r", 0x20000, 0x40000, 0x7fbfdf5e}, {"13.1s", 0x20000, 0x60000, 0xe9caea3b}};

const arc_info arc_game = { "shadoww", "Shadow Warriors", "Tecmo", 1988, W, H, FPS, RATE, kSets,
                            { "ATTACK", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const int kPX[16] = {0, 4, 8, 12, 16, 20, 24, 28, 256, 260, 264, 268, 272, 276, 280, 284};
static const int kPY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 512, 544, 576, 608, 640, 672, 704, 736};
static const arc_layout k8 = { 8, 8, 0x800, 4, 256, false, false, false, {0, 1, 2, 3}, kPX, kPY };
static const arc_layout kSpr8 = { 8, 8, 0x8000, 4, 256, false, false, false, {0, 1, 2, 3}, kPX, kPY };
static const arc_layout k16 = { 16, 16, 0x1000, 4, 1024, false, false, false, {0, 1, 2, 3}, kPX, kPY };

// the machine on the heap
static struct state {
    zd_z80 sound;
    ym2203d ym0, ym1;
    okim6295d oki;
    const uint8_t *rom, *oki_rom, *chars, *bg, *fg, *sprites;   // blobs, the graphics raw
    uint16_t ram[0x2000], vram1[0x800], vram2[0x1000], vram3[0x1000], sprite_ram[0x1000], pal_ram[0x1000];
    uint16_t palette[0x1000];
    uint8_t sound_rom[0x10000], sound_ram[0x800];   // (the ROM's last 4 KB hidden)
    arc_fb fb;
    uint16_t sx_txt, sy_txt, sx_bg, sy_bg, sx_fg, sy_fg;
    uint8_t sy_txt_off, sy_bg_off, sy_fg_off, soundlatch, in0;
    uint16_t in1, dsw;
    bool flip;
    double main_debt, sound_debt;
    int64_t audio_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

void cpu_pulse_reset(void) {}
void cpu_instr_callback(unsigned int pc) { (void)pc; }
void cpu_set_fc(int fc) { (void)fc; }
// IRQ 5 held for a single acknowledge (dsp-cpp's HOLD)
int cpu_irq_ack(int level) { (void)level; m68k_set_irq(0); return M68K_INT_ACK_AUTOVECTOR; }

static uint16_t read16(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) return (uint16_t)(S.rom[a & ~1u] << 8 | S.rom[a | 1]);
    if (a >= 0x60000 && a <= 0x63fff) return S.ram[(a & 0x3fff) >> 1];
    if (a >= 0x70000 && a <= 0x70fff) return S.vram1[(a & 0xfff) >> 1];
    if (a >= 0x72000 && a <= 0x73fff) return S.vram2[(a & 0x1fff) >> 1];
    if (a >= 0x74000 && a <= 0x75fff) return S.vram3[(a & 0x1fff) >> 1];
    if (a >= 0x76000 && a <= 0x77fff) return S.sprite_ram[(a & 0x1fff) >> 1];
    if (a >= 0x78000 && a <= 0x79fff) return S.pal_ram[(a & 0x1fff) >> 1];
    switch (a & ~1u) {
    case 0x7a000: return S.in0;
    case 0x7a002: return S.in1;
    case 0x7a004: return S.dsw;
    default: return 0;
    }
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) return;
    if (a >= 0x60000 && a <= 0x63fff) { S.ram[(a & 0x3fff) >> 1] = v; return; }
    if (a >= 0x70000 && a <= 0x70fff) { S.vram1[(a & 0xfff) >> 1] = v; return; }
    if (a >= 0x72000 && a <= 0x73fff) { S.vram2[(a & 0x1fff) >> 1] = v; return; }
    if (a >= 0x74000 && a <= 0x75fff) { S.vram3[(a & 0x1fff) >> 1] = v; return; }
    if (a >= 0x76000 && a <= 0x77fff) { S.sprite_ram[(a & 0x1fff) >> 1] = v; return; }
    if (a >= 0x78000 && a <= 0x79fff) {
        const int i = (a & 0x1fff) >> 1;
        S.pal_ram[i] = v;               // xBGR 444
        S.palette[i] = arc_rgb565((uint32_t)((v & 15) * 0x11) << 16 | (uint32_t)((v >> 4 & 15) * 0x11) << 8 |
                                  (uint32_t)((v >> 8 & 15) * 0x11));
        return;
    }
    switch (a & ~1u) {
    case 0x7a104: S.sy_txt = v; break;
    case 0x7a108: S.sy_txt_off = (uint8_t)v; break;
    case 0x7a10c: S.sx_txt = v; break;
    case 0x7a204: S.sy_fg = v; break;
    case 0x7a208: S.sy_fg_off = (uint8_t)v; break;
    case 0x7a20c: S.sx_fg = v; break;
    case 0x7a304: S.sy_bg = v; break;
    case 0x7a308: S.sy_bg_off = (uint8_t)v; break;
    case 0x7a30c: S.sx_bg = v; break;
    case 0x7a802:
        S.soundlatch = (uint8_t)v;
        zd_set_nmi(&S.sound, ZD_ASSERT);
        break;
    case 0x7a806: m68k_set_irq(0); break;
    case 0x7a808: S.flip = v & 1; break;
    default: break;
    }
}

unsigned int m68k_read_memory_8(unsigned int a) { const uint16_t w = read16(a & ~1u); return (a & 1) ? w & 0xff : w >> 8; }
unsigned int m68k_read_memory_16(unsigned int a) { return read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return (unsigned)read16(a) << 16 | read16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    const uint16_t w = read16(a & ~1u);
    write16(a & ~1u, (a & 1) ? (uint16_t)((w & 0xff00) | (v & 0xff)) : (uint16_t)((w & 0xff) | (v & 0xff) << 8));
}
void m68k_write_memory_16(unsigned int a, unsigned int v) { write16(a, (uint16_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    write16(a, (uint16_t)(v >> 16));
    write16(a + 2, (uint16_t)v);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0xf000) return S.sound_rom[a];
    if (a < 0xf800) return S.sound_ram[a & 0x7ff];
    if (a == 0xf800) return okid_read(&S.oki);
    if (a == 0xfc20) {
        zd_set_nmi(&S.sound, ZD_CLEAR);
        return S.soundlatch;
    }
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xf000 && a < 0xf800) S.sound_ram[a & 0x7ff] = v;
    else if (a == 0xf800) okid_write(&S.oki, v);
    else if (a == 0xf810) ym2203d_control(&S.ym0, v);
    else if (a == 0xf811) ym2203d_write(&S.ym0, v);
    else if (a == 0xf820) ym2203d_control(&S.ym1, v);
    else if (a == 0xf821) ym2203d_write(&S.ym1, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

static void sound_cycles(void *u, int c)
{
    (void)u;
    S.oki_acc += (int64_t)c * okid_sample_frequency(&S.oki);
    while (S.oki_acc >= SOUND_CLOCK) {
        S.oki_acc -= SOUND_CLOCK;
        S.last_oki = okid_update(&S.oki);
    }
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2203d_update(&S.ym0) + ym2203d_update(&S.ym1) + S.last_oki;
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

// A layer's pixel (0: transparent): bg / fg 64x32 tiles of 16x16, the text
// 32x32 of 8x8
static inline int tile16_px(const uint16_t *vram, const uint8_t *gfx, int base, int x, int y)
{
    const int f = (y >> 4) * 64 + (x >> 4);
    const unsigned pen = arc_gfx_px(&k16, gfx, 0x80000, vram[f + 0x800] & 0x0fff, x & 15, y & 15);
    return pen ? base + ((vram[f] >> 4) & 0x0f) * 16 + (int)pen : -1;
}

static inline int text_px(int x, int y)
{
    const int f = (y >> 3) * 32 + (x >> 3);
    const unsigned pen = arc_gfx_px(&k8, S.chars, 0x10000, S.vram1[f + 0x400] & 0x07ff, x & 7, y & 7);
    return pen ? 0x100 + ((S.vram1[f] >> 4) & 0x0f) * 16 + (int)pen : -1;
}

// the picture is rows 16-239 of what is scrolled
static void draw_layer(int which)
{
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&S.fb, y);
        for (int x = 0; x < W; x++) {
            int c;
            if (which == 0)
                c = tile16_px(S.vram3, S.bg, 0x300, (S.sx_bg + x) & 0x3ff,
                              ((uint16_t)(S.sy_bg - S.sy_bg_off + 16) + 16 + y) & 0x1ff);
            else if (which == 1)
                c = tile16_px(S.vram2, S.fg, 0x200, (S.sx_fg + x) & 0x3ff,
                              ((uint16_t)(S.sy_fg - S.sy_fg_off + 16) + 16 + y) & 0x1ff);
            else c = text_px((S.sx_txt + x) & 0xff, ((uint16_t)(S.sy_txt - S.sy_txt_off + 16) + 16 + y) & 0xff);
            if (c >= 0) out[x] = S.palette[c];
        }
    }
}

static inline uint16_t blend565(uint16_t a, uint16_t b)
{
    return (uint16_t)(((((a & 0xf800) + (b & 0xf800)) >> 1) & 0xf800) | ((((a & 0x7e0) + (b & 0x7e0)) >> 1) & 0x7e0) |
                      ((((a & 0x1f) + (b & 0x1f)) >> 1) & 0x1f));
}

static void draw_sprites(int priority)
{
    static const uint8_t kLayout[8][8] = {
        {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23}, {8, 9, 12, 13, 24, 25, 28, 29},
        {10, 11, 14, 15, 26, 27, 30, 31}, {32, 33, 36, 37, 48, 49, 52, 53}, {34, 35, 38, 39, 50, 51, 54, 55},
        {40, 41, 44, 45, 56, 57, 60, 61}, {42, 43, 46, 47, 58, 59, 62, 63}};
    for (int i = 0; i < 0x100; i++) {
        const uint16_t *s = &S.sprite_ram[i * 8];
        const uint16_t attr = s[0];
        if (((attr >> 6) & 3) != priority || !(attr & 4)) continue;
        const bool fx = attr & 1, fy = attr & 2, blend = attr & 0x20;
        const uint16_t color = s[2];
        const int size = 1 << (color & 3);
        int code = s[1];
        if (size >= 2) code &= 0x7ffc;
        if (size >= 4) code &= 0x7ff3;
        if (size >= 8) code &= 0x7fcf;
        const int base = (color & 0xf0) + (blend ? 0x400 : 0);
        const int pos_x = s[4] & 0x1ff, pos_y = s[3] & 0x1ff;
        for (int row = 0; row < size; row++) {
            const int sy = fy ? 8 * (size - 1 - row) : 8 * row;
            for (int col = 0; col < size; col++) {
                const int sx = fx ? 8 * (size - 1 - col) : 8 * col;
                const int c = code + kLayout[row][col];
                for (int py = 0; py < 8; py++) {
                    // (on the 512-wide work area at +64: the picture from 64, 80)
                    const int y = 64 + ((pos_y + sy + py) & 0x1ff) - 80;
                    if (y < 0 || y >= H) continue;
                    uint16_t *out = arc_fb_row(&S.fb, y);
                    for (int px = 0; px < 8; px++) {
                        const int x = 64 + ((pos_x + sx + px) & 0x1ff) - 64;
                        if (x >= W) continue;
                        const unsigned pen = arc_gfx_px(&kSpr8, S.sprites, 0x100000, c, fx ? 7 - px : px, fy ? 7 - py : py);
                        if (!pen) continue;
                        const uint16_t p = S.palette[base + pen];
                        out[x] = blend ? blend565(out[x], p) : p;
                    }
                }
            }
        }
    }
}

static void update_video(void)
{
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&S.fb, y);
        for (int x = 0; x < W; x++) out[x] = S.palette[0x200];
    }
    draw_sprites(3);
    draw_layer(0);
    draw_sprites(2);
    draw_layer(1);
    draw_sprites(1);
    draw_layer(2);
    draw_sprites(0);
    if (S.flip)                         // turned round
        for (int y = 0; y < H / 2; y++) {
            uint16_t *a = arc_fb_row(&S.fb, y), *b = arc_fb_row(&S.fb, H - 1 - y);
            for (int x = 0; x < W; x++) {
                const uint16_t t = a[x];
                a[x] = b[W - 1 - x];
                b[W - 1 - x] = t;
            }
        }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&S.fb, y); }

// a ROM pair as the bytes of 16-bit words (`high` the even ones)
static const uint8_t *word_blob(const char *name, const arc_rom *high, const arc_rom *low, int n, uint32_t size)
{
    arc_blob b;
    uint32_t key = arc_rom_key(high, n, arc_rom_key(low, n, size));
    if (arc_blob_begin(&b, name, size, key)) {
        uint8_t *h = (uint8_t *)malloc(0x20000), *l = (uint8_t *)malloc(0x20000), *w = (uint8_t *)malloc(0x1000);
        if (!h || !l || !w) return NULL;
        for (int k = 0; k < n && !b.err; k++) {
            arc_rom one = high[k], two = low[k];
            one.offset = two.offset = 0;
            if (!arc_load(&one, 1, h, one.size) || !arc_load(&two, 1, l, two.size)) { b.err = true; break; }
            for (uint32_t a = 0; a < one.size; a += 0x800) {
                for (int i = 0; i < 0x800; i++) {
                    w[i * 2] = h[a + i];
                    w[i * 2 + 1] = l[a + i];
                }
                arc_blob_write(&b, high[k].offset * 2 + a * 2, w, 0x1000);
            }
        }
        free(h);
        free(l);
        free(w);
    }
    return arc_blob_end(&b);
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&S.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    if (!(S.rom = word_blob("program", kHigh, kLow, 1, 0x40000)) || !ARC_LOAD(kSound, S.sound_rom, 0x10000) ||
        !(S.oki_rom = ARC_ROM_BLOB("oki", kOki, 0x20000)) || !(S.chars = ARC_ROM_BLOB("chars", kChars, 0x10000)) ||
        !(S.bg = ARC_ROM_BLOB("bg", kBg, 0x80000)) || !(S.fg = ARC_ROM_BLOB("fg", kFg, 0x80000)) ||
        !(S.sprites = word_blob("sprites", kSprHigh, kSprLow, 4, 0x100000)))
        return false;
    S.sound.read = sound_read;
    S.sound.write = sound_write;
    S.sound.in = in_ff;
    S.sound.out = out_nop;
    S.sound.cycles = sound_cycles;
    zd_init(&S.sound);
    ym2203d_init(&S.ym0, SOUND_CLOCK, 1.0f, 1.0f);
    ym2203d_init(&S.ym1, SOUND_CLOCK, 1.0f, 1.0f);
    S.ym0.irq = ym_irq;
    okid_init(&S.oki, OKI_CLOCK, true, S.oki_rom, 0x20000);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    memset(S.vram1, 0, sizeof S.vram1);
    memset(S.vram2, 0, sizeof S.vram2);
    memset(S.vram3, 0, sizeof S.vram3);
    memset(S.sprite_ram, 0, sizeof S.sprite_ram);
    memset(S.pal_ram, 0, sizeof S.pal_ram);
    memset(S.palette, 0, sizeof S.palette);
    memset(S.sound_ram, 0, sizeof S.sound_ram);
    S.sx_txt = S.sy_txt = S.sx_bg = S.sy_bg = S.sx_fg = S.sy_fg = 0;
    S.sy_txt_off = S.sy_bg_off = S.sy_fg_off = 0;
    S.flip = false;
    S.soundlatch = 0;
    S.in0 = 0xff;
    S.in1 = 0xffff;
    S.dsw = 0xffff;
    S.main_debt = S.sound_debt = 0;
    S.audio_acc = S.oki_acc = 0;
    S.last_oki = 0;
    m68k_pulse_reset();
    m68k_set_irq(0);
    zd_reset(&S.sound);
    ym2203d_reset(&S.ym0);
    ym2203d_reset(&S.ym1);
    okid_reset(&S.oki);
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = MAIN_CLOCK / FPS / SCANLINES, sc = SOUND_CLOCK / FPS / SCANLINES;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            update_video();
            m68k_set_irq(5);
        }
        S.main_debt += mc;
        if ((int)S.main_debt > 0) S.main_debt -= m68k_execute((int)S.main_debt);
        S.sound_debt += sc;
        S.sound_debt -= zd_run(&S.sound, (int)S.sound_debt);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&S.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_LEFT) v &= ~0x01;
    if (p & AP_RIGHT) v &= ~0x02;
    if (p & AP_DOWN) v &= ~0x04;
    if (p & AP_UP) v &= ~0x08;
    if (p & AP_B1) v &= ~0x10;
    if (p & AP_B2) v &= ~0x20;
    if (p & AP_B3) v &= ~0x40;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t s = 0xff;
    if (in->p[0] & AP_START) s &= ~0x01;
    if (in->p[1] & AP_START) s &= ~0x02;
    if (in->coin1) s &= ~0x40;
    if (in->coin2) s &= ~0x80;
    S.in0 = s;
    S.in1 = (uint16_t)(pad(in->p[0]) | pad(in->p[1]) << 8);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw = v;
}
