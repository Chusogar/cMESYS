// actfancr.c - Data East's Act-Fancer Cybernetick Hyper Weapon (1989) for the
// common arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/
// actfancer (after dsp-emulator's actfancer_hw.pas). A HuC6280 at 7.16 MHz
// (cpu/hu6280dsp), two BAC06 playfields (16x16 background, 8x8 text) and the
// MXC06 sprites (video/deco_bac06d), a 6502 sound CPU with a YM2203, a
// YM3812 and an OKI M6295.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/hu6280dsp/hu6280dsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/ym3812dsp.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/deco_bac06d.h"

#define W 256
#define H 240
#define FPS 57.444885
#define SCANLINES 256
#define MAIN_CLOCK (21477200 / 3)
#define SOUND_CLOCK 1500000
#define RATE 44100

static const char *const kSets[] = { "actfancr", "actfancr2", "actfancr1", "actfancrj", NULL };
const arc_info arc_game = { "actfancr", "Act-Fancer Cybernetick Hyper Weapon", "Data East", 1989, W, H, FPS, RATE, kSets,
                            { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {{"fe08-3.bin", 0x10000, 0x00000, 0x35f1999d}, {"fe09-3.bin", 0x10000, 0x10000, 0xd21416ca}, {"fe10-3.bin", 0x10000, 0x20000, 0x85535fcc}};
static const arc_rom kChars[] = {{"15", 0x10000, 0x00000, 0xa1baf21e}, {"16", 0x10000, 0x10000, 0x22e64730}};
static const arc_rom kSound[] = {{"17-1", 0x8000, 0x8000, 0x289ad106}};
static const arc_rom kOki[] = {{"18", 0x10000, 0x00000, 0x5c55b242}};
static const arc_rom kTiles[] = {{"14", 0x10000, 0x00000, 0xd6457420}, {"12", 0x10000, 0x10000, 0x08787b7a}, {"13", 0x10000, 0x20000, 0xc30c37dc}, {"11", 0x10000, 0x30000, 0x1f006d9f}};
static const arc_rom kSprites[] = {
    {"02", 0x10000, 0x00000, 0xb1db0efc}, {"03", 0x08000, 0x10000, 0xf313e04f}, {"06", 0x10000, 0x18000, 0x8cb6dd87}, {"07", 0x08000, 0x28000, 0xdd345def},
    {"00", 0x10000, 0x30000, 0xd50a9550}, {"01", 0x08000, 0x40000, 0x34935e93}, {"04", 0x10000, 0x48000, 0xbcf41795}, {"05", 0x08000, 0x58000, 0xd38b94aa}};

static const int kPtX[16] = {128, 129, 130, 131, 132, 133, 134, 135, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kPtY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

static struct state {
    hu6280d cpu;
    m65xx snd;
    ym3812d ym3812;
    ym2203d ym2203;
    okim6295d oki;
    bac06_chip bac;
    bac06_gfx gchars, gtiles, gsprites;
    uint8_t rom[0x30000], ram[0x4000], snd_mem[0x10000], oki_rom[0x10000], spr_buf[0x800], pal_ram[0x800];
    uint16_t pal[0x400];
    uint16_t pens[256 * 256];
    uint8_t sound_latch, in0, in1, in2, dsw_a, dsw_b;
    uint32_t frames;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *A_;
#define A (*A_)

// ---------------------------------------------------------------------------
// The HuC6280

static void word_byte(uint16_t *w, uint32_t a, uint8_t v)
{
    *w = (a & 1) ? (uint16_t)((*w & 0x00ff) | (v << 8)) : (uint16_t)((*w & 0xff00) | v);
}

static uint8_t main_rd(void *u, uint32_t a)
{
    (void)u;
    a &= 0x1fffff;
    if (a <= 0x2ffff) return A.rom[a];
    if (a >= 0x62000 && a <= 0x63fff) return (uint8_t)(A.bac.tile[0].data[(a & 0x1fff) >> 1] >> (8 * (a & 1)));
    if (a >= 0x72000 && a <= 0x727ff) return (uint8_t)(A.bac.tile[1].data[(a & 0x7ff) >> 1] >> (8 * (a & 1)));
    if (a >= 0x100000 && a <= 0x1007ff) return A.spr_buf[a & 0x7ff];
    if (a >= 0x120000 && a <= 0x1205ff) return A.pal_ram[a & 0x7ff];
    if (a == 0x130000) return A.in0;
    if (a == 0x130001) return A.in2;
    if (a == 0x130002) return A.dsw_a;
    if (a == 0x130003) return A.dsw_b;
    if (a == 0x140000) return A.in1;
    if (a >= 0x1f0000 && a <= 0x1f3fff) return A.ram[a & 0x3fff];
    return 0;
}

static void main_wr(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0x1fffff;
    if (a <= 0x2ffff) return;
    for (int l = 0; l < 2; l++) {
        const uint32_t base = l ? 0x70000 : 0x60000;
        bac06_layer *ly = &A.bac.tile[l];
        if (a >= base && a <= base + 7) {           // (even: the low byte, odd: the high one)
            uint16_t w = ly->control_0[(a & 7) >> 1];
            word_byte(&w, a, v);
            bac06_control0(ly, (int)((a & 7) >> 1), w);
            return;
        }
        if (a >= base + 0x10 && a <= base + 0x1f) {
            uint16_t w = ly->control_1[(a & 7) >> 1];
            word_byte(&w, a, v);
            bac06_control1(ly, (int)((a & 7) >> 1), w, false);
            return;
        }
        const uint32_t mask = l ? 0x7ff : 0x1fff;
        if (a >= base + 0x2000 && a <= base + 0x2000 + mask) {
            const size_t i = (a & mask) >> 1;
            if (i < 0x2000) word_byte(&ly->data[i], a, v);
            return;
        }
    }
    if (a >= 0x100000 && a <= 0x1007ff) A.spr_buf[a & 0x7ff] = v;
    else if (a == 0x110000) {
        for (int i = 0; i < 0x400; i++) A.bac.sprite_ram[i] = (uint16_t)(A.spr_buf[i * 2] | (A.spr_buf[i * 2 + 1] << 8));
    } else if (a >= 0x120000 && a <= 0x1205ff) {
        const size_t o = a & 0x7ff;
        if (A.pal_ram[o] != v) {
            A.pal_ram[o] = v;
            const int i = (int)(o >> 1);
            if (i < 0x300) {
                const uint8_t rg = A.pal_ram[i * 2], b = A.pal_ram[i * 2 + 1];
                const unsigned r = (rg & 15) * 0x11, g = (rg >> 4) * 0x11, bl = (b & 15) * 0x11;
                A.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3));
            }
        }
    } else if (a == 0x150000) {
        A.sound_latch = v;
        m65xx_set_nmi(&A.snd, M65_PULSE);
    } else if (a >= 0x1f0000 && a <= 0x1f3fff) A.ram[a & 0x3fff] = v;
}

// ---------------------------------------------------------------------------
// The sound board

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a == 0x3000) return A.sound_latch;
    if (a == 0x3800) return okid_read(&A.oki);
    if (a <= 0x7ff || a >= 0x4000) return A.snd_mem[a];
    return 0;
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7ff) A.snd_mem[a] = v;
    else if (a == 0x800) ym2203d_control(&A.ym2203, v);
    else if (a == 0x801) ym2203d_write(&A.ym2203, v);
    else if (a == 0x1000) ym3812d_control(&A.ym3812, v);
    else if (a == 0x1001) ym3812d_write(&A.ym3812, v);
    else if (a == 0x3800) okid_write(&A.oki, v);
}

static void snd_cycles(void *u, int c)
{
    (void)u;
    A.audio_acc += (int64_t)c * RATE;
    while (A.audio_acc >= SOUND_CLOCK) {
        A.audio_acc -= SOUND_CLOCK;
        const int s = ym3812d_update(&A.ym3812) + ym2203d_update(&A.ym2203) + okid_update(&A.oki);
        if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; m65xx_set_irq(&A.snd, on ? M65_HOLD : M65_CLEAR); }

// ---------------------------------------------------------------------------

static void update_video(void)
{
    memset(A.pens, 0, sizeof A.pens);
    bac06_draw(&A.bac.tile[0], &A.gtiles, false, false, A.pens);
    bac06_draw_sprites(&A.bac, &A.gsprites, 0, 0, A.frames & 1, A.pens);
    bac06_draw(&A.bac.tile[1], &A.gchars, true, false, A.pens);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) A.fb[y * W + x] = A.pal[A.pens[(y + 8) * 256 + x] & 0x3ff];
}

static uint8_t *decode(const arc_rom *roms, int n, size_t size, const arc_layout *l, bool *ok)
{
    uint8_t *raw = (uint8_t *)calloc(1, size), *set = NULL;
    if (raw && *ok && (*ok = arc_load(roms, n, raw, size))) set = arc_gfx_decode(l, raw, size);
    free(raw);
    if (!set) *ok = false;
    return set;
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    bool ok = ARC_LOAD(kMain, A.rom, sizeof A.rom) && ARC_LOAD(kSound, A.snd_mem, sizeof A.snd_mem) && ARC_LOAD(kOki, A.oki_rom, sizeof A.oki_rom);
    const arc_layout cl = {8, 8, 0x1000, 4, 64, false, false, false, {0x8000 * 8, 0x18000 * 8, 0, 0x10000 * 8}, kPtX + 8, kPtY};
    const arc_layout tl = {16, 16, 0xc00, 4, 256, false, false, false, {0, 0x10000 * 8, 0x20000 * 8, 0x30000 * 8}, kPtX, kPtY};
    const arc_layout sl = {16, 16, 0xc00, 4, 256, false, false, false, {0, 0x18000 * 8, 0x30000 * 8, 0x48000 * 8}, kPtX, kPtY};
    A.gchars = (bac06_gfx){decode(kChars, 2, 0x20000, &cl, &ok), 0x1000};
    A.gtiles = (bac06_gfx){decode(kTiles, 4, 0x40000, &tl, &ok), 0xc00};
    A.gsprites = (bac06_gfx){decode(kSprites, 8, 0x60000, &sl, &ok), 0xc00};
    if (!ok) return false;
    A.dsw_a = 0x7f;
    A.dsw_b = 0xff;
    hu6280d_init(&A.cpu, MAIN_CLOCK);
    A.cpu.read = main_rd;
    A.cpu.write = main_wr;
    A.snd.read = snd_rd;
    A.snd.write = snd_wr;
    A.snd.cycles = snd_cycles;
    ym3812d_init(&A.ym3812, 3000000, false, 1.0f);
    A.ym3812.irq = ym_irq;
    ym2203d_init(&A.ym2203, 1500000, 1.0f, 1.0f);
    okid_init(&A.oki, 1024188, true, A.oki_rom, sizeof A.oki_rom);
    bac06_init(&A.bac, 0x100, 0x000, 0x000, 2, 1, 1, 0x200, 0x0f);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    hu6280d_reset(&A.cpu);
    m65xx_reset(&A.snd);
    ym3812d_reset(&A.ym3812);
    ym2203d_reset(&A.ym2203);
    okid_reset(&A.oki);
    bac06_reset(&A.bac);
    memset(A.ram, 0, sizeof A.ram);
    memset(A.spr_buf, 0, sizeof A.spr_buf);
    memset(A.pal_ram, 0, sizeof A.pal_ram);
    memset(A.pal, 0, sizeof A.pal);
    A.sound_latch = 0;
    A.in0 = 0xff;
    A.in1 = 0x7f;
    A.in2 = 0xff;
    A.frames = 0;
    A.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / FPS / SCANLINES + 0.5), sc = (int)((double)SOUND_CLOCK / FPS / SCANLINES + 0.5);
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 8) A.in1 &= 0x7f;              // VBLANK off
        if (line == 248) {
            hu6280d_set_irq_line(&A.cpu, 0, HU_HOLD);
            update_video();
            A.in1 |= 0x80;
        }
        hu6280d_run(&A.cpu, mc);
        m65xx_run(&A.snd, sc);
    }
    A.frames++;
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    A.in0 = A.in2 = 0xff;
    A.in1 = (uint8_t)(0x7f | (A.in1 & 0x80));
    static const unsigned bits[6] = {AP_UP, AP_DOWN, AP_LEFT, AP_RIGHT, AP_B1, AP_B2};
    for (int b = 0; b < 6; b++) {
        if (p1 & bits[b]) A.in0 &= (uint8_t)~(1 << b);
        if (p2 & bits[b]) A.in2 &= (uint8_t)~(1 << b);
    }
    if (p1 & AP_START) A.in0 &= 0x7f;
    if (p2 & AP_START) A.in2 &= 0x7f;
    if (in->coin1) A.in1 &= 0xfe;
    if (in->coin2) A.in1 &= 0xfd;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw_a = v;
    if (bank == 1) A.dsw_b = v;
}
