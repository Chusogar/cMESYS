// sg1000_core.c - Sega SG-1000 (1983): a C11 migration of dsp-cpp's
// drivers/consoles/sg1000 (from sg1000.pas). Z80 at 3.58 MHz (cpu/z80dsp),
// TMS9918A (video/tms9918), SN76489 (sound/sn76496); no BIOS: the cartridge
// at $0000 (up to 48 KB), 8 KB of RAM mirrored over $C000-$FFFF, the VDP on
// the Z80's /INT, PAUSE on its NMI. Some cartridges carried extra RAM at
// $2000 or $8000 (by CRC, as dsp-cpp).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../video/tms9918.h"
#include "../../../sound/sn76496.h"

#define CLOCK 3579545
#define LINE_CYCLES 228
#define LINES 262
#define RATE 44100
#define MAX_CART 0xc000

static const char *const kExts[] = { ".sg", ".sc", ".bin", NULL };
const cons_info cons_machine = {
    "sg1000", "Sega SG-1000", "Sega, 1983", kExts, "SG SC BIN", TMS_W, TMS_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "1", "2", NULL, NULL }, "PAUSE", NULL,
    CONS_NO_KEYS, 2, NULL,
};

static struct {
    zd_z80 z80;
    tms9918 vdp;
    sn76496 psg;
    uint8_t *mem;               // 64 KB: the cartridge, then the RAM at $C000
    uint8_t *vram;
    uint16_t *fb;
    uint16_t pal[16];
    bool ram_8k, mid_8k_ram;
    uint8_t keys[2];
    bool push_pause;
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
    uint8_t *cart;
    bool loaded;
} S;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff) return S.mem[a];
    return S.mem[0xc000 + (a & 0x1fff)];
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff || (a >= 0x4000 && a <= 0x7fff) || (a >= 0xa000 && a <= 0xbfff)) return;
    if (a <= 0x3fff) { if (S.ram_8k) S.mem[a] = v; return; }
    if (a <= 0x9fff) { if (S.mid_8k_ram) S.mem[a] = v; return; }
    S.mem[0xc000 + (a & 0x1fff)] = v;
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    p &= 0xff;
    if (p >= 0x80 && p <= 0xbf) return (p & 1) ? tms_status_read(&S.vdp) : tms_vram_read(&S.vdp);
    if (p >= 0xc0) return (p & 1) ? S.keys[1] : S.keys[0];
    return 0xff;
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    p &= 0xff;
    if (p >= 0x40 && p <= 0x7f) sn76496_write(&S.psg, v);
    else if (p >= 0x80 && p <= 0xbf) {
        if (p & 1) tms_register_write(&S.vdp, v);
        else tms_vram_write(&S.vdp, v);
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (uint64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        int32_t v = sn76496_update(&S.psg);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        if (S.naudio < 1024) S.audio[S.naudio++] = (int16_t)v;
    }
}

static void vdp_irq(void *u, bool on)
{
    (void)u;
    zd_set_irq(&S.z80, on ? ZD_ASSERT : ZD_CLEAR, 0xff);
}

bool cons_init(void)
{
    S.mem = (uint8_t *)calloc(1, 0x10000);
    S.vram = (uint8_t *)calloc(1, 0x4000);
    S.fb = (uint16_t *)calloc(TMS_W * TMS_H, sizeof(uint16_t));
    if (!S.mem || !S.vram || !S.fb) return false;
    for (int i = 0; i < 16; i++) S.pal[i] = cons_rgb565(tms_palette[i]);
    S.z80.read = rd;
    S.z80.write = wr;
    S.z80.in = in;
    S.z80.out = out;
    S.z80.cycles = cycles;
    S.vdp.vram = S.vram;
    S.vdp.irq = vdp_irq;
    sn76496_init(&S.psg, CLOCK, RATE, 1.0f);
    zd_init(&S.z80);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    zd_reset(&S.z80);
    sn76496_reset(&S.psg);
    tms_reset(&S.vdp);
    S.keys[0] = S.keys[1] = 0xff;
    S.push_pause = false;
    S.audio_acc = 0;
    S.debt = 0;
}

static bool extra_ram_2000(uint32_t crc)
{
    switch (crc) {          // BomberMan Special, King's Valley, Knightmare, Kage, Rally X, Road Fighter...
    case 0x69fc1494: case 0xce5648c3: case 0x223397a1: case 0x281d2888: case 0x2e7166d5:
    case 0x306d5f78: case 0x29e047cc: case 0x5cbd1163: case 0xc550b4f0: case 0xfc87463c:
        return true;
    default:
        return false;
    }
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || !size) {
        if (why) *why = "empty file";
        free(data);
        return false;
    }
    if (size > MAX_CART) size = MAX_CART;
    S.ram_8k = S.mid_8k_ram = false;
    memset(S.mem, 0, 0x10000);
    memcpy(S.mem, data, size);
    cons_reset();
    const uint32_t crc = cons_crc32(data, size);
    if (extra_ram_2000(crc)) S.ram_8k = true;
    if (crc == 0x092f29d6 || crc == 0xaf4f14bc || crc == 0x1d1a0ca3) S.mid_8k_ram = true;   // Castle, Othello
    if (crc == 0x49e9718b) memcpy(S.mem + 0x4000, S.mem, 0x4000);   // Safari Hunting's protection mirror
    free(data);
    S.loaded = true;
    return true;
}

void cons_eject(void)
{
    memset(S.mem, 0, 0xc000);
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    uint8_t pen[TMS_W];
    for (int line = 0; line < LINES; line++) {
        // (the cycles a line overran are taken off the next one: dsp-cpp
        // runs each line from scratch, a few % fast, which the sound shows)
        const int want = LINE_CYCLES - S.debt;
        S.debt = want > 0 ? zd_run(&S.z80, want) - want : -want;
        if (tms_line(&S.vdp, line, pen) && line < TMS_H) {
            uint16_t *row = S.fb + line * TMS_W;
            for (int x = 0; x < TMS_W; x++) row[x] = S.pal[pen[x]];
        }
    }
    *audio = S.audio;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = TMS_W;
    *h = TMS_H;
    *stride = TMS_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    static unsigned pads[2];
    pads[player & 1] = b;
    const unsigned p1 = pads[0], p2 = pads[1];
    uint8_t k0 = 0xff, k1 = 0xff;
    if (p1 & CP_UP) k0 &= 0xfe;
    if (p1 & CP_DOWN) k0 &= 0xfd;
    if (p1 & CP_LEFT) k0 &= 0xfb;
    if (p1 & CP_RIGHT) k0 &= 0xf7;
    if (p1 & CP_B1) k0 &= 0xef;
    if (p1 & CP_B2) k0 &= 0xdf;
    if (p2 & CP_UP) k0 &= 0xbf;
    if (p2 & CP_DOWN) k0 &= 0x7f;
    if (p2 & CP_LEFT) k1 &= 0xfe;
    if (p2 & CP_RIGHT) k1 &= 0xfd;
    if (p2 & CP_B1) k1 &= 0xfb;
    if (p2 & CP_B2) k1 &= 0xf7;
    S.keys[0] = k0;
    S.keys[1] = k1;
    // PAUSE: the NMI (player 1's START or COIN stands in for it)
    if (player == 0) {
        if (p1 & (CP_START | CP_COIN)) {
            S.push_pause = true;
        } else {
            if (S.push_pause) zd_set_nmi(&S.z80, ZD_PULSE);
            S.push_pause = false;
        }
    }
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return NULL; }
