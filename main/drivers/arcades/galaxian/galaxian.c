// galaxian.c - Galaxian (Namco / Midway, 1979), Moon Cresta (Nichibutsu,
// 1980), Scramble (Konami, 1981) and Frogger (Konami, 1981) for the common
// arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/galaxian (from
// dsp-emulator's galaxian_hw.pas). A Z80 at 3 MHz, a map of 8x8 characters
// scrolled per row, 8 sprites, bullets and a star field. Galaxian and Moon
// Cresta have no sound here, as dsp-cpp (the board's is discrete); Scramble
// and Frogger have Konami's sound board (a Z80 and one or two AY-3-8910),
// behind two 8255. ARC_VARIANT: 0 galaxian, 1 mooncrst, 2 scramble, 3 frogger.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define MOONCRST (ARC_VARIANT == 1)
#define SCRAMBLE (ARC_VARIANT == 2)
#define FROGGER (ARC_VARIANT == 3)
#define KONAMI (SCRAMBLE || FROGGER)

#define CLOCK 3072000
#define SOUND_CLOCK (14318181 / 8)
#define SCANLINES 256
#define FPS 60.60606060
#define RATE 44100
#define W 224
#define H 256

#if MOONCRST
static const char *const kSets[] = { "mooncrst", NULL };
static const arc_rom kCpu[] = {
    {"mc1", 0x800, 0x0000, 0x7d954a7a}, {"mc2", 0x800, 0x0800, 0x44bb7cfa}, {"mc3", 0x800, 0x1000, 0x9c412104},
    {"mc4", 0x800, 0x1800, 0x7e9b1ab5}, {"mc5.7r", 0x800, 0x2000, 0x16c759af}, {"mc6.8d", 0x800, 0x2800, 0x69bcafdb},
    {"mc7.8e", 0x800, 0x3000, 0xb50dbc46}, {"mc8", 0x800, 0x3800, 0x18ca312b}};
static const arc_rom kGfx[] = {
    {"mcs_b", 0x800, 0x0000, 0xfb0f1f81}, {"mcs_d", 0x800, 0x0800, 0x13932a15},
    {"mcs_a", 0x800, 0x1000, 0x631ebb5a}, {"mcs_c", 0x800, 0x1800, 0x24cfd145}};
static const arc_rom kPal[] = {{"mmi6331.6l", 0x20, 0, 0x6a0c7d87}};
const arc_info arc_game = { "mooncrst", "Moon Cresta", "Nichibutsu", 1980, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };
#define ROM_SIZE 0x4000
#define GFX_SIZE 0x2000
#elif SCRAMBLE
static const char *const kSets[] = { "scramble", NULL };
static const arc_rom kCpu[] = {
    {"s1.2d", 0x800, 0x0000, 0xea35ccaa}, {"s2.2e", 0x800, 0x0800, 0xe7bba1b3}, {"s3.2f", 0x800, 0x1000, 0x12d7fc3e},
    {"s4.2h", 0x800, 0x1800, 0xb59360eb}, {"s5.2j", 0x800, 0x2000, 0x4919a91c}, {"s6.2l", 0x800, 0x2800, 0x26a4547b},
    {"s7.2m", 0x800, 0x3000, 0x0bb49470}, {"s8.2p", 0x800, 0x3800, 0x6a5740e5}};
static const arc_rom kGfx[] = {{"c2.5f", 0x800, 0, 0x4708845b}, {"c1.5h", 0x800, 0x800, 0x11fd2887}};
static const arc_rom kPal[] = {{"c01s.6e", 0x20, 0, 0x4e3caeab}};
static const arc_rom kSound[] = {
    {"ot1.5c", 0x800, 0x0000, 0xbcd297f0}, {"ot2.5d", 0x800, 0x0800, 0xde7912da}, {"ot3.5e", 0x800, 0x1000, 0xba2fa933}};
const arc_info arc_game = { "scramble", "Scramble", "Konami", 1981, W, H, FPS, RATE, kSets,
                            { "FIRE", "BOMB", NULL, NULL }, ARC_JOY_8WAY };
#define ROM_SIZE 0x4000
#define GFX_SIZE 0x1000
#elif FROGGER
static const char *const kSets[] = { "frogger", NULL };
static const arc_rom kCpu[] = {
    {"frogger.26", 0x1000, 0x0000, 0x597696d6}, {"frogger.27", 0x1000, 0x1000, 0xb6e6fcc3},
    {"frsm3.7", 0x1000, 0x2000, 0xaca22ae0}};
static const arc_rom kGfx[] = {{"frogger.607", 0x800, 0, 0x05f7d883}, {"frogger.606", 0x800, 0x800, 0xf524ee30}};
static const arc_rom kPal[] = {{"pr-91.6l", 0x20, 0, 0x413703bf}};
static const arc_rom kSound[] = {
    {"frogger.608", 0x800, 0x0000, 0xe8ab0256}, {"frogger.609", 0x800, 0x0800, 0x7380a48f},
    {"frogger.610", 0x800, 0x1000, 0x31d7eb27}};
const arc_info arc_game = { "frogger", "Frogger", "Konami", 1981, W, H, FPS, RATE, kSets,
                            { NULL, NULL, NULL, NULL }, ARC_JOY_4WAY };
#define ROM_SIZE 0x3000
#define GFX_SIZE 0x1000
#else
static const char *const kSets[] = { "galaxian", "galmidw", NULL };
static const arc_rom kCpu[] = {
    {"galmidw.u", 0x800, 0x0000, 0x745e2d61}, {"galmidw.v", 0x800, 0x0800, 0x9c999a40},
    {"galmidw.w", 0x800, 0x1000, 0xb5894925}, {"galmidw.y", 0x800, 0x1800, 0x6b3ca10b},
    {"7l", 0x800, 0x2000, 0x1b933207}};
static const arc_rom kGfx[] = {{"1h.bin", 0x800, 0, 0x39fb43a4}, {"1k.bin", 0x800, 0x800, 0x7e3f56a2}};
static const arc_rom kPal[] = {{"6l.bpr", 0x20, 0, 0xc3ac9467}};

const arc_info arc_game = { "galaxian", "Galaxian", "Namco", 1979, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };
#define ROM_SIZE 0x2800
#define GFX_SIZE 0x1000
#endif
#define CHARS (GFX_SIZE / 16)
#define SPRITES (GFX_SIZE / 64)

// dsp-cpp's minimal 8255 (Scramble, Frogger): mode 0, no bit set / reset
typedef struct { uint8_t ctrl, lat[3]; } ppi_t;

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
#if KONAMI
    zd_z80 sound;
    ay8910 ay0, ay1;
    ppi_t ppi0, ppi1;
    uint8_t sound_rom[0x1800], sound_ram[0x400], sound_latch, port_b_latch, prot, in2;
    uint16_t prot_state;
    bool sound_mute, background;
    uint64_t sound_cycles;
#endif
#if MOONCRST
    uint8_t gfx_bank[3];
#endif
    uint8_t rom[0x4000], ram[0x800], videoram[0x400], attributes[0x40], sprites[0x20], bullets[0x20], misc[0x100];
    uint8_t *chars, *sprite_gfx;
    uint16_t palette[64];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    bool nmi_enable, stars_enable;
    uint32_t stars_scroll;
    uint8_t in0, in1, dsw_a, dsw_b, dsw_c;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *G_;
#define G (*G_)

// the attributes, sprites and bullets block
static uint8_t obj_r(uint8_t off)
{
    if (off <= 0x3f) return G.attributes[off];
    if (off <= 0x5f) return G.sprites[off & 0x1f];
    if (off <= 0x7f) return G.bullets[off & 0x1f];
    return G.misc[off];
}

static void obj_w(uint8_t off, uint8_t v)
{
    if (off <= 0x3f) G.attributes[off] = v;
    else if (off <= 0x5f) G.sprites[off & 0x1f] = v;
    else if (off <= 0x7f) G.bullets[off & 0x1f] = v;
    else G.misc[off] = v;
}

static void nmi_w(uint8_t v)
{
    G.nmi_enable = (v & 1) != 0;
    if (!G.nmi_enable) zd_set_nmi(&G.cpu, ZD_CLEAR);
}

#if KONAMI
// ---------------------------------------------------------------------------
// The two 8255

static uint8_t ppi_in(int chip, int port)
{
#if SCRAMBLE
    if (chip == 0) {
        if (port == 0) return G.in0;
        if (port == 1) return (uint8_t)(G.in1 | G.dsw_a);
        return (uint8_t)(G.in2 | G.dsw_b | (G.prot & 0x80) | ((G.prot & 0x80) >> 2));
    }
    if (port == 2) return G.prot;
#else
    if (chip == 0) return port == 0 ? G.in0 : port == 1 ? (uint8_t)(G.in1 | G.dsw_a) : (uint8_t)(G.in2 | G.dsw_b);
#endif
    return (chip ? &G.ppi1 : &G.ppi0)->lat[port];
}

static uint8_t ppi_r(int chip, int idx)
{
    idx &= 3;
    return idx == 3 ? (chip ? &G.ppi1 : &G.ppi0)->ctrl : ppi_in(chip, idx);
}

static void ppi_w(int chip, int idx, uint8_t v)
{
    ppi_t *p = chip ? &G.ppi1 : &G.ppi0;
    idx &= 3;
    if (idx == 3) {
        if (v & 0x80) p->ctrl = v;
        return;
    }
    p->lat[idx] = v;
    if (chip == 0) return;
    if (idx == 0) G.sound_latch = v;
    else if (idx == 1) {
#if SCRAMBLE
        // bit 3 falling: the sound IRQ; bit 4: mute
        const uint8_t old = G.port_b_latch;
        G.port_b_latch = v;
        if ((old & 0x08) && !(v & 0x08)) zd_set_irq(&G.sound, ZD_HOLD, 0xff);
        G.sound_mute = (v & 0x10) != 0;
#else
        if (G.port_b_latch == 0 && v != 0) zd_set_irq(&G.sound, ZD_HOLD, 0xff);
        G.port_b_latch = v;
#endif
    }
#if SCRAMBLE
    else {                              // the protection
        G.prot_state = (uint16_t)((G.prot_state << 4) | (v & 0x0f));
        const int num1 = (G.prot_state >> 8) & 0x0f, num2 = (G.prot_state >> 4) & 0x0f;
        switch (G.prot_state & 0x0f) {
        case 0x6: G.prot ^= 0x80; break;
        case 0x9: G.prot = num1 + 1 > 0x0f ? 0xf0 : (uint8_t)((num1 + 1) << 4); break;
        case 0xa: G.prot = 0; break;
        case 0xb: G.prot = num2 - num1 < 0 ? 0 : (uint8_t)((num2 - num1) << 4); break;
        case 0xf: G.prot = num1 - num2 < 0 ? 0 : (uint8_t)((num1 - num2) << 4); break;
        default: break;
        }
    }
#endif
}

static void ppi_reset(ppi_t *p)
{
    p->ctrl = 0x9b;
    p->lat[0] = p->lat[1] = p->lat[2] = 0xff;
}

// ---------------------------------------------------------------------------
// Konami's sound board

static uint8_t timer_r(void)
{
    const uint32_t period = 16u * 16u * 2u * 8u * 5u * 2u;
    uint32_t c = (uint32_t)((G.sound_cycles * 8u) % period);
    uint8_t hi = 0;
    if (c >= period / 2) {
        hi = 1;
        c -= period / 2;
    }
    const uint8_t v = (uint8_t)((hi << 7) | (((c >> 14) & 1) << 6) | (((c >> 13) & 1) << 5) | (((c >> 11) & 1) << 4) | 0x0e);
    if (FROGGER) return (uint8_t)((v & 0xc7) | ((v & 0x08) << 3) | (v & 0x10) | ((v & 0x20) >> 3));
    return v;
}

static uint8_t ay0_r(void)
{
    if (G.ay0.latch == 14) G.ay0.regs[14] = G.sound_latch;
    if (G.ay0.latch == 15) G.ay0.regs[15] = timer_r();
    return ay8910_data_r(&G.ay0);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
#if FROGGER
    a &= 0x7fff;
    if (a <= 0x17ff) return G.sound_rom[a];
    if (a <= 0x1fff) return 0;
    if (a >= 0x4000 && a <= 0x5fff) return G.sound_ram[a & 0x3ff];
    return 0xff;
#else
    if (a <= 0x17ff) return G.sound_rom[a];
    if (a <= 0x1fff) return 0;
    const uint16_t b = a & (uint16_t)~0x6c00;
    if (b >= 0x8000 && b <= 0x83ff) return G.sound_ram[a & 0x3ff];
    return 0xff;
#endif
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if FROGGER
    a &= 0x7fff;
    if (a >= 0x4000 && a <= 0x5fff) G.sound_ram[a & 0x3ff] = v;
#else
    const uint16_t b = a & (uint16_t)~0x6c00;
    if (b >= 0x8000 && b <= 0x83ff) G.sound_ram[a & 0x3ff] = v;
#endif
}

static uint8_t sound_in(void *u, uint16_t p)
{
    (void)u;
#if FROGGER
    return (p & 0xff) == 0x40 ? ay0_r() : 0xff;
#else
    uint8_t r = 0xff;
    if (p & 0x20) r &= ay8910_data_r(&G.ay1);
    if (p & 0x80) r &= ay0_r();
    return r;
#endif
}

static void sound_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
#if FROGGER
    if ((p & 0xff) == 0x40) ay8910_data_w(&G.ay0, v);
    else if ((p & 0xff) == 0x80) ay8910_address_w(&G.ay0, v);
#else
    if (p & 0x10) ay8910_address_w(&G.ay1, v);
    else if (p & 0x20) ay8910_data_w(&G.ay1, v);
    if (p & 0x40) ay8910_address_w(&G.ay0, v);
    else if (p & 0x80) ay8910_data_w(&G.ay0, v);
#endif
}
#endif

// ---------------------------------------------------------------------------
// The main CPU's maps

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
#if FROGGER
    if (a <= 0x3fff) return G.rom[a];
    if (a >= 0x8000 && a <= 0x87ff) return G.ram[a & 0x7ff];
    if (a >= 0xa800 && a <= 0xafff) return G.videoram[a & 0x3ff];
    if (a >= 0xb000 && a <= 0xb7ff) return obj_r(a & 0xff);
    if (a >= 0xc000) {
        uint8_t r = 0xff;
        if (a & 0x1000) r &= ppi_r(1, (a >> 1) & 3);
        if (a & 0x2000) r &= ppi_r(0, (a >> 1) & 3);
        return r;
    }
    return 0xff;
#elif SCRAMBLE
    if (a <= 0x3fff) return G.rom[a];
    if (a <= 0x47ff) return G.ram[a & 0x7ff];
    if (a <= 0x4fff) return G.videoram[a & 0x3ff];
    if (a <= 0x5fff) return obj_r(a & 0xff);
    if (a >= 0x8000) {
        uint8_t r = 0xff;
        if (a & 0x100) r &= ppi_r(0, a & 3);
        if (a & 0x200) r &= ppi_r(1, a & 3);
        return r;
    }
    return 0xff;
#else
#if MOONCRST
#define BASE 0x8000
#else
#define BASE 0x4000
#endif
    if (a <= 0x3fff) return G.rom[a];
    if (a >= BASE && a <= BASE + 0x7ff) return G.ram[a & 0x3ff];
    if (a >= BASE + 0x1000 && a <= BASE + 0x17ff) return G.videoram[a & 0x3ff];
    if (a >= BASE + 0x1800 && a <= BASE + 0x1fff) return obj_r(a & 0xff);
    if (a >= BASE + 0x2000 && a <= BASE + 0x27ff) return (uint8_t)(G.in0 | G.dsw_a);
    if (a >= BASE + 0x2800 && a <= BASE + 0x2fff) return (uint8_t)(G.in1 | G.dsw_b);
    if (a >= BASE + 0x3000 && a <= BASE + 0x37ff) return G.dsw_c;
    return 0xff;
#endif
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
#if FROGGER
    if (a >= 0x8000 && a <= 0x87ff) G.ram[a & 0x7ff] = v;
    else if (a >= 0xa800 && a <= 0xafff) G.videoram[a & 0x3ff] = v;
    else if (a >= 0xb000 && a <= 0xb7ff) obj_w(a & 0xff, v);
    else if (a >= 0xb800 && a <= 0xbfff) {
        if ((a & 0x1f) == 8) nmi_w(v);
    } else if (a >= 0xc000) {
        if (a & 0x1000) ppi_w(1, (a >> 1) & 3, v);
        if (a & 0x2000) ppi_w(0, (a >> 1) & 3, v);
    }
#elif SCRAMBLE
    if (a <= 0x47ff) G.ram[a & 0x7ff] = v;
    else if (a <= 0x4fff) G.videoram[a & 0x3ff] = v;
    else if (a <= 0x5fff) obj_w(a & 0xff, v);
    else if (a >= 0x6800 && a <= 0x6fff) {
        switch (a & 7) {
        case 1: nmi_w(v); break;
        case 3: G.background = (v & 1) != 0; break;
        case 4: G.stars_enable = (v & 1) != 0; break;
        default: break;
        }
    } else if (a >= 0x8000) {
        if (a & 0x100) ppi_w(0, a & 3, v);
        if (a & 0x200) ppi_w(1, a & 3, v);
    }
#else
    if (a >= BASE && a <= BASE + 0x7ff) G.ram[a & 0x3ff] = v;
    else if (a >= BASE + 0x1000 && a <= BASE + 0x17ff) G.videoram[a & 0x3ff] = v;
    else if (a >= BASE + 0x1800 && a <= BASE + 0x1fff) obj_w(a & 0xff, v);
#if MOONCRST
    else if (a >= 0xa000 && a <= 0xa7ff) {
        if ((a & 7) <= 2) G.gfx_bank[a & 7] = v;
    } else if (a >= 0xb000 && a <= 0xb7ff) {
        if ((a & 7) == 0) nmi_w(v);
        else if ((a & 7) == 4) G.stars_enable = (v & 1) != 0;
    }
#else
    else if (a >= 0x7000 && a <= 0x77ff) {
        if ((a & 7) == 1) nmi_w(v);
        else if ((a & 7) == 4) G.stars_enable = (v & 1) != 0;
    }
#endif
#endif
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void cycles(void *u, int c)
{
    (void)u;
#if KONAMI
    const int sc = (int)(((int64_t)c * SOUND_CLOCK + CLOCK / 2) / CLOCK);
    if (sc > 0) {
        const int ran = zd_run(&G.sound, sc);
        G.sound_cycles += (uint64_t)(ran > 0 ? ran : sc);
    }
#endif
    G.audio_acc += (int64_t)c * RATE;
    while (G.audio_acc >= CLOCK) {
        G.audio_acc -= CLOCK;
        int32_t s = 0;
#if KONAMI
        if (!G.sound_mute) s = FROGGER ? ay8910_render(&G.ay0) : ay8910_render(&G.ay0) + ay8910_render(&G.ay1);
#endif
        if (G.naudio < 1024) G.audio[G.naudio++] = arc_clamp16(s);
    }
}

#if FROGGER
// Frogger: the nibbles of x swapped, the colour bits turned
static int frogger_color(uint8_t v) { return ((((v >> 1) & 3) + ((v << 2) & 4)) & 7) << 2; }

static void update_video(void)
{
    // half of the (turned) screen is the river
    for (int i = 0; i < 128 * 256; i++) G.compose[i] = G.palette[35];
    for (int i = 128 * 256; i < 256 * 256; i++) G.compose[i] = G.palette[0];
    for (int offset = 0; offset < 0x400; offset++) {
        const int tile_x = 31 - (offset >> 5), tile_y = offset & 31;
        const uint8_t attr = G.attributes[tile_y * 2];
        const int color = frogger_color(G.attributes[1 + tile_y * 2]);
        const int scroll = tile_x * 8 + ((attr & 0x0f) << 4) + (attr >> 4);
        const uint8_t *px = G.chars + G.videoram[offset] * 64;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t p = px[y * 8 + x];
                if (p) G.compose[(tile_y * 8 + y) * 256 + ((scroll + x) & 0xff)] = G.palette[p + color];
            }
    }
    for (int i = 7; i >= 0; i--) {
        const uint8_t *e = &G.sprites[i * 4];
        const int y = e[3] + 1;
        if (y < 16) continue;
        const uint8_t attr = e[1];
        const bool fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
        const int color = frogger_color(e[2]), x = ((e[0] & 0x0f) << 4) | (e[0] >> 4);
        const uint8_t *px = G.sprite_gfx + (attr & 0x3f) * 256;
        for (int py = 0; py < 16; py++) {
            const int sy = y + py;
            if (sy >= 256) continue;
            const int src_y = fy ? 15 - py : py;
            for (int qx = 0; qx < 16; qx++) {
                const uint8_t p = px[src_y * 16 + (fx ? 15 - qx : qx)];
                if (p) G.compose[sy * 256 + ((x + qx) & 0xff)] = G.palette[p + color];
            }
        }
    }
    for (int y = 0; y < H; y++) memcpy(G.fb + y * W, G.compose + y * 256 + 16, W * 2);
}
#else
static void update_video(void)
{
    // the characters (pen 0 black), each row scrolled by its attribute
    for (int offset = 0; offset < 0x400; offset++) {
        const int tile_x = 31 - (offset >> 5), tile_y = offset & 31;
        const int color = (G.attributes[1 + (tile_y << 1)] & 7) << 2;
        const int scroll = tile_x * 8 + G.attributes[tile_y << 1];
        int code = G.videoram[offset];
#if MOONCRST
        if (G.gfx_bank[2] && (code & 0xc0) == 0x80) code = (code & 0x3f) | (G.gfx_bank[0] << 6) | (G.gfx_bank[1] << 7) | 0x100;
        code &= 0x1ff;
#endif
        const uint8_t *px = G.chars + code * 64;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t p = px[y * 8 + x];
                G.compose[(tile_y * 8 + y) * 256 + ((scroll + x) & 0xff)] = p ? G.palette[p + color] : 0;
            }
    }
    if (G.stars_enable) {
        uint32_t rng = 0x12345678u ^ G.stars_scroll;
        for (int n = 0; n < 64; n++) {
            rng = rng * 1664525u + 1013904223u;
            const int x = (int)((rng >> 8) + G.stars_scroll) & 0xff, y = (int)(rng >> 16) & 0xff;
            if (G.compose[y * 256 + x] != 0) continue;
            const uint32_t c = 0x40 + ((rng >> 24) & 3) * 0x3f;
            G.compose[y * 256 + x] = arc_rgb565(c << 16 | c << 8 | c);
        }
    }
    for (int f = 0; f < 8; f++) {
        int y = (SCRAMBLE ? 249 : 250) - G.bullets[3 + f * 4];
        if (f > 2) y += 1;
        const uint16_t color = G.palette[SCRAMBLE ? 32 : f == 7 ? 33 : 32];
        const int x = G.bullets[1 + f * 4];
        for (int dy = 0; dy < (SCRAMBLE ? 2 : 4); dy++)
            if (y + dy >= 0 && y + dy < 256) G.compose[(y + dy) * 256 + x] = color;
    }
    for (int i = 7; i >= 0; i--) {
        const uint8_t *e = &G.sprites[i * 4];
        const int y = e[3] + 1;
        if (y < 16) continue;
        const uint8_t attr = e[1];
        const bool fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
        const int color = (e[2] & 7) << 2, x = e[0];
        int code = attr & 0x3f;
#if MOONCRST
        if (G.gfx_bank[2] && (code & 0x30) == 0x20) code = (code & 0x0f) | (G.gfx_bank[0] << 4) | (G.gfx_bank[1] << 5) | 0x40;
        code &= 0x7f;
#endif
        const uint8_t *px = G.sprite_gfx + code * 256;
        for (int py = 0; py < 16; py++) {
            const int sy = y + py;
            if (sy >= 256) continue;
            const int src_y = fy ? 15 - py : py;
            for (int qx = 0; qx < 16; qx++) {
                const uint8_t p = px[src_y * 16 + (fx ? 15 - qx : qx)];
                if (p) G.compose[sy * 256 + ((x + qx) & 0xff)] = G.palette[p + color];
            }
        }
    }
    for (int y = 0; y < H; y++) memcpy(G.fb + y * W, G.compose + y * 256 + 16, W * 2);
    G.stars_scroll++;
}
#endif

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t gfx[GFX_SIZE], prom[0x20];
    if (!G.fb || !ARC_LOAD(kCpu, G.rom, ROM_SIZE) || !ARC_LOAD(kGfx, gfx, sizeof gfx) || !ARC_LOAD(kPal, prom, sizeof prom))
        return false;
#if MOONCRST
    for (int f = 0; f < 0x4000; f++) {  // the opcodes and data encrypted alike
        const uint8_t c1 = G.rom[f];
        uint8_t c2 = c1;
        if (c1 & 0x02) c2 ^= 0x40;
        if (c1 & 0x20) c2 ^= 0x04;
        if (!(f & 1)) c2 = (uint8_t)((c2 & 0xbb) | ((c2 >> 4) & 0x04) | ((c2 << 4) & 0x40));   // bits 2 and 6 swapped
        G.rom[f] = c2;
    }
#endif
#if KONAMI
    if (!ARC_LOAD(kSound, G.sound_rom, sizeof G.sound_rom)) return false;
#endif
#if FROGGER                             // D0 and D1 swapped
    for (int i = 0x800; i < 0x1000; i++) gfx[i] = (uint8_t)((gfx[i] & 0xfc) | ((gfx[i] & 1) << 1) | ((gfx[i] & 2) >> 1));
    for (int i = 0; i < 0x800; i++)
        G.sound_rom[i] = (uint8_t)((G.sound_rom[i] & 0xfc) | ((G.sound_rom[i] & 1) << 1) | ((G.sound_rom[i] & 2) >> 1));
#endif
    arc_layout cl = { 8, 8, CHARS, 2, 64, false, true, false, {0, CHARS * 64}, kX8, kY8 };
    arc_layout sl = { 16, 16, SPRITES, 2, 256, false, true, false, {0, SPRITES * 256}, kX16, kY16 };
    if (!(G.chars = arc_gfx_decode(&cl, gfx, sizeof gfx)) || !(G.sprite_gfx = arc_gfx_decode(&sl, gfx, sizeof gfx)))
        return false;
    static const arc_resnet nets[3] = {{3, {1000, 470, 220}, 470, 0}, {3, {1000, 470, 220}, 470, 0}, {2, {470, 220}, 470, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    for (int i = 0; i < 32; i++) {
        const uint8_t d = prom[i];
        const int r = arc_combine(w[0], 3, d & 7), g = arc_combine(w[1], 3, (d >> 3) & 7), b = arc_combine(w[2], 2, d >> 6);
        G.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    G.palette[32] = arc_rgb565(0xffffff);
    G.palette[33] = arc_rgb565(0xffff00);
    G.palette[35] = arc_rgb565(0x0040c0);   // the sky / the river
#if KONAMI
    G.sound.read = sound_read;
    G.sound.write = sound_write;
    G.sound.in = sound_in;
    G.sound.out = sound_out;
    zd_init(&G.sound);
    ay8910_init(&G.ay0, SOUND_CLOCK, RATE);
    ay8910_init(&G.ay1, SOUND_CLOCK, RATE);
#endif
#if MOONCRST
    G.dsw_b = 0x80;
#endif
    G.cpu.read = rd;
    G.cpu.write = wr;
    G.cpu.in = in_ff;
    G.cpu.out = out_nop;
    G.cpu.cycles = cycles;
    zd_init(&G.cpu);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&G.cpu);
    G.nmi_enable = G.stars_enable = false;
    G.stars_scroll = 0;
    G.in0 = G.in1 = 0;
#if KONAMI
    zd_reset(&G.sound);
    ay8910_reset(&G.ay0);
    ay8910_reset(&G.ay1);
    ppi_reset(&G.ppi0);
    ppi_reset(&G.ppi1);
    G.in0 = G.in1 = G.in2 = 0xff;
    G.background = G.sound_mute = false;
    G.sound_latch = G.port_b_latch = G.prot = 0;
    G.prot_state = 0;
    G.sound_cycles = 0;
#endif
#if MOONCRST
    memset(G.gfx_bank, 0, sizeof G.gfx_bank);
#endif
    memset(G.videoram, 0, sizeof G.videoram);
    memset(G.attributes, 0, sizeof G.attributes);
    memset(G.sprites, 0, sizeof G.sprites);
    memset(G.bullets, 0, sizeof G.bullets);
    G.audio_acc = 0;
    zd_set_nmi(&G.cpu, ZD_CLEAR);
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)((double)CLOCK / (FPS * SCANLINES) + 0.5);
    G.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 248 && G.nmi_enable) zd_set_nmi(&G.cpu, ZD_ASSERT);
        zd_run(&G.cpu, line_cycles);
        if (line == 248) zd_set_nmi(&G.cpu, ZD_CLEAR);
    }
    update_video();
    if (audio) *audio = G.audio;
    return G.naudio;
}

const uint16_t *arc_frame(void) { return G.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
#if FROGGER
    uint8_t a = 0xff, b = 0xff, c = 0xff;
    if (p2 & AP_UP) a &= ~0x01;
    if (p1 & AP_RIGHT) a &= ~0x10;
    if (p1 & AP_LEFT) a &= ~0x20;
    if (in->coin2) a &= ~0x40;
    if (in->coin1) a &= ~0x80;
    if (p2 & AP_RIGHT) b &= ~0x10;
    if (p2 & AP_LEFT) b &= ~0x20;
    if (p2 & AP_START) b &= ~0x40;
    if (p1 & AP_START) b &= ~0x80;
    if (p2 & AP_DOWN) c &= ~0x01;
    if (p1 & AP_UP) c &= ~0x10;
    if (p1 & AP_DOWN) c &= ~0x40;
    G.in0 = a, G.in1 = b, G.in2 = c;
#elif SCRAMBLE
    uint8_t a = 0xff, b = 0xff, c = 0xff;
    if (p2 & AP_UP) a &= ~0x01;
    if (p1 & AP_B2) a &= ~0x02;
    if (p1 & AP_B1) a &= ~0x08;
    if (p1 & AP_RIGHT) a &= ~0x10;
    if (p1 & AP_LEFT) a &= ~0x20;
    if (in->coin2) a &= ~0x40;
    if (in->coin1) a &= ~0x80;
    if (p2 & AP_B2) b &= ~0x04;
    if (p2 & AP_B1) b &= ~0x08;
    if (p2 & AP_RIGHT) b &= ~0x10;
    if (p2 & AP_LEFT) b &= ~0x20;
    if (p2 & AP_START) b &= ~0x40;
    if (p1 & AP_START) b &= ~0x80;
    if (p2 & AP_DOWN) c &= ~0x01;
    if (p1 & AP_UP) c &= ~0x10;
    if (p1 & AP_DOWN) c &= ~0x40;
    G.in0 = a, G.in1 = b, G.in2 = c;
#else
    uint8_t a = 0, b = 0;
    if (in->coin1) a |= 0x01;
    if (in->coin2) a |= 0x02;
    if (p1 & AP_LEFT) a |= 0x04;
    if (p1 & AP_RIGHT) a |= 0x08;
    if (p1 & AP_B1) a |= 0x10;
    if (p1 & AP_START) b |= 0x01;
    if (p2 & AP_START) b |= 0x02;
    G.in0 = a;
    G.in1 = b;
#endif
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) G.dsw_a = v;
    else if (bank == 1) G.dsw_b = v;
    else if (bank == 2) G.dsw_c = v;
}
