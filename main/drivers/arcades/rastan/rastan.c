// rastan.c - Rastan / Rastan Saga (Taito, 1987) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/rastan_hw (from
// dsp-emulator's rastan_hw.pas). A 68000 at 8 MHz, a Z80 at 4 MHz behind the
// TC0140SYT, a YM2151 and an MSM5205 streamed from its ADPCM ROM, the PC080SN
// tilemaps and PC090OJ sprites on a 512 x 512 work area cropped to 320 x 240.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/tc0140sytd.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/msm5205dsp.h"

#define W 320
#define H 240
#define WW 512
#define WH 512
#define FPS 60.0
#define SCANLINES 256
#define MAIN_CLOCK 8000000
#define SOUND_CLOCK 4000000
#define RATE 44100
#define N(k) ((int)(sizeof k / sizeof k[0]))

static const char *const kSets[] = { "rastan", NULL };
const arc_info arc_game = { "rastan", "Rastan", "Taito", 1987, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };

// MAME's rastan (World rev 1)
static const arc_rom kMainRoms[] = {
    {"b04-38.19", 0x10000, 0x00000, 0x1c91dbb1}, {"b04-37.7", 0x10000, 0x00001, 0xecf20bdd},
    {"b04-40.20", 0x10000, 0x20000, 0x0930d4b3}, {"b04-39.8", 0x10000, 0x20001, 0xd95ade5e},
    {"b04-42.21", 0x10000, 0x40000, 0x1857a7cb}, {"b04-43-1.9", 0x10000, 0x40001, 0xca4702ff},
};
static const arc_rom kCharRoms[] = {
    {"b04-01.40", 0x20000, 0x00000, 0xcd30de19}, {"b04-03.39", 0x20000, 0x20000, 0xab67e064},
    {"b04-02.67", 0x20000, 0x40000, 0x54040fec}, {"b04-04.66", 0x20000, 0x60000, 0x94737e93},
};
static const arc_rom kSpriteRoms[] = {
    {"b04-05.15", 0x20000, 0x00000, 0xc22d94ac}, {"b04-07.14", 0x20000, 0x20000, 0xb5632a51},
    {"b04-06.28", 0x20000, 0x40000, 0x002ccf39}, {"b04-08.27", 0x20000, 0x60000, 0xfeafca05},
};
static const arc_rom kSoundRom[] = {{"b04-19.49", 0x10000, 0, 0xee81fdd8}};
static const arc_rom kAdpcmRom[] = {{"b04-20.76", 0x10000, 0, 0xfd1a34cc}};

// rastan_hw.pas's ps_x: the pixel columns, bit offsets into the packed 4 bpp ROM
static const int kPixelX[16] = {0, 4, 0x200000, 0x200004, 8, 12, 0x200008, 0x20000c,
                                16, 20, 0x200010, 0x200014, 24, 28, 0x200018, 0x20001c};
static const int kTileY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kSpriteY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};

static struct state {
    m68kd cpu;
    zd_z80 snd;
    ym2151 ym;
    msm5205d msm;
    tc0140sytd syt;
    uint16_t rom[0x30000], ram1[0x2000], ram2[0x8000], ram3[0x2000], palette_ram[0x800];
    uint8_t sound_ram[0x1000], sound_rom[0x10000], adpcm_rom[0x10000];
    uint8_t sound_bank;
    uint16_t adpcm_pos;
    uint8_t adpcm_val;
    bool adpcm_have_val;
    uint8_t *tiles, *sprites;
    uint32_t palette[0x800];
    uint32_t background[WW * WH], foreground[WW * WH], composite[WW * WH];
    uint16_t fb[W * H];
    uint16_t scroll_x1, scroll_y1, scroll_x2, scroll_y2;
    uint8_t sprite_bank, in0, in1, in2, dsw_a, dsw_b;
    int64_t audio_acc, msm_acc;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static void set_palette(int index, uint16_t v)  // (xBGR 555)
{
#define P5(x) ((uint8_t)((((x) & 0x1f) << 3) | (((x) & 0x1f) >> 2)))
    S.palette[index] = 0xff000000u | ((uint32_t)P5(v) << 16) | ((uint32_t)P5(v >> 5) << 8) | P5(v >> 10);
#undef P5
}

static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x5ffff) return (a >> 1) < 0x30000 ? S.rom[a >> 1] : 0;
    if (a >= 0x10c000 && a <= 0x10ffff) return S.ram1[(a & 0x3fff) >> 1];
    if (a >= 0x200000 && a <= 0x200fff) return S.palette_ram[(a & 0xfff) >> 1];
    switch (a & ~1u) {
    case 0x390000: return S.in1;
    case 0x390002: return S.in2;
    case 0x390004: return 0x8f;
    case 0x390006: return S.in0;
    case 0x390008: return S.dsw_a;
    case 0x39000a: return S.dsw_b;
    case 0x3e0002: return 0;                    // (word reads leave the mailbox alone)
    default: break;
    }
    if (a >= 0xc00000 && a <= 0xc0ffff) return S.ram2[(a & 0xffff) >> 1];
    if (a >= 0xd00000 && a <= 0xd03fff) return S.ram3[(a & 0x3fff) >> 1];
    return 0;
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a <= 0x5ffff) return;
    if (a >= 0x10c000 && a <= 0x10ffff) { S.ram1[(a & 0x3fff) >> 1] = v; return; }
    if (a >= 0x200000 && a <= 0x200fff) {
        const int i = (int)((a & 0xfff) >> 1);
        if (S.palette_ram[i] != v) {
            S.palette_ram[i] = v;
            set_palette(i, v);
        }
        return;
    }
    switch (a & ~1u) {
    case 0x350008: case 0x3c0000: return;
    case 0x380000: S.sprite_bank = (uint8_t)((v & 0xe0) >> 5); return;
    case 0x3e0000: tc0140sytd_port_w(&S.syt, (uint8_t)(v >> 8)); return;
    case 0x3e0002: tc0140sytd_comm_w(&S.syt, (uint8_t)(v >> 8)); return;
    case 0xc20000: S.scroll_y1 = (uint16_t)((512 - v) & 0x1ff); return;
    case 0xc20002: S.scroll_y2 = (uint16_t)((512 - v) & 0x1ff); return;
    case 0xc40000: S.scroll_x1 = (uint16_t)((512 - v) & 0x1ff); return;
    case 0xc40002: S.scroll_x2 = (uint16_t)((512 - v) & 0x1ff); return;
    default: break;
    }
    if (a >= 0xc00000 && a <= 0xc0ffff) { S.ram2[(a & 0xffff) >> 1] = v; return; }
    if (a >= 0xc50000 && a <= 0xc50003) return;
    if (a >= 0xd00000 && a <= 0xd03fff) S.ram3[(a & 0x3fff) >> 1] = v;
}

static uint16_t cpu_rw(void *u, uint32_t a) { (void)u; return main_read(a); }
static void cpu_ww(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }
static uint8_t cpu_rb(void *u, uint32_t a)
{
    (void)u;
    a &= 0xffffff;
    if ((a & ~1u) == 0x3e0002) return (a & 1) == 0 ? tc0140sytd_comm_r(&S.syt) : 0;
    const uint16_t w = main_read(a);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static void cpu_wb(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0xffffff;
    if ((a & ~1u) == 0x3e0000) {
        if ((a & 1) == 0) tc0140sytd_port_w(&S.syt, v);
        return;
    }
    if ((a & ~1u) == 0x3e0002) {
        if ((a & 1) == 0) tc0140sytd_comm_w(&S.syt, v);
        return;
    }
    uint16_t w = main_read(a);
    w = (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0x00ff) | (v << 8));
    main_write(a, w);
}

// ---------------------------------------------------------------------------
// Sound

static void reset_sound(void)
{
    zd_reset(&S.snd);
    ym2151_reset(&S.ym);
    msm5205d_reset(&S.msm);
    S.sound_bank = 0;
    S.adpcm_pos = 0;
    S.adpcm_val = 0;
    S.adpcm_have_val = false;
    S.audio_acc = S.msm_acc = 0;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return S.sound_rom[a];
    if (a <= 0x7fff) return S.sound_rom[S.sound_bank * 0x4000 + (a & 0x3fff)];
    if (a <= 0x8fff) return S.sound_ram[a & 0xfff];
    if (a == 0x9001) return ym2151_status(&S.ym);
    if (a == 0xa001) return tc0140sytd_slave_comm_r(&S.syt);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x8fff) { S.sound_ram[a & 0xfff] = v; return; }
    switch (a) {
    case 0x9000: ym2151_select(&S.ym, v); break;
    case 0x9001: ym2151_write(&S.ym, v); break;
    case 0xa000: tc0140sytd_slave_port_w(&S.syt, v); break;
    case 0xa001: tc0140sytd_slave_comm_w(&S.syt, v); break;
    case 0xb000: S.adpcm_pos = (uint16_t)((S.adpcm_pos & 0xff) | (v << 8)); break;
    case 0xc000: msm5205d_set_reset(&S.msm, false); break;
    case 0xd000:
        msm5205d_set_reset(&S.msm, true);
        S.adpcm_pos &= 0xff00;
        break;
    default: break;
    }
}

static void advance_adpcm(void *u)              // (rastan_snd_adpcm: the high, then the low nibble)
{
    (void)u;
    if (S.adpcm_have_val) {
        msm5205d_data_w(&S.msm, S.adpcm_val & 0x0f);
        S.adpcm_have_val = false;
        S.adpcm_pos = (uint16_t)(S.adpcm_pos + 1);
    } else {
        S.adpcm_val = S.adpcm_rom[S.adpcm_pos];
        msm5205d_data_w(&S.msm, S.adpcm_val >> 4);
        S.adpcm_have_val = true;
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, cycles);
    S.msm_acc += (int64_t)cycles * msm5205d_sample_frequency(&S.msm);
    while (S.msm_acc >= SOUND_CLOCK) {
        S.msm_acc -= SOUND_CLOCK;
        msm5205d_vclk(&S.msm);
    }
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&S.ym) + msm5205d_output(&S.msm) / 4;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.snd, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }
static void ym_port(void *u, uint8_t v) { (void)u; S.sound_bank = v & 3; }
static void syt_nmi(void *u) { (void)u; zd_set_nmi(&S.snd, ZD_PULSE); }
static void syt_reset(void *u) { (void)u; reset_sound(); }

// ---------------------------------------------------------------------------
// Video

static void draw_tilemap(bool fg)
{
    uint32_t *target = fg ? S.foreground : S.background;
    const int base = fg ? 0x4000 : 0;
    for (int tile = 0; tile < 0x1000; tile++) {
        const uint16_t attrib = S.ram2[base + tile * 2];
        const int code = S.ram2[base + tile * 2 + 1] & 0x3fff, color = (attrib & 0x7f) << 4;
        const bool fx = (attrib & 0x4000) != 0, fy = (attrib & 0x8000) != 0;
        const int x = tile % 64, y = tile / 64;
        const uint8_t *px = S.tiles + (size_t)code * 64;
        for (int row = 0; row < 8; row++) {
            const int sy = fy ? 7 - row : row;
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = px[sy * 8 + (fx ? 7 - col : col)];
                const size_t d = (size_t)((y * 8 + row) * WW + x * 8 + col);
                target[d] = (fg && pen == 0) ? 0 : S.palette[color + pen];
            }
        }
    }
}

static void blit_layer(const uint32_t *layer, uint16_t sx, uint16_t sy, bool transparent)
{
    for (int y = 0; y < WH; y++) {
        const int src_y = (y + sy) & 0x1ff;
        for (int x = 0; x < WW; x++) {
            const uint32_t p = layer[src_y * WW + ((x + sx) & 0x1ff)];
            if (transparent && p == 0) continue;
            S.composite[y * WW + x] = p;
        }
    }
}

static void draw_sprites(void)
{
    for (int index = 255; index >= 0; index--) {
        const uint16_t *e = &S.ram3[index * 4];
        const int code = e[2] & 0xfff;
        if (code == 0) continue;
        const uint16_t attrib = e[0];
        const int color = ((attrib & 0x0f) | ((S.sprite_bank & 0x0f) << 4)) << 4;
        const bool fx = (attrib & 0x4000) != 0, fy = (attrib & 0x8000) != 0;
        const int pos_x = ((int)e[3] + 16) & 0x1ff, pos_y = (int)e[1] & 0x1ff;
        const uint8_t *px = S.sprites + (size_t)code * 256;
        for (int row = 0; row < 16; row++) {
            const int y = (pos_y + row) & 0x1ff;
            const int sy = fy ? 15 - row : row;
            for (int col = 0; col < 16; col++) {
                const int x = (pos_x + col) & 0x1ff;
                const uint8_t pen = px[sy * 16 + (fx ? 15 - col : col)];
                if (pen) S.composite[y * WW + x] = S.palette[color + pen];
            }
        }
    }
}

static void update_video(void)
{
    draw_tilemap(false);
    draw_tilemap(true);
    blit_layer(S.background, S.scroll_x1, S.scroll_y1, false);
    blit_layer(S.foreground, S.scroll_x2, S.scroll_y2, true);
    draw_sprites();
    for (int y = 0; y < H; y++)                 // (actualiza_trozo_final(16, 8, 320, 240))
        for (int x = 0; x < W; x++) {
            const uint32_t c = S.composite[(y + 8) * WW + x + 16];
            S.fb[y * W + x] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
        }
}

// ---------------------------------------------------------------------------

static bool load_raw(const arc_rom *l, int n, uint8_t *dst)
{
    for (int i = 0; i < n; i++) {
        arc_rom one = l[i];
        one.offset = 0;
        if (!arc_load(&one, 1, dst + l[i].offset, one.size)) return false;
    }
    return true;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.dsw_a = S.dsw_b = 0xff;
    uint8_t *buf = (uint8_t *)malloc(0x80000);
    if (!buf) return false;
    for (int i = 0; i < N(kMainRoms); i++) {    // (the even file: the high byte)
        arc_rom one = kMainRoms[i];
        one.offset = 0;
        if (!arc_load(&one, 1, buf, one.size)) return false;
        const bool high = (kMainRoms[i].offset & 1) == 0;
        for (uint32_t j = 0, k = kMainRoms[i].offset >> 1; j < one.size; j++, k++)
            S.rom[k] = high ? (uint16_t)((buf[j] << 8) | (S.rom[k] & 0x00ff)) : (uint16_t)((S.rom[k] & 0xff00) | buf[j]);
    }
    if (!load_raw(kSoundRom, 1, S.sound_rom) || !load_raw(kAdpcmRom, 1, S.adpcm_rom)) return false;
    const arc_layout tl = {8, 8, 0x4000, 4, 16 * 8, false, false, false, {0, 1, 2, 3}, kPixelX, kTileY};
    const arc_layout sl = {16, 16, 0x1000, 4, 64 * 8, false, false, false, {0, 1, 2, 3}, kPixelX, kSpriteY};
    memset(buf, 0, 0x80000);
    if (!load_raw(kCharRoms, N(kCharRoms), buf) || !(S.tiles = arc_gfx_decode(&tl, buf, 0x80000))) return false;
    memset(buf, 0, 0x80000);
    if (!load_raw(kSpriteRoms, N(kSpriteRoms), buf) || !(S.sprites = arc_gfx_decode(&sl, buf, 0x80000))) return false;
    free(buf);

    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = cpu_rw;
    S.cpu.write_word = cpu_ww;
    S.cpu.read_byte = cpu_rb;
    S.cpu.write_byte = cpu_wb;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.cycles = sound_cycles;
    zd_init(&S.snd);
    ym2151_init(&S.ym, 4000000, RATE);
    S.ym.irq_handler = ym_irq;
    S.ym.port_handler = ym_port;
    msm5205d_init(&S.msm, 384000, 48, 4);
    S.msm.vclk_handler = advance_adpcm;
    S.syt.nmi_cb = syt_nmi;
    S.syt.reset_cb = syt_reset;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&S.cpu);
    tc0140sytd_reset(&S.syt);
    reset_sound();
    S.in0 = 0x1f;
    S.in1 = S.in2 = 0xff;
    S.scroll_x1 = S.scroll_y1 = S.scroll_x2 = S.scroll_y2 = 0;
    S.sprite_bank = 0;
    for (int i = 0; i < WW * WH; i++) S.background[i] = S.composite[i] = 0xff000000u;
}

int arc_run_frame(const int16_t **audio)
{
    const int main_cycles = (int)((double)MAIN_CLOCK / FPS / SCANLINES + 0.5);
    const int snd = (int)((double)SOUND_CLOCK / FPS / SCANLINES + 0.5);
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 248) {
            update_video();
            m68kd_set_irq(&S.cpu, 5, M68_HOLD);
        }
        m68kd_run(&S.cpu, main_cycles);
        zd_run(&S.snd, snd);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static uint8_t joy(unsigned p)
{
    uint8_t r = 0xff;
    if (p & AP_UP) r &= 0xfe;
    if (p & AP_DOWN) r &= 0xfd;
    if (p & AP_LEFT) r &= 0xfb;
    if (p & AP_RIGHT) r &= 0xf7;
    if (p & AP_B1) r &= 0xef;
    if (p & AP_B2) r &= 0xdf;
    return r;
}

void arc_input(const arc_in *in)
{
    S.in0 = 0x1f;
    if (in->coin1) S.in0 |= 0x20;
    if (in->coin2) S.in0 |= 0x40;
    S.in1 = joy(in->p[0]);
    S.in2 = joy(in->p[1]);
    if (in->p[0] & AP_START) S.in0 &= 0xf7;
    if (in->p[1] & AP_START) S.in0 &= 0xef;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    if (bank == 1) S.dsw_b = v;
}
