// pv1000_core.c - Casio PV-1000 (1983): a C11 migration of dsp-cpp's
// drivers/consoles/pv1000. Z80 at 3.58 MHz (cpu/z80dsp), a tile display of
// 28x24 tiles of 8x8 pixels in 8 colours (patterns in the cartridge or in
// 1 KB of RAM at $BC00; the map at $B800) with a border, three square-wave
// voices, two joysticks; the cartridge at $0000 (up to 32 KB, mirrored), no
// BIOS. 224x244 (26 lines of border above and below the 192).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#define W 224
#define H 244
#define ACTIVE_H 192
#define BORDER_TOP 26
#define CLOCK (17897725u / 5u)
#define LINES 262
#define FPS 59.92274
#define LINE_CYCLES ((int)((double)CLOCK / (FPS * LINES) + 0.5))
#define RATE 44100

static const char *const kExts[] = { ".bin", ".rom", NULL };
const cons_info cons_machine = {
    "pv1000", "Casio PV-1000", "Casio, 1983", kExts, "BIN ROM", W, H, FPS, RATE,
    { "BUTTON 1", "BUTTON 2", NULL, NULL }, "START", NULL, CONS_NO_KEYS, 2, NULL,
};

static const uint32_t kPalette[8] = { 0x000000, 0x0000ff, 0x00ff00, 0x00ffff, 0xff0000, 0xff00ff, 0xffff00, 0xffffff };

typedef struct { uint32_t count; uint16_t period; uint8_t val; } voice;

static struct {
    zd_z80 cpu;
    uint8_t rom[0x8000];
    uint8_t tile_map[0x400], pattern_ram[0x400];
    bool dirty[0x400];
    uint8_t io_ram[8];
    bool force_pattern, fd_buffer_flag, loaded;
    uint8_t fd_data, pcg_bank, border_col;
    uint8_t keys[4];
    unsigned pads[2];
    voice v[3];
    uint8_t sound_control;
    int16_t sound_out;
    uint32_t sound_phase;           // (5 / 1024 ticks a CPU cycle, in 1024ths)
    uint64_t audio_acc;
    int debt;
    uint16_t *fb;
    uint16_t pal[8];
    int16_t audio[1024];
    int naudio;
} S;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return S.rom[a & 0x7fff];
    if (a >= 0xb800 && a < 0xbc00) return S.tile_map[a & 0x3ff];
    if (a >= 0xbc00 && a <= 0xbfff) return S.pattern_ram[a & 0x3ff];
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xb800 && a < 0xbc00) {
        const uint16_t i = a & 0x3ff;
        if (S.tile_map[i] != v) {
            S.tile_map[i] = v;
            S.dirty[i] = true;
        }
        return;
    }
    if (a >= 0xbc00 && a <= 0xbfff) {
        const uint16_t i = a & 0x3ff;
        if (S.pattern_ram[i] != v) {
            S.pattern_ram[i] = v;
            memset(S.dirty, 1, sizeof S.dirty);         // (any pattern: the whole map)
        }
    }
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfe: case 0xff:
        return S.io_ram[p & 7];
    case 0xfc: {
        uint8_t t = S.fd_buffer_flag ? 1 : 0;
        if (S.fd_data) t |= 2;
        S.fd_buffer_flag = false;
        return t;
    }
    case 0xfd: {
        uint8_t t = 0;
        for (int f = 0; f < 4; f++)
            if (S.io_ram[5] & (1 << f)) {
                t |= S.keys[f];
                S.fd_data = (uint8_t)(S.fd_data & ~(1 << f));
            }
        return t;
    }
    default:
        return 0xff;
    }
}

static void paint_border(void)
{
    const uint16_t c = S.pal[S.border_col & 7];
    for (int y = 0; y < BORDER_TOP; y++)
        for (int x = 0; x < W; x++) S.fb[y * W + x] = c;
    for (int y = BORDER_TOP + ACTIVE_H; y < H; y++)
        for (int x = 0; x < W; x++) S.fb[y * W + x] = c;
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0xf8: case 0xf9: case 0xfa: case 0xfb: {
        S.io_ram[p & 7] = v;
        const int ch = p & 3;
        if (ch <= 2) {
            const uint16_t per = (uint16_t)((~v) & 0x3f);
            if (per == 0 && S.v[ch].period != 0) S.v[ch].val = (uint8_t)~S.v[ch].val;
            S.v[ch].period = per;
        } else {
            S.sound_control = v;
        }
        break;
    }
    case 0xfc: case 0xfe:
        S.io_ram[p & 7] = v;
        break;
    case 0xfd:
        S.io_ram[p & 7] = v;
        S.fd_data = 0x0f;
        break;
    case 0xff:
        S.io_ram[p & 7] = v;
        S.pcg_bank = (uint8_t)((v & 0x20) >> 5);
        S.force_pattern = (v & 0x10) != 0;
        if (S.border_col != (v & 7)) {
            S.border_col = v & 7;
            paint_border();
        }
        break;
    default:
        break;
    }
}

static void sound_tick(void)
{
    static const int vol[3] = { 0x10, 0x18, 0x20 };
    for (int f = 0; f < 3; f++) {
        S.v[f].count++;
        if (S.v[f].period > 0 && S.v[f].count >= S.v[f].period) {
            S.v[f].count = 0;
            S.v[f].val = (uint8_t)~S.v[f].val;
        }
    }
    if (!(S.sound_control & 2)) {
        S.sound_out = 0;
        return;
    }
    int sum = (S.v[0].val & 1) * vol[0] * 256 + (S.v[1].val & 1) * vol[1] * 256 + (S.v[2].val & 1) * vol[2] * 256;
    sum -= (vol[0] + vol[1] + vol[2]) * 128;
    if (sum > 32767) sum = 32767;
    if (sum < -32768) sum = -32768;
    S.sound_out = (int16_t)sum;
}

static void cycles(void *u, int c)
{
    (void)u;
    S.sound_phase += (uint32_t)c * 5;
    while (S.sound_phase >= 1024) {
        S.sound_phase -= 1024;
        sound_tick();
    }
    S.audio_acc += (uint64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        if (S.naudio < 1024) S.audio[S.naudio++] = S.sound_out;
    }
}

static uint8_t fetch(uint16_t a)
{
    if (a >= 0xbc00 && a <= 0xbfff) return S.pattern_ram[a & 0x3ff];
    if (a < 0x8000) return S.rom[a & 0x7fff];
    return 0;
}

static void draw_tile(int sx, int sy, uint16_t base)
{
    for (int y = 0; y < 8; y++) {
        const uint8_t p3 = fetch((uint16_t)(base + y)), p2 = fetch((uint16_t)(base + 8 + y)),
                      p1 = fetch((uint16_t)(base + 16 + y));
        const int py = BORDER_TOP + sy * 8 + y;
        for (int x = 7; x >= 0; x--) {
            const int idx = ((p1 >> x) & 1) | (((p2 >> x) & 1) << 1) | (((p3 >> x) & 1) << 2);
            const int px = (sx - 2) * 8 + (7 - x);
            if (px < 0 || px >= W) continue;
            S.fb[py * W + px] = S.pal[idx & 7];
        }
    }
}

static void update_video(void)
{
    for (int sy = 0; sy < 24; sy++)
        for (int sx = 2; sx <= 29; sx++) {
            const uint16_t a = (uint16_t)(sy * 32 + sx);
            if (!S.dirty[a]) continue;
            uint16_t tile = S.tile_map[a], pos;
            if (tile < 0xe0 || S.force_pattern) {
                tile = (uint16_t)(tile | (S.pcg_bank << 8));
                pos = (uint16_t)(tile * 32 + 8);
            } else {
                tile = (uint16_t)(tile - 0xe0);
                pos = (uint16_t)(0xbc00 + tile * 32 + 8);
            }
            draw_tile(sx, sy, pos);
            S.dirty[a] = false;
        }
}

bool cons_init(void)
{
    S.fb = (uint16_t *)calloc(W * H, sizeof(uint16_t));
    if (!S.fb) return false;
    for (int i = 0; i < 8; i++) S.pal[i] = cons_rgb565(kPalette[i]);
    memset(S.rom, 0xff, sizeof S.rom);
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.in = in;
    S.cpu.out = out;
    S.cpu.cycles = cycles;
    zd_init(&S.cpu);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    zd_reset(&S.cpu);
    memset(S.tile_map, 0, sizeof S.tile_map);
    memset(S.pattern_ram, 0, sizeof S.pattern_ram);
    memset(S.dirty, 1, sizeof S.dirty);
    memset(S.io_ram, 0, sizeof S.io_ram);
    memset(S.keys, 0, sizeof S.keys);
    S.force_pattern = S.fd_buffer_flag = false;
    S.fd_data = S.pcg_bank = S.border_col = 0;
    for (int i = 0; i < 3; i++) {
        S.v[i].count = 0;
        S.v[i].period = 0;
        S.v[i].val = 1;
    }
    S.sound_control = 0;
    S.sound_out = 0;
    S.sound_phase = 0;
    S.audio_acc = 0;
    S.debt = 0;
    for (int i = 0; i < W * H; i++) S.fb[i] = S.pal[0];
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name, (void)why;
    memset(S.rom, 0xff, sizeof S.rom);
    const size_t n = size < sizeof S.rom ? size : sizeof S.rom;
    memcpy(S.rom, data, n);
    if (n > 0 && n < sizeof S.rom)                  // small cartridges mirrored over the 32 KB
        for (size_t i = n; i < sizeof S.rom; i++) S.rom[i] = S.rom[i % n];
    free(data);
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    memset(S.rom, 0xff, sizeof S.rom);
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    const unsigned p1 = S.pads[0], p2 = S.pads[1];
    S.keys[0] = (uint8_t)(((p1 & CP_COIN) ? 1 : 0) | ((p1 & CP_START) ? 2 : 0) | ((p2 & CP_COIN) ? 4 : 0) |
                          ((p2 & CP_START) ? 8 : 0));
    S.keys[1] = (uint8_t)(((p1 & CP_DOWN) ? 1 : 0) | ((p1 & CP_RIGHT) ? 2 : 0) | ((p2 & CP_DOWN) ? 4 : 0) |
                          ((p2 & CP_RIGHT) ? 8 : 0));
    S.keys[2] = (uint8_t)(((p1 & CP_LEFT) ? 1 : 0) | ((p1 & CP_UP) ? 2 : 0) | ((p2 & CP_LEFT) ? 4 : 0) |
                          ((p2 & CP_UP) ? 8 : 0));
    S.keys[3] = (uint8_t)(((p1 & CP_B1) ? 1 : 0) | ((p1 & CP_B2) ? 2 : 0) | ((p2 & CP_B1) ? 4 : 0) |
                          ((p2 & CP_B2) ? 8 : 0));
    for (int f = 0; f < LINES; f++) {
        if (f == 20) {
            S.fd_buffer_flag = true;
            zd_set_irq(&S.cpu, ZD_ASSERT, 0xff);
        }
        switch (f) {
        case 221: case 225: case 229: case 233: case 239: case 243: case 247: case 251: case 253: case 259:
        case 1: case 5: case 9: case 13: case 17: case 21:
            zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
            break;
        default: break;
        }
        if (f == 220) {
            update_video();
            zd_set_irq(&S.cpu, ZD_ASSERT, 0xff);
        }
        switch (f) {
        case 224: case 228: case 232: case 238: case 242: case 246: case 250: case 252: case 258:
        case 0: case 4: case 8: case 12: case 16:
            zd_set_irq(&S.cpu, ZD_ASSERT, 0xff);
            break;
        default: break;
        }
        const int want = LINE_CYCLES - S.debt;
        S.debt = want > 0 ? zd_run(&S.cpu, want) - want : -want;
    }
    *audio = S.audio;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = W;
    *h = H;
    *stride = W;
    return S.fb;
}

void cons_pad(int player, unsigned bits) { S.pads[player & 1] = bits; }
void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return NULL; }
