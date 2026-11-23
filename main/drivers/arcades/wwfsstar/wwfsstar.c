// wwfsstar.c - WWF Superstars (Technos Japan, 1989) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/wwfsstar (from
// dsp-emulator's wwfsuperstars_hw.pas). A 68000 at 10 MHz (Musashi here), a
// Z80 with a YM2151 and an OKI M6295, a scrolling 512x512 background of
// 16x16 tiles, 16x16 / 16x32 sprites and 8x8 text. The graphics are read
// straight from their ROMs (3 MB of them, 5 MB decoded). The OKI runs at
// its own rate (dsp-cpp clocked it at the output rate).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68k/m68k.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"

#define MAIN_CLOCK 10000000
#define SOUND_CLOCK 3579545
#define OKI_CLOCK 1056000
#define SCANLINES 272
#define VBLANK_LINE 240
#define FPS 57.444853
#define RATE 44100
#define W 256
#define H 240

static const char *const kSets[] = { "wwfsstar", NULL };
static const arc_rom kEven[] = {{"24ac-0_j-1.34", 0x20000, 0, 0xec8fd2c9}};
static const arc_rom kOdd[] = {{"24ad-0_j-1.35", 0x20000, 0, 0x54e614e4}};
static const arc_rom kSound[] = {{"24ab-0.12", 0x8000, 0, 0x1e44f8aa}};
static const arc_rom kOki[] = {{"24a9-0.46", 0x20000, 0, 0x703ff08f}, {"24j8-0.45", 0x20000, 0x20000, 0x61138487}};
static const arc_rom kChars[] = {{"24aa-0.58", 0x20000, 0, 0xcb12ba40}};
static const arc_rom kBg[] = {{"24j7-0.113", 0x40000, 0, 0xe0a1909e}, {"24j6-0.112", 0x40000, 0x40000, 0x77932ef8}};
static const arc_rom kSprites[] = {
    {"c951.114", 0x80000, 0x000000, 0xfa76d1f0}, {"24j4-0.115", 0x40000, 0x080000, 0xc4a589a3},
    {"24j5-0.116", 0x40000, 0x0c0000, 0xd6bca436}, {"c950.117", 0x80000, 0x100000, 0xcca5703d},
    {"24j2-0.118", 0x40000, 0x180000, 0xdc1b7600}, {"24j3-0.119", 0x40000, 0x1c0000, 0x3ba12d43}};

const arc_info arc_game = { "wwfsstar", "WWF Superstars", "Technos Japan", 1989, W, H, FPS, RATE, kSets,
                            { "PUNCH", "KICK", "JUMP", NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {1, 0, 65, 64, 129, 128, 193, 192};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {3, 2, 1, 0, 131, 130, 129, 128, 259, 258, 257, 256, 387, 386, 385, 384};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const arc_layout kCharL = { 8, 8, 0x1000, 4, 256, false, false, false, {0, 2, 4, 6}, kCharX, kY8 };
static const arc_layout kBgL = { 16, 16, 0x1000, 4, 512, false, false, false, {0x40000 * 8, 0x40000 * 8 + 4, 0, 4}, kX16, kY16 };
static const arc_layout kSprL = { 16, 16, 0x4000, 4, 512, false, false, false, {0x100000 * 8, 0x100000 * 8 + 4, 0, 4}, kX16, kY16 };

// the machine on the heap
static struct state {
    zd_z80 sound;
    ym2151 ym;
    okim6295d oki;
    const uint8_t *rom, *oki_rom, *chars, *bg, *sprites;   // blobs, the graphics raw
    uint16_t ram[0x2000], fg_ram[0x800], bg_ram[0x800], sprite_ram[0x200], pal_ram[0x800];
    uint16_t palette[0x200];
    uint8_t sound_rom[0x8000], sound_ram[0x800];
    arc_fb fb;
    uint16_t scroll_x, scroll_y;
    uint8_t soundlatch, dsw_a, dsw_b, p1, p2, coins;
    bool vblank, flip;
    uint8_t irq_pending;                 // bit n: 68000 IRQ level n asserted
    double main_debt, sound_debt;
    int64_t audio_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

static void update_irq(void) { m68k_set_irq((S.irq_pending & 0x40) ? 6 : (S.irq_pending & 0x20) ? 5 : 0); }

void cpu_pulse_reset(void) {}
void cpu_instr_callback(unsigned int pc) { (void)pc; }
void cpu_set_fc(int fc) { (void)fc; }
int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }

static uint16_t read16(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x40000) return (uint16_t)(S.rom[a & ~1u] << 8 | S.rom[a | 1]);
    if (a >= 0x80000 && a < 0x81000) return S.fg_ram[(a & 0xfff) >> 1];
    if (a >= 0xc0000 && a < 0xc1000) return S.bg_ram[(a & 0xfff) >> 1];
    if (a >= 0x100000 && a < 0x100400) return S.sprite_ram[(a & 0x3ff) >> 1];
    if (a >= 0x140000 && a < 0x141000) return S.pal_ram[(a & 0xfff) >> 1];
    switch (a & 0xfffffe) {
    case 0x180000: return S.dsw_a;
    case 0x180002: return S.dsw_b;
    case 0x180004: return S.p1;
    case 0x180006: return S.p2;
    case 0x180008: return (uint8_t)(S.coins | (S.vblank ? 1 : 0));
    default: break;
    }
    if (a >= 0x1c0000 && a < 0x1c4000) return S.ram[(a & 0x3fff) >> 1];
    return 0xffff;
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a < 0x40000) return;
    if (a >= 0x80000 && a < 0x81000) { S.fg_ram[(a & 0xfff) >> 1] = v; return; }
    if (a >= 0xc0000 && a < 0xc1000) { S.bg_ram[(a & 0xfff) >> 1] = v; return; }
    if (a >= 0x100000 && a < 0x100400) { S.sprite_ram[(a & 0x3ff) >> 1] = v; return; }
    if (a >= 0x140000 && a < 0x141000) {
        const int i = (a & 0xfff) >> 1;
        S.pal_ram[i] = v;
        S.palette[i & 0x1ff] = arc_rgb565((uint32_t)((v & 15) * 0x11) << 16 | (uint32_t)((v >> 4 & 15) * 0x11) << 8 |
                                          (uint32_t)((v >> 8 & 15) * 0x11));
        return;
    }
    switch (a & 0xfffffe) {
    case 0x180000: S.irq_pending &= ~0x40; update_irq(); return;
    case 0x180002: S.irq_pending &= ~0x20; update_irq(); return;
    case 0x180004: S.scroll_x = v & 0x1ff; return;
    case 0x180006: S.scroll_y = v & 0x1ff; return;
    case 0x180008:
        S.soundlatch = (uint8_t)v;
        zd_set_nmi(&S.sound, ZD_PULSE);
        return;
    case 0x18000a: S.flip = v & 1; return;
    default: break;
    }
    if (a >= 0x1c0000 && a < 0x1c4000) S.ram[(a & 0x3fff) >> 1] = v;
}

// (the byte accesses as dsp-cpp's word bus: a word read, a word write)
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
    if (a < 0x8000) return S.sound_rom[a];
    if (a < 0x8800) return S.sound_ram[a & 0x7ff];
    if (a == 0x8801) return ym2151_status(&S.ym);
    if (a == 0x9800) return okid_read(&S.oki);
    if (a == 0xa000) return S.soundlatch;
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a < 0x8800) S.sound_ram[a & 0x7ff] = v;
    else if (a == 0x8800) ym2151_select(&S.ym, v);
    else if (a == 0x8801) ym2151_write(&S.ym, v);
    else if (a == 0x9800) okid_write(&S.oki, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

static void sound_cycles(int c)
{
    ym2151_run_timers(&S.ym, c);
    S.oki_acc += (int64_t)c * okid_sample_frequency(&S.oki);
    while (S.oki_acc >= SOUND_CLOCK) {
        S.oki_acc -= SOUND_CLOCK;
        S.last_oki = okid_update(&S.oki);
    }
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&S.ym) + S.last_oki / 2;
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

// an element straight from its ROM onto the picture
static void blit(const arc_layout *l, const uint8_t *rom, size_t len, int code, int base, int dx, int dy, bool fx,
                 bool fy, bool transparent)
{
    for (int y = 0; y < l->height; y++) {
        const int py = dy + y;
        if (py < 0 || py >= H) continue;
        uint16_t *out = arc_fb_row(&S.fb, py);
        for (int x = 0; x < l->width; x++) {
            const int px = dx + x;
            if (px < 0 || px >= W) continue;
            const unsigned pen = arc_gfx_px(l, rom, len, code, fx ? l->width - 1 - x : x, fy ? l->height - 1 - y : y);
            if (transparent && !pen) continue;
            out[px] = S.palette[(base + pen) & 0x1ff];
        }
    }
}

static void update_video(void)
{
    // the background, a pixel at a time: 32x32 tiles of 16x16 in Technos'
    // order, scrolled (the top 8 lines cut)
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&S.fb, y);
        const int wy = (y + 8 + S.scroll_y) & 0x1ff;
        for (int x = 0; x < W; x++) {
            const int wx = (x + S.scroll_x) & 0x1ff, tx = wx >> 4, ty = wy >> 4;
            const int pos = (tx & 0x0f) + ((ty & 0x0f) << 4) + ((tx & 0x10) << 4) + ((ty & 0x10) << 5);
            const uint16_t attr = S.bg_ram[pos * 2], lo = S.bg_ram[pos * 2 + 1];
            const int code = (lo & 0xff) | ((attr & 0x0f) << 8), color = (attr >> 4) & 7;
            const int px = (attr & 0x08) ? 15 - (wx & 15) : wx & 15;
            out[x] = S.palette[(256 + (color << 4) + arc_gfx_px(&kBgL, S.bg, 0x80000, code, px, wy & 15)) & 0x1ff];
        }
    }
    for (int f = 0; f <= 0x65; f++) {
        const uint16_t *s = &S.sprite_ram[f * 5];
        const uint16_t attr = s[1];
        if (!(attr & 1)) continue;
        int y = (s[0] & 0xff) | ((attr & 4) << 6);
        y = ((256 - y) & 0x1ff) - 32;
        int x = (s[4] & 0xff) | ((attr & 8) << 5);
        x = ((256 - x) & 0x1ff) - 16;
        const uint16_t attr2 = s[2];
        const bool fx = (attr2 & 0x80) != 0, fy = (attr2 & 0x40) != 0;
        int code = (s[3] & 0xff) | ((attr2 & 0x3f) << 8);
        const int color = 128 + (attr & 0xf0);
        if (attr & 2) {                 // 16x32
            code &= 0x3ffe;
            const int a = fy ? 16 : 0;
            blit(&kSprL, S.sprites, 0x200000, code, color, x, y + a - 8, fx, fy, true);
            blit(&kSprL, S.sprites, 0x200000, code + 1, color, x, y + (a ^ 16) - 8, fx, fy, true);
        } else blit(&kSprL, S.sprites, 0x200000, code, color, x, y + 16 - 8, fx, fy, true);
    }
    for (int f = 0; f < 0x400; f++) {   // the text
        const uint16_t attr = S.fg_ram[f * 2], lo = S.fg_ram[f * 2 + 1];
        blit(&kCharL, S.chars, 0x20000, (lo & 0xff) | ((attr & 0x0f) << 8), ((attr >> 4) & 0x0f) << 4, (f & 0x1f) * 8,
             (f >> 5) * 8 - 8, false, false, true);
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&S.fb, y); }

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&S.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    // the program: the even ROM the high bytes
    uint8_t *half = (uint8_t *)malloc(0x20000);
    if (!half) return false;
    arc_blob b;
    if (arc_blob_begin(&b, "program", 0x40000, ARC_ROM_KEY(kEven, ARC_ROM_KEY(kOdd, 0)))) {
        uint8_t *even = (uint8_t *)malloc(0x1000);
        if (!even || !ARC_LOAD(kEven, half, 0x20000)) return false;
        uint8_t *odd = (uint8_t *)malloc(0x20000);
        if (!odd || !ARC_LOAD(kOdd, odd, 0x20000)) return false;
        for (uint32_t a = 0; a < 0x20000; a += 0x800) {
            for (int i = 0; i < 0x800; i++) {
                even[i * 2] = half[a + i];
                even[i * 2 + 1] = odd[a + i];
            }
            arc_blob_write(&b, a * 2, even, 0x1000);
        }
        free(odd);
        free(even);
    }
    free(half);
    if (!(S.rom = arc_blob_end(&b)) || !ARC_LOAD(kSound, S.sound_rom, 0x8000) ||
        !(S.oki_rom = ARC_ROM_BLOB("oki", kOki, 0x40000)) || !(S.chars = ARC_ROM_BLOB("chars", kChars, 0x20000)) ||
        !(S.bg = ARC_ROM_BLOB("bg", kBg, 0x80000)) || !(S.sprites = ARC_ROM_BLOB("sprites", kSprites, 0x200000)))
        return false;
    S.sound.read = sound_read;
    S.sound.write = sound_write;
    S.sound.in = in_ff;
    S.sound.out = out_nop;
    zd_init(&S.sound);
    ym2151_init(&S.ym, SOUND_CLOCK, RATE);
    S.ym.irq_handler = ym_irq;
    okid_init(&S.oki, OKI_CLOCK, true, S.oki_rom, 0x40000);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    S.dsw_a = S.dsw_b = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    memset(S.fg_ram, 0, sizeof S.fg_ram);
    memset(S.bg_ram, 0, sizeof S.bg_ram);
    memset(S.sprite_ram, 0, sizeof S.sprite_ram);
    memset(S.pal_ram, 0, sizeof S.pal_ram);
    memset(S.palette, 0, sizeof S.palette);
    memset(S.sound_ram, 0, sizeof S.sound_ram);
    S.scroll_x = S.scroll_y = 0;
    S.soundlatch = 0;
    S.flip = S.vblank = false;
    S.p1 = S.p2 = 0xff;
    S.coins = 0xfe;
    S.irq_pending = 0;
    S.main_debt = S.sound_debt = 0;
    S.audio_acc = S.oki_acc = 0;
    S.last_oki = 0;
    m68k_pulse_reset();
    m68k_set_irq(0);
    zd_reset(&S.sound);
    ym2151_reset(&S.ym);
    okid_reset(&S.oki);
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = MAIN_CLOCK / FPS / SCANLINES, sc = SOUND_CLOCK / FPS / SCANLINES;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) S.vblank = false;
        if (line == VBLANK_LINE) {
            S.vblank = true;
            S.irq_pending |= 0x60;
            update_irq();
            update_video();
        } else if (line > 0 && line % 16 == 0 && line <= 256) {   // IRQ 5 every 16 lines
            S.irq_pending |= 0x20;
            update_irq();
        }
        S.main_debt += mc;
        if ((int)S.main_debt > 0) S.main_debt -= m68k_execute((int)S.main_debt);
        S.sound_debt += sc;
        const int done = zd_run(&S.sound, (int)S.sound_debt);
        S.sound_debt -= done;
        sound_cycles(done);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&S.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_RIGHT) v &= ~0x01;
    if (p & AP_LEFT) v &= ~0x02;
    if (p & AP_UP) v &= ~0x04;
    if (p & AP_DOWN) v &= ~0x08;
    if (p & AP_B1) v &= ~0x10;
    if (p & AP_B2) v &= ~0x20;
    if (p & AP_B3) v &= ~0x40;
    if (p & AP_START) v &= ~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    S.p1 = pad(in->p[0]);
    S.p2 = pad(in->p[1]);
    uint8_t c = 0xfe;
    if (in->coin1) c &= ~0x02;
    if (in->coin2) c &= ~0x04;
    S.coins = c;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    else if (bank == 1) S.dsw_b = v;
}
