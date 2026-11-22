// bublbobl.c - Bubble Bobble (Taito, 1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/bublbobl (from dsp-emulator's
// bublbobl_hw.pas, with MAME's sprite list). A main Z80 and a sub Z80 at
// 6 MHz sharing RAM, a sound Z80 with a YM2203 and a YM3526, and the 6801U4
// MCU (an HD63701 here) that reads the inputs and raises the main CPU's
// interrupt. Everything on screen is built from 8x8 tiles in object lists.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/hd63701dsp/hd63701dsp.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/ym3812dsp.h"

#define MAIN_CLOCK 6000000
#define SOUND_CLOCK 3000000
#define MCU_CLOCK 4000000
#define YM_CLOCK 3000000
#define FPS 59.185606
#define SCANLINES 264
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "bublbobl", NULL };
static const arc_rom kMain[] = {{"a78-06-1.51", 0x8000, 0x0000, 0x567934b6}, {"a78-05-1.52", 0x10000, 0x8000, 0x9f8ee242}};
static const arc_rom kSub[] = {{"a78-08.37", 0x8000, 0, 0xae11a07b}};
static const arc_rom kSound[] = {{"a78-07.46", 0x8000, 0, 0x4f9a26e8}};
static const arc_rom kMcu[] = {{"a78-01.17", 0x1000, 0, 0xb1bfb53d}};
static const arc_rom kProm[] = {{"a71-25.41", 0x100, 0, 0x2d0f8545}};
static const arc_rom kGfx[] = {
    {"a78-09.12", 0x8000, 0x00000, 0x20358c22}, {"a78-10.13", 0x8000, 0x08000, 0x930168a9},
    {"a78-11.14", 0x8000, 0x10000, 0x9773e512}, {"a78-12.15", 0x8000, 0x18000, 0xd045549b},
    {"a78-13.16", 0x8000, 0x20000, 0xd0af35c5}, {"a78-14.17", 0x8000, 0x28000, 0x7b5369a8},
    {"a78-15.30", 0x8000, 0x40000, 0x6b61a413}, {"a78-16.31", 0x8000, 0x48000, 0xb5492d97},
    {"a78-17.32", 0x8000, 0x50000, 0xd69762d5}, {"a78-18.33", 0x8000, 0x58000, 0x9f243b68},
    {"a78-19.34", 0x8000, 0x60000, 0x66e9438c}, {"a78-20.35", 0x8000, 0x68000, 0x9ef863ad}};

const arc_info arc_game = { "bublbobl", "Bubble Bobble", "Taito", 1986, W, H, FPS, RATE, kSets,
                            { "BUBBLE", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {3, 2, 1, 0, 11, 10, 9, 8};
static const int kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sub, sound;
    hd63701 mcu;
    ym2203d ym2203;
    ym3812d ym3526;
    uint8_t mem[0x10000], sub_rom[0x8000], sound_mem[0x10000], bank_rom[8][0x4000];
    uint8_t pal_ram[0x200], prom[0x100];
    uint16_t palette[0x100];
    uint8_t *gfx;
    uint16_t bitmap[256 * 256];
    uint16_t *fb;
    uint8_t bank, sound_stat, sound_latch;
    bool sound_nmi, video_enable, flip, sub_reset, sound_reset, mcu_held;
    uint8_t in0, in1, in2, dsw_a, dsw_b;
    uint8_t p1_out, p2_out, p3_in, p3_out, p4_out;
    bool ym2203_irq, ym3526_irq;
    double frame_main, frame_sub, frame_snd, frame_mcu;
    int16_t audio[1024];
} *B_;
#define B (*B_)

static void set_sub_reset(bool held)
{
    if (held && !B.sub_reset) zd_reset(&B.sub);
    B.sub_reset = held;
}

static void set_sound_reset(bool held)
{
    if (held && !B.sound_reset) {
        zd_reset(&B.sound);
        zd_set_irq(&B.sound, ZD_CLEAR, 0xff);
        zd_set_nmi(&B.sound, ZD_CLEAR);
        B.sound_nmi = false;
    }
    B.sound_reset = held;
}

static void set_mcu_reset(bool held)
{
    B.mcu_held = held;
    hd63701_set_reset(&B.mcu, held ? HD_ASSERT : HD_CLEAR);
}

static void update_sound_irq(void) { zd_set_irq(&B.sound, (B.ym2203_irq || B.ym3526_irq) ? ZD_ASSERT : ZD_CLEAR, 0xff); }
static void ym2203_irq(void *u, bool on) { (void)u; B.ym2203_irq = on; update_sound_irq(); }
static void ym3526_irq(void *u, bool on) { (void)u; B.ym3526_irq = on; update_sound_irq(); }

static void set_color(int i)
{
    const uint8_t rg = B.pal_ram[i * 2], b = B.pal_ram[i * 2 + 1];
    B.palette[i] = arc_rgb565((uint32_t)((rg >> 4) * 0x11) << 16 | (uint32_t)((rg & 15) * 0x11) << 8 | (uint32_t)((b >> 4) * 0x11));
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff || (a >= 0xc000 && a <= 0xf7ff) || a >= 0xfc00) return B.mem[a];
    if (a <= 0xbfff) return B.bank_rom[B.bank & 7][a & 0x3fff];
    if (a <= 0xf9ff) return B.pal_ram[a & 0x1ff];
    if (a == 0xfa00) return B.sound_stat;
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if ((a >= 0xc000 && a <= 0xf7ff) || a >= 0xfc00) B.mem[a] = v;
    else if (a <= 0xf9ff) {
        const uint16_t i = a & 0x1ff;
        if (B.pal_ram[i] != v) {
            B.pal_ram[i] = v;
            set_color(i >> 1);
        }
    } else if (a >= 0xfa00 && a <= 0xfa7f) {
        if ((a & 3) == 0) {
            if (!B.sound_nmi) {
                B.sound_latch = v;
                B.sound_nmi = true;
                if (!B.sound_reset) zd_set_nmi(&B.sound, ZD_ASSERT);
            }
        } else if ((a & 3) == 3) set_sound_reset(v != 0);
    } else if (a >= 0xfb00 && a <= 0xfb3f) {
        if (!B.sub_reset) zd_set_nmi(&B.sub, ZD_PULSE);
    } else if (a >= 0xfb40 && a <= 0xfb7f) {
        B.bank = (v ^ 4) & 7;
        set_sub_reset((v & 0x10) == 0);
        set_mcu_reset((v & 0x20) == 0);
        B.video_enable = (v & 0x40) != 0;
        B.flip = (v & 0x80) != 0;
    }
}

static void main_irq_ack(void *u) { (void)u; zd_set_irq(&B.main, ZD_CLEAR, B.mem[0xfc00]); }

static uint8_t sub_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff) return B.sub_rom[a];
    if (a >= 0xe000 && a <= 0xf7ff) return B.mem[a];
    return 0xff;
}

static void sub_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xe000 && a <= 0xf7ff) B.mem[a] = v;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x8fff) return B.sound_mem[a];
    switch (a) {
    case 0x9000: return ym2203d_status(&B.ym2203);
    case 0x9001: return ym2203d_read(&B.ym2203);
    case 0xa000: return ym3812d_status(&B.ym3526);
    case 0xa001: return 0xff;
    case 0xb000: return B.sound_latch;
    default: return 0xff;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x8fff) { B.sound_mem[a] = v; return; }
    switch (a) {
    case 0x9000: ym2203d_control(&B.ym2203, v); break;
    case 0x9001: ym2203d_write(&B.ym2203, v); break;
    case 0xa000: ym3812d_control(&B.ym3526, v); break;
    case 0xa001: ym3812d_write(&B.ym3526, v); break;
    case 0xb000: B.sound_stat = v; break;
    case 0xb002:
        B.sound_nmi = false;
        zd_set_nmi(&B.sound, ZD_CLEAR);
        break;
    default: break;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// the MCU: port 1 bit 6 falling raises the main CPU's IRQ (vector at fc00);
// port 2 bit 4 rising strobes an access (port 4 / port 2 the address, port 3
// the data) to the inputs and DIP switches or to the shared RAM at fc00
static uint8_t mcu_p1_read(void *u) { (void)u; return B.in0; }
static uint8_t mcu_p3_read(void *u) { (void)u; return B.p3_in; }
static void mcu_p1_write(void *u, uint8_t v)
{
    (void)u;
    if ((B.p1_out & 0x40) && !(v & 0x40)) zd_set_irq(&B.main, ZD_ASSERT, B.mem[0xfc00]);
    B.p1_out = v;
}
static void mcu_p2_write(void *u, uint8_t v)
{
    (void)u;
    if (!(B.p2_out & 0x10) && (v & 0x10)) {
        const uint16_t a = (uint16_t)(B.p4_out | ((v & 0x0f) << 8));
        if (B.p1_out & 0x80) {
            if (!(a & 0x800)) {
                switch (a & 3) {
                case 0: B.p3_in = B.dsw_a; break;
                case 1: B.p3_in = B.dsw_b; break;
                case 2: B.p3_in = B.in1; break;
                case 3: B.p3_in = B.in2; break;
                }
            } else if ((a & 0xc00) == 0xc00) B.p3_in = B.mem[0xfc00 + (a & 0x3ff)];
        } else if ((a & 0xc00) == 0xc00) B.mem[0xfc00 + (a & 0x3ff)] = B.p3_out;
    }
    B.p2_out = v;
}
static void mcu_p3_write(void *u, uint8_t v) { (void)u; B.p3_out = v; }
static void mcu_p4_write(void *u, uint8_t v) { (void)u; B.p4_out = v; }

static void draw_tile(int code, int color, bool fx, bool fy, int x, int y)
{
    const uint8_t *src = B.gfx + (size_t)code * 64;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            const uint8_t pen = src[(fy ? 7 - r : r) * 8 + (fx ? 7 - c : c)];
            if (pen == 15) continue;
            const int dx = x + c, dy = (y + r) & 0xff;
            if (dx < 0 || dx >= 256) continue;
            B.bitmap[dy * 256 + dx] = B.palette[(color + pen) & 0xff];
        }
}

static void update_video(void)
{
    const uint16_t backdrop = B.video_enable ? B.palette[0xff] : 0;
    for (int i = 0; i < 256 * 256; i++) B.bitmap[i] = backdrop;
    if (B.video_enable) {
        int sx = 0;
        for (int offs = 0; offs < 0xc0; offs++) {
            const uint8_t *o = &B.mem[0xdd00 + offs * 4];
            if (!o[0] && !o[1] && !o[2] && !o[3]) continue;
            const uint8_t gfx_num = o[1], gfx_attr = o[3];
            const int prom_line = 0x80 + ((gfx_num & 0xe0) >> 1);
            int gfx_offs = (gfx_num & 0x1f) << 7;
            if ((gfx_num & 0xa0) == 0xa0) gfx_offs |= 0x1000;
            const int sy = -(int)o[0];
            for (int yc = 0; yc < 32; yc++) {
                const uint8_t atrib2 = B.prom[prom_line + (yc >> 1)];
                if (atrib2 & 8) continue;
                if (!(atrib2 & 4)) {
                    sx = o[2];
                    if (gfx_attr & 0x40) sx -= 256;
                }
                for (int xc = 0; xc < 2; xc++) {
                    const int goffs = gfx_offs + (xc << 6) + ((yc & 7) << 1) + ((atrib2 & 3) << 4);
                    const uint8_t atrib = B.mem[0xc001 + goffs];
                    const int code = B.mem[0xc000 + goffs] + ((atrib & 3) << 8) + ((gfx_attr & 0x0f) << 10);
                    const int color = (atrib & 0x3c) << 2;
                    bool fx = (atrib & 0x40) != 0, fy = (atrib & 0x80) != 0;
                    int x = sx + xc * 8, y = (sy + yc * 8) & 0xff;
                    if (B.flip) {
                        x = 248 - x;
                        y = 248 - y;
                        fx = !fx;
                        fy = !fy;
                    }
                    draw_tile(code, color, fx, fy, x, y);
                }
            }
            sx += 16;
        }
    }
    memcpy(B.fb, B.bitmap + 16 * 256, W * H * 2);
}

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    B.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *rom = (uint8_t *)calloc(1, 0x80000);
    if (!B.fb || !rom) return false;
    if (!ARC_LOAD(kMain, rom, 0x18000)) return false;
    memcpy(B.mem, rom, 0x8000);
    memset(B.bank_rom, 0xff, sizeof B.bank_rom);
    for (int b = 0; b < 4; b++) memcpy(B.bank_rom[b], rom + 0x8000 + b * 0x4000, 0x4000);
    if (!ARC_LOAD(kSub, B.sub_rom, 0x8000) || !ARC_LOAD(kSound, B.sound_mem, 0x8000)) return false;
    hd63701_init(&B.mcu, MCU_CLOCK, HD_HD63701Y);
    memset(B.mcu.rom, 0xff, sizeof B.mcu.rom);
    if (!ARC_LOAD(kMcu, B.mcu.rom + 0x3000, 0x1000) || !ARC_LOAD(kProm, B.prom, 0x100)) return false;
    memset(rom, 0, 0x80000);
    if (!ARC_LOAD(kGfx, rom, 0x80000)) return false;
    for (int i = 0; i < 0x80000; i++) rom[i] = (uint8_t)~rom[i];   // ROM 0 is pen 15, transparent
    const int half = 0x4000 * 16 * 8;
    arc_layout l = { 8, 8, 0x4000, 4, 128, false, false, false, {0, 4, half, half + 4}, kX8, kY8 };
    B.gfx = arc_gfx_decode(&l, rom, 0x80000);
    free(rom);
    if (!B.gfx) return false;
    B.main.read = main_read;
    B.main.write = main_write;
    B.main.irq_ack = main_irq_ack;
    B.sub.read = sub_read;
    B.sub.write = sub_write;
    B.sound.read = sound_read;
    B.sound.write = sound_write;
    B.main.in = B.sub.in = B.sound.in = in_ff;
    B.main.out = B.sub.out = B.sound.out = out_nop;
    zd_init(&B.main);
    zd_init(&B.sub);
    zd_init(&B.sound);
    B.mcu.port_read[0] = mcu_p1_read;
    B.mcu.port_read[2] = mcu_p3_read;
    B.mcu.port_write[0] = mcu_p1_write;
    B.mcu.port_write[1] = mcu_p2_write;
    B.mcu.port_write[2] = mcu_p3_write;
    B.mcu.port_write[3] = mcu_p4_write;
    ym2203d_init(&B.ym2203, YM_CLOCK, 1.0f, 1.0f);
    B.ym2203.irq = ym2203_irq;
    ym3812d_init(&B.ym3526, YM_CLOCK, true, 1.0f);
    B.ym3526.irq = ym3526_irq;
    B.sub_reset = true;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&B.main);
    zd_reset(&B.sub);
    zd_reset(&B.sound);
    hd63701_reset(&B.mcu);
    ym2203d_reset(&B.ym2203);
    ym3812d_reset(&B.ym3526);
    B.bank = B.sound_stat = B.sound_latch = 0;
    B.sound_nmi = B.video_enable = B.flip = false;
    B.in0 = 0xb3;
    B.in1 = B.in2 = 0xff;
    B.dsw_a = 0xfe;
    B.dsw_b = 0xff;
    B.p1_out = B.p2_out = B.p3_in = B.p3_out = B.p4_out = 0;
    B.ym2203_irq = B.ym3526_irq = false;
    B.frame_main = B.frame_sub = B.frame_snd = B.frame_mcu = 0;
    memset(B.pal_ram, 0, sizeof B.pal_ram);
    for (int i = 0; i < 0x100; i++) B.palette[i] = 0;
    memset(B.mem + 0xc000, 0, 0x4000);
    memset(B.sound_mem + 0x8000, 0, 0x8000);
    set_sub_reset(true);
    set_sound_reset(false);
    set_mcu_reset(true);
}

static void run_part(zd_z80 *z, double per_line, double *frac)
{
    const double c = per_line + *frac;
    zd_run(z, (int)c);
    *frac = c - (int)c;
}

int arc_run_frame(const int16_t **audio)
{
    const double main_c = MAIN_CLOCK / FPS / SCANLINES, snd_c = SOUND_CLOCK / FPS / SCANLINES;
    const double mcu_c = MCU_CLOCK / FPS / SCANLINES;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (!B.sub_reset) zd_set_irq(&B.sub, ZD_HOLD, 0xff);
            if (!B.mcu_held) hd63701_set_irq(&B.mcu, HD_HOLD);
            update_video();
        }
        run_part(&B.main, main_c, &B.frame_main);
        if (!B.sub_reset) run_part(&B.sub, main_c, &B.frame_sub);
        else B.frame_sub = 0;
        if (!B.sound_reset) run_part(&B.sound, snd_c, &B.frame_snd);
        else B.frame_snd = 0;
        const double c = mcu_c + B.frame_mcu;
        hd63701_run(&B.mcu, (int)c);
        B.frame_mcu = c - (int)c;
    }
    const int n = (int)(RATE / FPS + 0.5);
    for (int i = 0; i < n; i++)
        B.audio[i] = arc_clamp16((ym2203d_update(&B.ym2203) + ym3812d_update(&B.ym3526)) * 6);
    if (audio) *audio = B.audio;
    return n;
}

const uint16_t *arc_frame(void) { return B.fb; }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_LEFT) v &= ~0x01;
    if (p & AP_RIGHT) v &= ~0x02;
    if (p & AP_DOWN) v &= ~0x04;
    if (p & AP_UP) v &= ~0x08;
    if (p & AP_B2) v &= ~0x10;
    if (p & AP_B1) v &= ~0x20;
    if (p & AP_START) v &= ~0x40;
    return v;
}

void arc_input(const arc_in *in)
{
    B.in1 = pad(in->p[0]);
    B.in2 = pad(in->p[1]);
    uint8_t v = 0xb3;                   // the coins active high
    if (in->coin1) v |= 0x04;
    if (in->coin2) v |= 0x08;
    B.in0 = v;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw_a = v;
    if (bank == 1) B.dsw_b = v;
}
