// baraduke.c - Namco's Baraduke and Metro-Cross (1985) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/baraduke_hw (after
// dsp-emulator's baraduke_hw.pas) and sound/namco_cus30. A 6809 at 1.536 MHz,
// an HD63701V MCU at 6.144 MHz reading the inputs and DIPs on its port 1 and
// driving the CUS30 (8 voices of 32-nibble waves, shared with the 6809), two
// scrolling 64x32 tile layers of 8x8 3bpp tiles, 8x8 text and 16x16 sprites
// in up to 2x2 blocks. ARC_VARIANT 1: Metro-Cross.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/hd63701dsp/hd63701dsp.h"

#define METRO (ARC_VARIANT == 1)
#define W 288
#define H 224
#define TW 512
#define TH 256
#define FPS 60.606060606
#define SCANLINES 264
#define MAIN_CLOCK (49152000 / 32)
#define MCU_CLOCK (49152000 / 8)
#define RATE 44100

#if METRO
static const char *const kSets[] = { "metrocrs", NULL };
const arc_info arc_game = { "metrocrs", "Metro-Cross", "Namco", 1985, W, H, FPS, RATE, kSets, { "SALTO", NULL }, ARC_JOY_8WAY };
static const arc_rom kRom[] = {{"mc1-3.9c", 0x2000, 0x6000, 0x3390b33c}, {"mc1-1.9a", 0x4000, 0x8000, 0x10b0977e}, {"mc1-2.9b", 0x4000, 0xc000, 0x5c846f35}};
static const arc_rom kMcu[] = {{"mc1-4.3b", 0x2000, 0x1000, 0x9c88f898}, {"cus60-60a1.mcu", 0x1000, 0x0000, 0x076ea82a}};
static const arc_rom kChars[] = {{"mc1-5.3j", 0x2000, 0x0000, 0x9b5ea33a}};
static const arc_rom kTiles[] = {{"mc1-7.4p", 0x4000, 0x0000, 0xc9dfa003}, {"mc1-6.4n", 0x4000, 0x4000, 0x9686dc3c}};
static const arc_rom kSprites[] = {{"mc1-8.8k", 0x4000, 0x0000, 0x265b31fa}, {"mc1-9.8l", 0x4000, 0x4000, 0x541ec029}};
static const arc_rom kProm[] = {{"mc1-1.1n", 0x800, 0x0000, 0x32a78a8b}, {"mc1-2.2m", 0x800, 0x0800, 0x6f4dca7b}};
#define MCU_EXT 0x2000
#define SPRITES 0x100
#define SPRITEX_ADD (-1)
#define SPRITEY_ADD (-32)
#else
static const char *const kSets[] = { "baraduke", "aliensec", NULL };
const arc_info arc_game = { "baraduke", "Baraduke", "Namco", 1985, W, H, FPS, RATE, kSets, { "DISPARO", NULL }, ARC_JOY_8WAY };
static const arc_rom kRom[] = {{"bd1_3.9c", 0x2000, 0x6000, 0xea2ea790}, {"bd1_1.9a", 0x4000, 0x8000, 0x4e9f2bdc}, {"bd1_2.9b", 0x4000, 0xc000, 0x40617fcd}};
static const arc_rom kMcu[] = {{"bd1_4b.3b", 0x4000, 0x1000, 0xa47ecd32}, {"cus60-60a1.mcu", 0x1000, 0x0000, 0x076ea82a}};
static const arc_rom kChars[] = {{"bd1_5.3j", 0x2000, 0x0000, 0x706b7fee}};
static const arc_rom kTiles[] = {{"bd1_8.4p", 0x4000, 0x0000, 0xb0bb0710}, {"bd1_7.4n", 0x4000, 0x4000, 0x0d7ebec9}, {"bd1_6.4m", 0x4000, 0x8000, 0xe5da0896}};
static const arc_rom kSprites[] = {{"bd1_9.8k", 0x4000, 0x0000, 0x87a29acc}, {"bd1_10.8l", 0x4000, 0x4000, 0x72b6d20c}, {"bd1_11.8m", 0x4000, 0x8000, 0x3076af9c}, {"bd1_12.8n", 0x4000, 0xc000, 0x8b4c09a3}};
static const arc_rom kProm[] = {{"bd1-1.1n", 0x800, 0x0000, 0x0d78ebc6}, {"bd1-2.2m", 0x800, 0x0800, 0x03f7241f}};
#define MCU_EXT 0x4000
#define SPRITES 0x200
#define SPRITEX_ADD 184
#define SPRITEY_ADD (-14)
#endif

typedef struct { uint8_t volume, waveform; int frequency; bool active; uint32_t pos; } voice_t;

static struct state {
    m6809d cpu;
    hd63701 mcu;
    uint8_t mem[0x10000], snd[0x10000];
    uint8_t *chars, *tiles[2], *sprites;
    uint16_t pal[0x800];
    uint16_t text[W * H], layer[2][TW * TH];   // palette index + 1 (0: transparent)
    bool text_dirty[0x400], tile_dirty[2][0x800];
    uint8_t in0, in1, in2, dsw_a, dsw_b, dsw_c, port_sel;
    uint16_t counter, scroll_x0, scroll_x1;
    uint8_t scroll_y0, scroll_y1;
    bool prio, copy_sprites;
    int64_t audio_acc;
    // the CUS30
    uint8_t regs[0x40], ram[0x400], wave[0x200];
    voice_t voice[8];
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *B_;
#define B (*B_)

// ---------------------------------------------------------------------------
// The CUS30 (dsp-cpp's sound/namco_cus30.h)

static uint8_t cus30_r(uint16_t a) { return B.ram[a & 0x3ff]; }

static void cus30_w(uint16_t a, uint8_t v)
{
    const uint16_t off = a & 0x3ff;
    if (off <= 0xff) {
        if (B.ram[off] == v) return;
        B.ram[off] = v;
        B.wave[off * 2] = (v >> 4) & 0x0f;
        B.wave[off * 2 + 1] = v & 0x0f;
        return;
    }
    B.ram[off] = v;
    if (off > 0x13f) return;
    const uint8_t r = off & 0x3f;
    if (B.regs[r] == v) return;
    B.regs[r] = v;
    const int ch = r / 8;
    voice_t *vc = &B.voice[ch];
    switch (r - ch * 8) {
    case 0: vc->volume = v & 0x0f; break;
    case 1: vc->waveform = (v >> 4) & 0x0f; break;
    case 2: case 3: {
        int f = (B.regs[ch * 8 + 1] & 0x0f) << 16;
        f += B.regs[ch * 8 + 2] << 8;
        vc->frequency = (f + B.regs[ch * 8 + 3]) * (24000000 / RATE);
        break;
    }
    default: break;
    }
    if (vc->frequency == 0 && vc->volume == 0) {
        vc->active = false;
        vc->pos = 0;
    } else vc->active = true;
}

static int16_t cus30_update(void)
{
    int32_t s = 0;
    for (int i = 0; i < 8; i++) {
        voice_t *v = &B.voice[i];
        if (!v->active) continue;
        s += (int32_t)B.wave[32 * v->waveform + ((v->pos >> 25) & 0x1f)] * v->volume * 64;
        v->pos += (uint32_t)v->frequency;
    }
    s = (s / 8) * 4;
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

// ---------------------------------------------------------------------------
// The 6809

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff || (a >= 0x4800 && a <= 0x4fff) || a >= 0x6000) return B.mem[a];
    if (a >= 0x4000 && a <= 0x43ff) return cus30_r(a);
    return 0xff;
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) {
        B.mem[a] = v;
        if (a == 0x1ff2) B.copy_sprites = true;
    } else if (a <= 0x3fff) {
        const int l = a >= 0x3000;
        if (B.mem[a] != v) {
            B.tile_dirty[l][(a & 0xfff) >> 1] = true;
            B.mem[a] = v;
        }
    } else if (a <= 0x43ff) cus30_w(a, v);
    else if (a >= 0x4800 && a <= 0x4fff) {
        if (B.mem[a] != v) {
            B.text_dirty[a & 0x3ff] = true;
            B.mem[a] = v;
        }
    } else if (a == 0x8800) m6809d_set_irq(&B.cpu, M6809D_CLEAR);
    else if (a == 0xb000) {
        B.scroll_x0 = (uint16_t)((B.scroll_x0 & 0xff) | (v << 8));
        const bool p = ((B.scroll_x0 & 0x0e00) >> 9) == 6;
        if (p != B.prio) {
            B.prio = p;
            memset(B.tile_dirty, 1, sizeof B.tile_dirty);
        }
    } else if (a == 0xb001) B.scroll_x0 = (uint16_t)((B.scroll_x0 & 0xff00) | v);
    else if (a == 0xb002) B.scroll_y0 = v;
    else if (a == 0xb004) B.scroll_x1 = (uint16_t)((B.scroll_x1 & 0xff) | (v << 8));
    else if (a == 0xb005) B.scroll_x1 = (uint16_t)((B.scroll_x1 & 0xff00) | v);
    else if (a == 0xb006) B.scroll_y1 = v;
}

// ---------------------------------------------------------------------------
// The MCU

static uint8_t mcu_rd(void *u, uint16_t a)
{
    (void)u;
    if (a == 0x1105) return (uint8_t)((++B.counter >> 4) & 0xff);
    if (a >= 0x1000 && a <= 0x13ff) return cus30_r(a);
    if (a >= 0x8000 && a <= 0xc7ff) return B.snd[a];
    return 0xff;
}

static void mcu_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x1000 && a <= 0x13ff) cus30_w(a, v);
    else if (a >= 0xc000 && a <= 0xc7ff) B.snd[a] = v;
}

static uint8_t port1_r(void *u)
{
    (void)u;
    switch (B.port_sel) {
    case 0: return (B.dsw_a & 0xf8) >> 3;
    case 1: return (uint8_t)(((B.dsw_a & 7) << 2) | ((B.dsw_b & 0xc0) >> 6));
    case 2: return (B.dsw_b & 0x3e) >> 1;
    case 3: return (uint8_t)(((B.dsw_b & 1) << 4) | (B.dsw_c & 0x0f));
    case 4: return B.in0;
    case 5: return B.in2;
    case 6: return B.in1;
    default: return 0xff;
    }
}

static void port1_w(void *u, uint8_t v)
{
    (void)u;
    if ((v & 0xe0) == 0x60) B.port_sel = v & 7;
}

static void mcu_cycles(void *u, int c)
{
    (void)u;
    B.audio_acc += (int64_t)c * RATE;
    while (B.audio_acc >= MCU_CLOCK) {
        B.audio_acc -= MCU_CLOCK;
        const int16_t s = cus30_update();
        if (B.naudio < 1024) B.audio[B.naudio++] = s;
    }
}

// ---------------------------------------------------------------------------
// Video

static void draw_text(void)
{
    for (int x = 0; x < 36; x++)
        for (int y = 0; y < 28; y++) {
            const int sx = x - 2, sy = y + 2;
            const int pos = (sx & 0x20) ? sy + ((sx & 0x1f) << 5) : sx + (sy << 5);
            if (!B.text_dirty[pos & 0x3ff]) continue;
            B.text_dirty[pos & 0x3ff] = false;
            const int base = B.mem[0x4c00 + pos] << 4;
            const uint8_t *px = B.chars + B.mem[0x4800 + pos] * 64;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    const uint8_t pen = px[r * 8 + c];
                    B.text[(y * 8 + r) * W + x * 8 + c] = pen == 3 ? 0 : (uint16_t)(((base + pen) & 0x7ff) + 1);
                }
        }
}

static void draw_tiles(int l, bool transparent)
{
    const int base = l ? 0x3000 : 0x2000;
    for (int f = 0; f < 0x800; f++) {
        if (!B.tile_dirty[l][f]) continue;
        B.tile_dirty[l][f] = false;
        const int x = f % 64, y = f / 64;
        const uint8_t atr = B.mem[base + 1 + f * 2];
        const int code = B.mem[base + f * 2] + ((atr & 3) << 8), cb = atr << 3;
        const uint8_t *px = B.tiles[l] + code * 64;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                const uint8_t pen = px[r * 8 + c];
                B.layer[l][(y * 8 + r) * TW + x * 8 + c] = (transparent && pen == 7) ? 0 : (uint16_t)(((cb + pen) & 0x7ff) + 1);
            }
    }
}

static void scroll_layer(int l, int sx0, int sy0)
{
    for (int y = 0; y < H; y++) {
        const uint16_t *src = B.layer[l] + ((sy0 + y) & 0xff) * TW;
        uint16_t *d = B.fb + y * W;
        for (int x = 0; x < W; x++) {
            const uint16_t p = src[(sx0 + x) & 0x1ff];
            if (p) d[x] = B.pal[p - 1];
        }
    }
}

static void draw_sprites(int priority)
{
    static const uint8_t offs[2][2] = {{0, 1}, {2, 3}};
    const int xoffs = (int)B.mem[0x7f5] - 256 * (B.mem[0x7f4] & 1), yoffs = B.mem[0x7f7];
    for (int f = 0; f <= 0x7e; f++) {
        const uint8_t *s = B.mem + 0x1800 + f * 0x10;
        const uint8_t a1 = s[0xa];
        if (priority != (a1 & 1)) continue;
        const uint8_t a2 = s[0xe], color = s[0xc];
        const bool flipx = a1 & 0x20, flipy = a2 & 1;
        const int sizex = (a1 & 0x80) >> 7, sizey = (a2 & 4) >> 2;
        const int sx = ((int)s[0xd] + ((color & 1) << 8) + xoffs + SPRITEX_ADD) & 0x1ff;
        const int sy = (240 - (int)s[0xf]) - yoffs - 16 * sizey + SPRITEY_ADD;
        int code = s[0xb] * 4;
        if ((a1 & 0x10) && sizex == 0) code += 1;
        if ((a2 & 0x10) && sizey == 0) code += 2;
        const int cb = (color & 0xfe) << 3;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++) {
                const uint8_t *px = B.sprites + ((code + offs[y ^ (sizey * flipy)][x ^ (sizex * flipx)]) % SPRITES) * 256;
                for (int r = 0; r < 16; r++) {
                    const int dy = sy + 16 * y + r;
                    if (dy < 0 || dy >= H) continue;
                    const int srow = flipy ? 15 - r : r;
                    for (int c = 0; c < 16; c++) {
                        const int dx = (sx + 16 * x + c) & 0x1ff;
                        if (dx >= W) continue;
                        const uint8_t pen = px[srow * 16 + (flipx ? 15 - c : c)];
                        if (pen != 15) B.fb[dy * W + dx] = B.pal[(cb + pen) & 0x7ff];
                    }
                }
            }
    }
}

static void update_video(void)
{
    draw_text();
    draw_tiles(0, B.prio);
    draw_tiles(1, !B.prio);
    memset(B.fb, 0, sizeof B.fb);
    if (B.prio) {
        scroll_layer(1, B.scroll_x1 + 24, B.scroll_y1 + 25);
        draw_sprites(0);
        scroll_layer(0, B.scroll_x0 + 26, B.scroll_y0 + 25);
    } else {
        scroll_layer(0, B.scroll_x0 + 26, B.scroll_y0 + 25);
        draw_sprites(0);
        scroll_layer(1, B.scroll_x1 + 24, B.scroll_y1 + 25);
    }
    draw_sprites(1);
    for (int i = 0; i < W * H; i++)
        if (B.text[i]) B.fb[i] = B.pal[B.text[i] - 1];
}

// ---------------------------------------------------------------------------

static const int kCharX[] = {64, 65, 66, 67, 0, 1, 2, 3}, kCharY[] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kTileX[] = {0, 1, 2, 3, 8, 9, 10, 11}, kTileY[] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kSprX[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
static const int kSprY[] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};

static int weight(int n) { return (n & 1) * 0x0e + ((n >> 1) & 1) * 0x1f + ((n >> 2) & 1) * 0x43 + ((n >> 3) & 1) * 0x8f; }

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *buf = (uint8_t *)calloc(1, 0x10000);
    uint8_t *t = (uint8_t *)calloc(1, 0x10000), *bank = (uint8_t *)calloc(1, 0x10000);
    if (!buf || !t || !bank) return false;
    bool ok = ARC_LOAD(kRom, B.mem, sizeof B.mem);
    memset(buf, 0, 0x5000);
    ok = ok && ARC_LOAD(kMcu, buf, 0x1000 + MCU_EXT);
    hd63701_init(&B.mcu, MCU_CLOCK, HD_HD63701V);
    memcpy(B.mcu.rom + 0x1000, buf, 0x1000);            // the CUS60 at $f000
    memcpy(B.snd + 0x8000, buf + 0x1000, MCU_EXT);      // its program at $8000
    arc_layout cl = {8, 8, 0x200, 2, 128, false, false, false, {0, 4}, kCharX, kCharY};
    memset(buf, 0, 0x2000);
    ok = ok && ARC_LOAD(kChars, buf, 0x2000);
    if (ok) B.chars = arc_gfx_decode(&cl, buf, 0x2000);
    // the tiles: the third plane unpacked as baraduke_hw.pas does
    memset(t, 0, 0x10000);
    if (METRO) memset(t + 0x8000, 0xff, 0x4000);
    ok = ok && ARC_LOAD(kTiles, t, 0xc000);
    if (METRO) memset(t + 0x8000, 0xff, 0x4000);
    for (int f = 0x2000; f <= 0x3fff; f++) {
        t[0x8000 + f + 0x2000] = t[0x8000 + f];
        t[0x8000 + f + 0x4000] = (uint8_t)(t[0x8000 + f] << 4);
    }
    for (int f = 0; f <= 0x1fff; f++) t[0x8000 + f + 0x2000] = (uint8_t)(t[0x8000 + f] << 4);
    arc_layout tl = {8, 8, 0x400, 3, 128, false, false, false, {0x8000 * 8, 0, 4}, kTileX, kTileY};
    for (int l = 0; l < 2 && ok; l++) {
        memset(bank, 0, 0x10000);
        memcpy(bank, t + l * 0x4000, 0x4000);
        memcpy(bank + 0x8000, t + 0x8000, 0x8000);
        B.tiles[l] = arc_gfx_decode(&tl, bank, 0x10000);
    }
    memset(buf, 0, 0x10000);
    ok = ok && ARC_LOAD(kSprites, buf, SPRITES * 128);
    arc_layout sl = {16, 16, SPRITES, 4, 1024, false, false, false, {0, 1, 2, 3}, kSprX, kSprY};
    if (ok) B.sprites = arc_gfx_decode(&sl, buf, SPRITES * 128);
    memset(buf, 0, 0x1000);
    ok = ok && ARC_LOAD(kProm, buf, 0x1000);
    for (int f = 0; f < 0x800 && ok; f++) {
        const uint8_t lo = buf[f], hi = buf[f + 0x800];
        const int r = weight(hi & 15), g = weight(lo & 15), b = weight(lo >> 4);
        B.pal[f] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    free(buf);
    free(t);
    free(bank);
    if (!ok || !B.chars || !B.tiles[0] || !B.tiles[1] || !B.sprites) return false;
    memset(B.text_dirty, 1, sizeof B.text_dirty);
    memset(B.tile_dirty, 1, sizeof B.tile_dirty);
    B.dsw_a = B.dsw_b = B.dsw_c = 0xff;
    B.cpu.read = main_rd;
    B.cpu.write = main_wr;
    B.mcu.read_mem = mcu_rd;
    B.mcu.write_mem = mcu_wr;
    B.mcu.port_read[0] = port1_r;
    B.mcu.port_write[0] = port1_w;
    B.mcu.cycles = mcu_cycles;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&B.cpu);
    hd63701_reset(&B.mcu);
    memset(B.regs, 0, sizeof B.regs);                   // the CUS30
    memset(B.ram, 0, sizeof B.ram);
    memset(B.wave, 0, sizeof B.wave);
    memset(B.voice, 0, sizeof B.voice);
    B.in0 = B.in1 = B.in2 = 0x1f;
    B.scroll_x0 = B.scroll_x1 = 0;
    B.scroll_y0 = B.scroll_y1 = 0;
    B.prio = B.copy_sprites = false;
    B.port_sel = 0;
    B.counter = 0;
    B.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int main_cycles = (int)((double)MAIN_CLOCK / FPS / SCANLINES);
    const int mcu_cycles = (int)((double)MCU_CLOCK / FPS / SCANLINES);
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            update_video();
            m6809d_set_irq(&B.cpu, M6809D_ASSERT);
            hd63701_set_irq(&B.mcu, HD_HOLD);
            if (B.copy_sprites) {
                for (int i = 0; i < 0x80; i++)
                    for (int j = 10; j <= 15; j++) B.mem[0x1800 + i * 0x10 + j] = B.mem[0x1800 + i * 0x10 + j - 6];
                B.copy_sprites = false;
            }
        }
        m6809d_run(&B.cpu, main_cycles);
        hd63701_run(&B.mcu, mcu_cycles);
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    B.in0 = B.in1 = B.in2 = 0x1f;
    if (in->coin1) B.in0 &= 0xfd;
    if (in->coin2) B.in0 &= 0xfb;
    if (p1 & AP_START) B.in0 &= 0xf7;
    if (p2 & AP_START) B.in0 &= 0xef;
    if (p1 & AP_LEFT) B.in1 &= 0xfe;
    if (p1 & AP_RIGHT) B.in1 &= 0xfd;
    if (p1 & AP_DOWN) B.in1 &= 0xfb;
    if (p1 & AP_UP) B.in1 &= 0xf7;
    if (p1 & AP_B1) B.in1 &= 0xef;
    if (p2 & AP_LEFT) B.in2 &= 0xfe;
    if (p2 & AP_RIGHT) B.in2 &= 0xfd;
    if (p2 & AP_DOWN) B.in2 &= 0xfb;
    if (p2 & AP_UP) B.in2 &= 0xf7;
    if (p2 & AP_B1) B.in2 &= 0xef;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw_a = v;
    else if (bank == 1) B.dsw_b = v;
    else if (bank == 2) B.dsw_c = v;
}
