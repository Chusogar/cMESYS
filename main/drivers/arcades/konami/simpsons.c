// simpsons.c - Konami's The Simpsons (1991) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/simpsons (after
// dsp-emulator's simpsons_hw.pas), with its 053246 / 053247 sprites, its
// 053251 priority mixer, its 053260 PCM and its ER5911 EEPROM. The 052001
// CPU, the 052109 tilemaps (video/k052109d), a Z80 with a YM2151. As in
// dsp-cpp, a frame's sound is made at its end.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/konamidsp/konamidsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../video/k052109d.h"

#define W 288
#define H 224
#define FPS 59.185606
#define SCANLINES 264
#define MAIN_CLOCK 12000000                     // (dsp-cpp gives the 052001 its input clock here, not a quarter)
#define SOUND_CLOCK 3579545
#define RATE 44100
#define SPR_SIZE 0x400000
#define PCM_SIZE 0x140000

static const char *const kSets[] = { "simpsons", "simpsons2p", "simpsons4pa", NULL };
const arc_info arc_game = { "simpsons", "The Simpsons", "Konami", 1991, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {{"072-g02.16c", 0x20000, 0x00000, 0x580ce1d6}, {"072-g01.17c|072-p01.17c", 0x20000, 0x20000, 0},
                                {"072-j13.13c|072-013.13c", 0x20000, 0x40000, 0}, {"072-j12.15c|072-012.15c", 0x20000, 0x60000, 0}};
static const arc_rom kSound[] = {{"072-g03.6g", 0x20000, 0, 0x76c1850c}, {"072-e03.6g", 0x20000, 0, 0}};
static const arc_rom kTiles[] = {{"072-b07.18h", 0x80000, 0, 0xba1ec910}, {"072-b06.16h", 0x80000, 2, 0xcf2bbcab}};
static const arc_rom kSprites[] = {{"072-b08.3n", 0x100000, 0, 0x7de500ad}, {"072-b09.8n", 0x100000, 2, 0xaa085093},
                                   {"072-b10.12n", 0x100000, 4, 0x577dbd53}, {"072-b11.16l", 0x100000, 6, 0x55fab05d}};
static const arc_rom kSamples[] = {{"072-d05.1f", 0x100000, 0, 0x1397a73b}, {"072-d04.1d", 0x40000, 0x100000, 0x78778013}};
static const arc_rom kEeprom[] = {{"simpsons.12c.nv", 0x80, 0, 0}, {"simpsons2p.12c.nv", 0x80, 0, 0xfbac4e30}};

typedef struct { uint32_t start; uint16_t length, pitch; uint8_t volume, pan; bool loop, kadpcm, playing; uint32_t position; uint16_t counter; int8_t output; } voice_t;
enum { EE_RESET, EE_START, EE_COMMAND, EE_READING, EE_WAIT_DATA, EE_COMPLETION };
enum { C_INVALID, C_READ, C_WRITE, C_WRITE_ALL, C_ERASE_ALL, C_LOCK, C_UNLOCK };

static struct state {
    konamid cpu;
    zd_z80 snd;
    ym2151 ym;
    k052109d tiles;
    uint8_t *main_rom, *snd_rom, *tile_rom, *spr_rom, *pcm_rom;
    uint8_t mem[0x10000], snd_mem[0x10000], pal_buf[0x1000];
    uint16_t sprite_ram[0x800], pal[0x800];
    // the 053246 / 053247
    uint8_t kx46[8];
    uint16_t kx47[32], obj_ram[0x800], sorted[256];
    int sprite_count;
    bool objcha;
    int obj_dx, obj_dy;
    // the 053251
    uint8_t k251_ram[16], pal_index[5];
    // the 053260
    voice_t voice[4];
    uint8_t portdata[4], keyon, mode;
    double phase;
    // the ER5911
    uint8_t ee[0x80];
    int ee_cs, ee_clk, ee_di, ee_state, ee_cmd, ee_bits, ee_addr;
    bool ee_locked;
    uint32_t ee_accum, ee_shift;
    uint8_t bank0, bank2000, rom_bank1, sound_bank;
    int nmi_timer, dma_timer;
    bool ym_irq, main_snd_irq, firq_enabled;
    uint8_t in0, in1, in2, sprite_colorbase, layer_colorbase[3], layerpri[3];
    double frame_main, frame_snd;
    uint16_t pens[W * H], layer[W * H];
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------
// The ER5911 EEPROM (dsp-cpp's machine/eeprom_er5911)

static void ee_execute(void)
{
    S.ee_cmd = C_INVALID;
    S.ee_addr = (int)(S.ee_accum & 0x1ff);
    switch (S.ee_accum >> 9) {
    case 0:
        switch (S.ee_addr >> 7) {
        case 0: S.ee_cmd = C_LOCK; break;
        case 2: S.ee_cmd = C_ERASE_ALL; break;
        case 3: S.ee_cmd = C_UNLOCK; break;
        default: break;
        }
        S.ee_addr = 0;
        break;
    case 1: case 3: S.ee_cmd = C_WRITE; break;
    case 2: S.ee_cmd = C_READ; break;
    default: break;
    }
    S.ee_addr &= 0x7f;
    S.ee_bits = 0;
    switch (S.ee_cmd) {
    case C_READ: S.ee_shift = 0; S.ee_state = EE_READING; break;
    case C_WRITE: case C_WRITE_ALL: S.ee_shift = 0; S.ee_state = EE_WAIT_DATA; break;
    case C_LOCK: S.ee_locked = true; S.ee_state = EE_RESET; break;
    case C_UNLOCK: S.ee_locked = false; S.ee_state = EE_RESET; break;
    case C_ERASE_ALL:
        if (S.ee_locked) S.ee_state = EE_RESET;
        else {
            memset(S.ee, 0xff, sizeof S.ee);
            S.ee_state = EE_COMPLETION;
        }
        break;
    default: S.ee_state = EE_RESET; break;
    }
}

enum { EV_CS_UP, EV_CS_DOWN, EV_CLK_UP, EV_CLK_DOWN };

static void ee_event(int ev)
{
    switch (S.ee_state) {
    case EE_RESET:
        if (ev == EV_CS_UP) S.ee_state = EE_START;
        break;
    case EE_START:
        if (ev == EV_CLK_UP && S.ee_di == 1) {
            S.ee_accum = 0;
            S.ee_bits = 0;
            S.ee_state = EE_COMMAND;
        } else if (ev == EV_CS_DOWN) S.ee_state = EE_RESET;
        break;
    case EE_COMMAND:
        if (ev == EV_CLK_UP) {
            S.ee_accum = (S.ee_accum << 1) | (uint32_t)S.ee_di;
            if (++S.ee_bits == 2 + 9) ee_execute();
        } else if (ev == EV_CS_DOWN) S.ee_state = EE_RESET;
        break;
    case EE_READING:
        if (ev == EV_CLK_UP) {
            const int bi = S.ee_bits++;
            if (bi % 8 == 0) S.ee_shift = (uint32_t)S.ee[(S.ee_addr + bi / 8) & 0x7f] << 24;
            else S.ee_shift = (S.ee_shift << 1) | 1u;
        } else if (ev == EV_CS_DOWN) S.ee_state = EE_RESET;
        break;
    case EE_WAIT_DATA:
        if (ev == EV_CLK_UP) {
            S.ee_shift = (S.ee_shift << 1) | (uint32_t)S.ee_di;
            if (++S.ee_bits == 8) {
                if (S.ee_locked) S.ee_state = EE_RESET;
                else {
                    const uint8_t v = (uint8_t)S.ee_shift;
                    if (S.ee_cmd == C_WRITE) S.ee[S.ee_addr % 0x80] = v;
                    else if (S.ee_cmd == C_WRITE_ALL)
                        for (int i = 0; i < 0x80; i++) S.ee[i] &= v;
                    S.ee_state = EE_COMPLETION;
                }
            }
        } else if (ev == EV_CS_DOWN) S.ee_state = EE_RESET;
        break;
    case EE_COMPLETION:
        if (ev == EV_CS_DOWN) S.ee_state = EE_RESET;
        break;
    default: break;
    }
}

static void ee_line(int *line, int v, int up, int down)
{
    v = v ? 1 : 0;
    if (v == *line) return;
    *line = v;
    ee_event(v ? up : down);
}

static int ee_do(void) { return (S.ee_state == EE_READING && !(S.ee_shift & 0x80000000u)) ? 0 : 1; }

// ---------------------------------------------------------------------------
// The 053260 (dsp-cpp's sound/k053260)

static uint8_t k260_read(uint8_t o)
{
    o &= 0x3f;
    if (o <= 1) return S.portdata[o];
    if (o == 0x29) {
        uint8_t s = 0;
        for (int i = 0; i < 4; i++)
            if (S.voice[i].playing) s |= (uint8_t)(1 << i);
        return s;
    }
    if (o == 0x2e && (S.mode & 1)) {
        const uint32_t a = S.voice[0].start & 0xffffff;
        if (a < PCM_SIZE) {
            S.voice[0].start = (S.voice[0].start + 1) & 0xffffff;
            return S.pcm_rom[a];
        }
    }
    return 0;
}

static void k260_write(uint8_t o, uint8_t v)
{
    o &= 0x3f;
    if (o <= 1) return;
    if (o <= 3) {
        S.portdata[o] = v;
        return;
    }
    if (o >= 0x08 && o < 0x28) {
        voice_t *c = &S.voice[(o - 8) / 8];
        switch ((o - 8) & 7) {
        case 0: c->start = (c->start & 0xffff00) | v; break;
        case 1: c->start = (c->start & 0xff00ff) | ((uint32_t)v << 8); break;
        case 2: c->start = (c->start & 0x00ffff) | ((uint32_t)v << 16); break;
        case 3: c->length = (uint16_t)((c->length & 0xff00) | v); break;
        case 4: c->length = (uint16_t)((c->length & 0x00ff) | (v << 8)); break;
        case 5: c->pitch = (uint16_t)((c->pitch & 0xff00) | v); break;
        case 6: c->pitch = (uint16_t)((c->pitch & 0x00ff) | (v << 8)); break;
        default: c->volume = v; break;
        }
        return;
    }
    if (o >= 0x28 && o < 0x2c) return;             // (as dsp-cpp: key on / loop never reached)
    if (o == 0x2f) S.mode = v;
}

static int16_t k260_decode(voice_t *v)
{
    if (!v->playing) return 0;
    const uint32_t a = v->start + v->position;
    if (a >= PCM_SIZE || (v->length && v->position >= v->length)) {
        if (v->loop) v->position = 0;
        else {
            v->playing = false;
            return 0;
        }
    }
    const uint32_t i = v->start + v->position;
    const uint8_t raw = i < PCM_SIZE ? S.pcm_rom[i] : 0;
    v->position++;
    return (int16_t)((int16_t)(int8_t)raw * (int16_t)v->volume);
}

static void k260_update(int n, int16_t *l, int16_t *r)
{
    const double step = ((double)SOUND_CLOCK / 32.0) / 44100.0;
    for (int i = 0; i < n; i++) {
        int32_t ml = 0, mr = 0;
        S.phase += step;
        while (S.phase >= 1.0) {
            S.phase -= 1.0;
            for (int c = 0; c < 4; c++) {
                voice_t *v = &S.voice[c];
                if (!v->playing) continue;
                v->output = (int8_t)(k260_decode(v) / (v->volume > 1 ? (int)v->volume : 1));
            }
        }
        for (int c = 0; c < 4; c++) {
            const voice_t *v = &S.voice[c];
            if (!v->playing) continue;
            const int32_t s = (int32_t)v->output * (int32_t)v->volume;
            ml += s;
            mr += s;
        }
        l[i] = arc_clamp16(ml);
        r[i] = arc_clamp16(mr);
    }
}

// ---------------------------------------------------------------------------
// The 053251

static void k251_write(uint8_t o, uint8_t v)
{
    o &= 0x0f;
    v &= 0x3f;
    if (S.k251_ram[o] == v) return;
    S.k251_ram[o] = v;
    if (o == 9)
        for (int i = 0; i < 3; i++) S.pal_index[i] = (uint8_t)(32 * ((v >> (2 * i)) & 3));
    else if (o == 10)
        for (int i = 0; i < 2; i++) S.pal_index[3 + i] = (uint8_t)(16 * ((v >> (3 * i)) & 7));
}

static void k251_reset(void)
{
    memset(S.k251_ram, 0, sizeof S.k251_ram);
    memset(S.pal_index, 0, sizeof S.pal_index);
}

// ---------------------------------------------------------------------------
// The 053246 / 053247 sprites

static const int kObjX[16] = {8, 12, 0, 4, 24, 28, 16, 20, 40, 44, 32, 36, 56, 60, 48, 52};

// a pixel: 4bpp packed, the nibbles of a row permuted (bit offsets kObjX), rows of 8 bytes
static inline uint8_t obj_px(uint32_t code, int x, int y)
{
    const uint8_t b = S.spr_rom[(size_t)code * 128 + y * 8 + (kObjX[x] >> 3)];
    return (kObjX[x] & 4) ? b & 0x0f : b >> 4;
}

static void obj_draw_single(uint32_t code, int offs, uint16_t color, uint16_t *dest, int dw, int dh, int crop_x, int crop_y)
{
    static const int xoffset[8] = {0, 1, 4, 5, 16, 17, 20, 21}, yoffset[8] = {0, 2, 8, 10, 32, 34, 40, 42};
    const uint16_t t4 = S.obj_ram[offs];
    int oy = S.obj_ram[offs + 2] & 0x3ff, ox = S.obj_ram[offs + 3] & 0x3ff;
    const uint8_t sy_ = (uint8_t)S.obj_ram[offs + 4];
    const float zy = sy_ ? 64.0f / (float)sy_ : 0.f;
    float zx;
    if (!(t4 & 0x4000)) {
        const uint8_t sx_ = (uint8_t)S.obj_ram[offs + 5];
        zx = sx_ ? 64.0f / (float)sx_ : 0.f;
    } else zx = zy;
    if (zx <= 0.f || zy <= 0.f) return;
    const bool flipx = t4 & 0x1000, flipy = t4 & 0x2000;
    const int offx = (int16_t)(((uint16_t)S.kx46[0] << 8) | S.kx46[1]), offy = (int16_t)(((uint16_t)S.kx46[2] << 8) | S.kx46[3]);
    const bool small = S.kx47[6] & 0x40;
    const int wrap = small ? 512 : 1024, xlim = wrap - (small ? 64 : 384), ylim = wrap - (small ? 128 : 512);
    ox = (ox - offx) & (wrap - 1);
    oy = (-oy - offy) & (wrap - 1);
    if (ox >= xlim) ox -= wrap;
    if (oy >= ylim) oy -= wrap;
    const int sb = (t4 >> 8) & 0x0f, width = 1 << (sb & 3), height = 1 << ((sb >> 2) & 3);
    ox = ox - (int)(zx * (float)width * 8.f);
    oy = oy - (int)(zy * (float)height * 8.f);
    ox += 53 - S.obj_dx;
    oy -= 6 + S.obj_dy;
    int xa = 0, ya = 0;
    if (code & 0x01) xa += 1;
    if (code & 0x02) ya += 1;
    if (code & 0x04) xa += 2;
    if (code & 0x08) ya += 2;
    if (code & 0x10) xa += 4;
    if (code & 0x20) ya += 4;
    const uint32_t cbase = code & ~(uint32_t)0x3f, mask = SPR_SIZE / 128 - 1;
    const uint16_t base = (uint16_t)(color << 4);
    for (int y = 0; y < height; y++) {
        const int sy0 = oy + (int)lround((double)zy * (double)y * 16.0), sy1 = oy + (int)lround((double)zy * (double)(y + 1) * 16.0);
        const int dh16 = sy1 - sy0;
        if (dh16 <= 0) continue;
        for (int x = 0; x < width; x++) {
            const int sx0 = ox + (int)lround((double)zx * (double)x * 16.0), sx1 = ox + (int)lround((double)zx * (double)(x + 1) * 16.0);
            const int dw16 = sx1 - sx0;
            if (dw16 <= 0) continue;
            uint32_t tile;
            if (width == 1 && height == 1) tile = code;
            else {
                tile = cbase + (uint32_t)xoffset[(flipx ? width - 1 - x + xa : x + xa) & 7] + (uint32_t)yoffset[(flipy ? height - 1 - y + ya : y + ya) & 7];
            }
            tile &= mask;
            for (int py = 0; py < dh16; py++) {
                const int src_y = flipy ? 15 - (py * 16 / dh16) : py * 16 / dh16;
                for (int px = 0; px < dw16; px++) {
                    const uint8_t pen = obj_px(tile, flipx ? 15 - (px * 16 / dw16) : px * 16 / dw16, src_y);
                    if (!pen) continue;
                    const int dx = sx0 + px - crop_x, dy = sy0 + py - crop_y;
                    if (dx < 0 || dy < 0 || dx >= dw || dy >= dh) continue;
                    dest[dy * dw + dx] = (uint16_t)(base + pen);
                }
            }
        }
    }
}

static void obj_draw(uint16_t *dest, int dw, int dh, int cx, int cy, uint8_t prio)
{
    if (S.sprite_count <= 0) return;
    const bool asc = S.kx47[6] & 0x10;
    for (int y = 0; y < S.sprite_count - 1; y++)
        for (int x = y + 1; x < S.sprite_count; x++) {
            const int za = S.obj_ram[S.sorted[y]] & 0xff, zb = S.obj_ram[S.sorted[x]] & 0xff;
            if (asc ? za >= zb : za <= zb) {
                const uint16_t t = S.sorted[y];
                S.sorted[y] = S.sorted[x];
                S.sorted[x] = t;
            }
        }
    for (int f = 0; f < S.sprite_count; f++) {
        const int w = S.sorted[f];
        const uint32_t code = S.obj_ram[w + 1] & (SPR_SIZE / 128 - 1);
        uint16_t color = S.obj_ram[w + 6];
        const int pri = (color & 0xf80) >> 6;
        uint16_t pm;
        if (pri <= S.layerpri[2]) pm = 0;
        else if (pri <= S.layerpri[1]) pm = 1;
        else if (pri <= S.layerpri[0]) pm = 2;
        else pm = 3;
        color = (uint16_t)(S.sprite_colorbase + (color & 0x1f));
        if (pm != prio) continue;
        obj_draw_single(code, w, color, dest, dw, dh, cx, cy);
    }
}

// ---------------------------------------------------------------------------
// The 052001

static void update_sound_irq(void) { zd_set_irq(&S.snd, (S.ym_irq || S.main_snd_irq) ? ZD_HOLD : ZD_CLEAR, 0xff); }

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x0fff) return S.bank0 == 1 ? S.pal_buf[a] : k052109d_read(&S.tiles, a);
    if (a <= 0x1fff) {
        switch (a) {
        case 0x1f80: return S.in2;
        case 0x1f81: return (uint8_t)(0xcf | (ee_do() << 4) | (1 << 5));
        case 0x1f90: return S.in0;
        case 0x1f91: return S.in1;
        case 0x1f92: case 0x1f93: return 0xff;
        case 0x1fc4:
            S.main_snd_irq = true;
            update_sound_irq();
            return 0;
        case 0x1fc6: case 0x1fc7: return S.portdata[2 + (a & 1)];
        case 0x1fc8: case 0x1fc9:
            if (S.objcha) {
                const uint32_t ad = ((uint32_t)S.kx46[6] << 17) | ((uint32_t)S.kx46[7] << 9) | ((uint32_t)S.kx46[4] << 1) | (a & 1);
                return ad < SPR_SIZE ? S.spr_rom[ad] : 0;
            }
            return 0;
        case 0x1fca: return 0;
        default: return k052109d_read(&S.tiles, a);
        }
    }
    if (a <= 0x3fff) {
        if (S.bank2000 == 0) return k052109d_read(&S.tiles, a);
        if (a > 0x2fff) return S.mem[a];
        const uint16_t w = (a & 0xfff) >> 1;
        return (a & 1) ? (uint8_t)S.sprite_ram[w] : (uint8_t)(S.sprite_ram[w] >> 8);
    }
    if (a <= 0x5fff || a >= 0x8000) return S.mem[a];
    return S.main_rom[(S.rom_bank1 & 0x3f) * 0x2000 + (a & 0x1fff)];
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x0fff) {
        if (S.bank0 == 1) {
            if (S.pal_buf[a] != v) {
                S.pal_buf[a] = v;
                const int i = a >> 1;
                const uint16_t w = (uint16_t)((S.pal_buf[i * 2] << 8) | S.pal_buf[i * 2 + 1]);
                const unsigned r = (w & 0x1f) * 255 / 31, g = ((w >> 5) & 0x1f) * 255 / 31, b = ((w >> 10) & 0x1f) * 255 / 31;
                S.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                memset(S.tiles.dirty, 1, sizeof S.tiles.dirty);   // (the layers are cleaned, as dsp-cpp)
                for (int l = 0; l < 3; l++) memset(S.tiles.layers[l], 0, 512 * 256 * 2);
            }
        } else k052109d_write(&S.tiles, a, v);
        return;
    }
    if (a <= 0x1fff) {
        if (a >= 0x1fa0 && a <= 0x1fa7) S.kx46[a & 7] = v;
        else if (a >= 0x1fb0 && a <= 0x1fbf) k251_write(a & 0xf, v);
        else if (a == 0x1fc0) {
            S.tiles.rmrd = v & 8;
            S.objcha = !(v & 0x20);
        } else if (a == 0x1fc2) {
            if (v != 0xff) {
                S.ee_di = (v >> 7) & 1;
                ee_line(&S.ee_cs, (v >> 3) & 1, EV_CS_UP, EV_CS_DOWN);
                ee_line(&S.ee_clk, (v >> 4) & 1, EV_CLK_UP, EV_CLK_DOWN);
                S.bank0 = v & 1;
                S.bank2000 = (v >> 1) & 1;
                S.firq_enabled = v & 4;
            }
        } else if (a == 0x1fc6 || a == 0x1fc7) S.portdata[a & 1] = v;
        else k052109d_write(&S.tiles, a, v);
        return;
    }
    if (a <= 0x3fff) {
        if (S.bank2000 == 0) k052109d_write(&S.tiles, a, v);
        else if (a > 0x2fff) S.mem[a] = v;
        else {
            const uint16_t w = (a & 0xfff) >> 1;
            S.sprite_ram[w] = (a & 1) ? (uint16_t)((S.sprite_ram[w] & 0xff00) | v) : (uint16_t)((S.sprite_ram[w] & 0x00ff) | (v << 8));
        }
        return;
    }
    if (a <= 0x5fff) S.mem[a] = v;
}

static void set_lines(void *u, uint8_t v) { (void)u; S.rom_bank1 = v & 0x3f; }

static void tile_cb(void *u, int layer, int bank, uint32_t *code, uint16_t *color, uint16_t *flags, uint16_t *pri)
{
    (void)u, (void)flags, (void)pri;
    *code = *code | ((uint32_t)((*color & 0x3f) << 8) | ((uint32_t)bank << 14));
    *color = (uint16_t)(S.layer_colorbase[layer % 3] + ((*color & 0xc0) >> 6));
}

// ---------------------------------------------------------------------------
// The sound board

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff || (a >= 0xf000 && a <= 0xf7ff)) return S.snd_mem[a];
    if (a <= 0xbfff) {
        const int b = S.sound_bank & 7, off = b < 2 ? 0x8000 : 0x8000 + (b - 2) * 0x4000;
        return S.snd_rom[(off + 0x4000 > 0x20000 ? 0x20000 - 0x4000 : off) + (a & 0x3fff)];
    }
    if (a == 0xf801) {
        ym2151_run_timers(&S.ym, 1024);             // (dsp-cpp: so that timer B shows)
        return ym2151_status(&S.ym);
    }
    if (a >= 0xfc00 && a <= 0xfc2f) return k260_read(a & 0x3f);
    return 0;
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a >= 0xf000 && a <= 0xf7ff) S.snd_mem[a] = v;
    else if (a == 0xf800) ym2151_select(&S.ym, v);
    else if (a == 0xf801) ym2151_write(&S.ym, v);
    else if (a == 0xfa00) {
        zd_set_nmi(&S.snd, ZD_CLEAR);
        S.nmi_timer = 0;
    } else if (a >= 0xfc00 && a <= 0xfc2f) k260_write(a & 0x3f, v);
    else if (a == 0xfe00) S.sound_bank = v & 7;
}

static uint8_t no_in(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void no_out(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void snd_cycles(void *u, int c) { (void)u; if (c > 0) ym2151_run_timers(&S.ym, c); }
static void snd_irq_ack(void *u) { (void)u; S.main_snd_irq = false; update_sound_irq(); }
static void ym_irq(void *u, bool on) { (void)u; S.ym_irq = on; update_sound_irq(); }

// ---------------------------------------------------------------------------

static void objdma(void)
{
    uint16_t *dst = S.obj_ram;
    const uint16_t *src = S.sprite_ram;
    int inac = 256;
    for (int n = 0; n < 256; n++, src += 8)
        if ((src[0] & 0x8000) && (src[0] & 0xff)) {
            memcpy(dst, src, 16);
            dst += 8;
            inac--;
        }
    for (; inac > 0; inac--, dst += 8) dst[0] = 0;
}

static void update_video(void)
{
    const uint8_t bg = S.pal_index[0], lc0 = S.pal_index[2], lc1 = S.pal_index[3], lc2 = S.pal_index[4];
    S.sprite_colorbase = S.pal_index[1];
    S.layer_colorbase[0] = lc0, S.layer_colorbase[1] = lc1, S.layer_colorbase[2] = lc2;
    uint8_t sl[3] = {0, 1, 2};
    S.layerpri[0] = S.k251_ram[2], S.layerpri[1] = S.k251_ram[3], S.layerpri[2] = S.k251_ram[4];
    uint8_t *lp = S.layerpri;
#define SWAP(a, b) do { if (lp[a] < lp[b]) { uint8_t t = lp[a]; lp[a] = lp[b]; lp[b] = t; t = sl[a]; sl[a] = sl[b]; sl[b] = t; } } while (0)
    SWAP(0, 1);
    SWAP(0, 2);
    SWAP(1, 2);
#undef SWAP
    for (int i = 0; i < W * H; i++) S.pens[i] = (uint16_t)(bg * 16);
    memset(S.tiles.dirty, 1, sizeof S.tiles.dirty);   // (dsp-cpp redraws the layers every frame)
    for (int l = 0; l < 3; l++) memset(S.tiles.layers[l], 0, 512 * 256 * 2);
    k052109d_draw_tiles(&S.tiles);
    S.sprite_count = 0;
    for (int f = 0; f < 256; f++)
        if (S.obj_ram[f * 8] & 0x8000) S.sorted[S.sprite_count++] = (uint16_t)(f * 8);
    const int cx = 112, cy = 16;
    obj_draw(S.pens, W, H, cx, cy, 3);
    for (int pass = 0; pass < 3; pass++) {
        memset(S.layer, 0, sizeof S.layer);
        k052109d_draw_layer(&S.tiles, sl[pass], S.layer, W, H, cx, cy);
        for (int i = 0; i < W * H; i++)
            if (S.layer[i]) S.pens[i] = S.layer[i];
        obj_draw(S.pens, W, H, cx, cy, (uint8_t)(2 - pass));
    }
    for (int i = 0; i < W * H; i++) S.fb[i] = S.pal[S.pens[i] & 0x7ff];
}

static bool load_interleaved(const arc_rom *roms, int n, uint8_t *dest, uint32_t size, int stride)
{
    for (int i = 0; i < n; i++) {
        arc_rom r = roms[i];
        r.offset = 0;
        uint8_t *part = (uint8_t *)malloc(r.size);
        if (!part || !arc_load(&r, 1, part, r.size)) {
            free(part);
            return false;
        }
        uint32_t d = roms[i].offset;
        for (uint32_t h = 0; h < r.size / 2; h++, d += (uint32_t)stride) {
            if (d < size) dest[d] = part[h * 2];
            if (d + 1 < size) dest[d + 1] = part[h * 2 + 1];
        }
        free(part);
    }
    return true;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.main_rom = (uint8_t *)calloc(1, 0x80000);
    S.snd_rom = (uint8_t *)calloc(1, 0x20000);
    S.tile_rom = (uint8_t *)calloc(1, 0x100000);
    S.spr_rom = (uint8_t *)calloc(1, SPR_SIZE);
    S.pcm_rom = (uint8_t *)calloc(1, PCM_SIZE);
    if (!S.main_rom || !S.snd_rom || !S.tile_rom || !S.spr_rom || !S.pcm_rom) return false;
    if (!ARC_LOAD(kMain, S.main_rom, 0x80000)) return false;
    if (!arc_load(&kSound[0], 1, S.snd_rom, 0x20000) && !arc_load(&kSound[1], 1, S.snd_rom, 0x20000)) return false;
    if (!load_interleaved(kTiles, 2, S.tile_rom, 0x100000, 4) || !load_interleaved(kSprites, 4, S.spr_rom, SPR_SIZE, 8) ||
        !ARC_LOAD(kSamples, S.pcm_rom, PCM_SIZE))
        return false;
    memset(S.ee, 0xff, sizeof S.ee);
    if (!arc_load(&kEeprom[0], 1, S.ee, 0x80)) {
        memset(S.ee, 0xff, sizeof S.ee);
        if (!arc_load(&kEeprom[1], 1, S.ee, 0x80)) memset(S.ee, 0xff, sizeof S.ee);
    }
    memcpy(S.mem + 0x8000, S.main_rom + 0x78000, 0x8000);
    memcpy(S.snd_mem, S.snd_rom, 0x8000);
    if (!k052109d_init(&S.tiles, tile_cb, NULL, S.tile_rom, 0x100000)) return false;
    S.obj_dx = 0, S.obj_dy = 16;
    S.cpu.rd = main_rd;
    S.cpu.wr = main_wr;
    S.cpu.set_lines = set_lines;
    S.snd.read = snd_rd;
    S.snd.write = snd_wr;
    S.snd.in = no_in;
    S.snd.out = no_out;
    S.snd.cycles = snd_cycles;
    S.snd.irq_ack = snd_irq_ack;
    zd_init(&S.snd);
    ym2151_init(&S.ym, SOUND_CLOCK, RATE);
    S.ym.irq_handler = ym_irq;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    konamid_reset(&S.cpu);
    zd_reset(&S.snd);
    zd_set_nmi(&S.snd, ZD_ASSERT);                  // (MAME asserts it at reset; cleared after a while)
    S.nmi_timer = 200;
    ym2151_reset(&S.ym);
    memset(S.voice, 0, sizeof S.voice);             // the 053260
    memset(S.portdata, 0, sizeof S.portdata);
    S.keyon = S.mode = 0;
    S.phase = 0;
    k052109d_reset(&S.tiles);
    memset(S.kx46, 0, sizeof S.kx46);               // the 053246
    memset(S.kx47, 0, sizeof S.kx47);
    S.objcha = false;
    S.sprite_count = 0;
    k251_reset();
    S.ee_cs = S.ee_clk = S.ee_di = 0;               // the ER5911
    S.ee_state = EE_RESET;
    S.ee_cmd = C_INVALID;
    S.ee_locked = false;
    S.ee_bits = S.ee_addr = 0;
    S.ee_accum = S.ee_shift = 0;
    S.bank0 = S.bank2000 = S.rom_bank1 = S.sound_bank = 0;
    S.firq_enabled = false;
    S.in0 = S.in1 = S.in2 = 0xff;
    S.dma_timer = 0;
    S.frame_main = S.frame_snd = 0;
    memset(S.sprite_ram, 0, sizeof S.sprite_ram);
    memset(S.pal_buf, 0, sizeof S.pal_buf);
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = (double)MAIN_CLOCK / FPS / SCANLINES, sc = (double)SOUND_CLOCK / FPS / SCANLINES;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 224) {
            if (S.tiles.irq_enabled) konamid_set_irq(&S.cpu, KC_HOLD);
            objdma();
            if (S.kx46[5] & 0x10) S.dma_timer = 1;
            update_video();
        }
        if (S.dma_timer > 0) {
            if (S.dma_timer == 1 && S.firq_enabled) konamid_set_firq(&S.cpu, KC_ASSERT);
            if (S.dma_timer == 8) {
                konamid_set_firq(&S.cpu, KC_CLEAR);
                S.dma_timer = 0;
            } else S.dma_timer++;
        }
        konamid_run(&S.cpu, (int)(mc + S.frame_main));
        S.frame_main = (mc + S.frame_main) - (int)(mc + S.frame_main);
        zd_run(&S.snd, (int)(sc + S.frame_snd));
        if (S.nmi_timer > 0) {
            S.nmi_timer -= (int)sc;
            if (S.nmi_timer <= 0) {
                S.nmi_timer = 0;
                zd_set_nmi(&S.snd, ZD_CLEAR);
            }
        }
        S.frame_snd = (sc + S.frame_snd) - (int)(sc + S.frame_snd);
    }
    const int n = (int)(44100.0 / FPS + 0.5);
    int16_t l[1024], r[1024];
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    k260_update(n, l, r);
    for (int i = 0; i < n; i++) S.audio[i] = arc_clamp16((int)ym2151_update(&S.ym) + ((int)l[i] + (int)r[i]) / 2);
    S.naudio = n;
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    static const unsigned bits[6] = {AP_LEFT, AP_RIGHT, AP_UP, AP_DOWN, AP_B1, AP_B2};
    S.in0 = S.in1 = S.in2 = 0xff;
    for (int b = 0; b < 6; b++) {
        if (in->p[0] & bits[b]) S.in0 &= (uint8_t)~(1 << b);
        if (in->p[1] & bits[b]) S.in1 &= (uint8_t)~(1 << b);
    }
    if (in->p[0] & AP_START) S.in0 &= 0x7f;
    if (in->p[1] & AP_START) S.in1 &= 0x7f;
    if (in->coin1) S.in2 &= 0xfe;
    if (in->coin2) S.in2 &= 0xfd;
}

void arc_dip(int bank, uint8_t v) { (void)bank, (void)v; }
