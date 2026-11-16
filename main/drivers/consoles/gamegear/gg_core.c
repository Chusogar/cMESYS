// gg_core.c - Sega Game Gear (1990): a C11 migration of dsp-cpp's
// drivers/consoles/gamegear (from sega_gg.pas). The Master System's Z80,
// VDP (video/sega_vdp, with the Game Gear's 12-bit palette) and PSG; the
// LCD shows the VDP's 160x144 middle. No BIOS (the registers as it leaves
// them). The Sega mapper, the Codemasters one by CRC. The cartridges that
// run in the Master System's mode (by CRC, as dsp-cpp) get the whole
// 256x192 picture, scaled to the LCD as the Game Gear does (dsp-cpp shows
// the VDP's whole 284x243 frame).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../video/sega_vdp.h"
#include "../../../sound/sn76496.h"

#define CLOCK 3579545
#define LINE_CYCLES 228
#define LINES SVDP_LINES_NTSC
#define RATE 44100
#define GG_W 160
#define GG_H 144
#define CROP_X 61
#define CROP_Y 51
#define MAX_BANKS 64

static const char *const kExts[] = { ".gg", ".bin", NULL };
const cons_info cons_machine = {
    "gamegear", "Sega Game Gear", "Sega, 1990", kExts, "GG BIN", GG_W, GG_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "1", "2", NULL, NULL }, "START", NULL,
    CONS_NO_KEYS, 1, NULL,
};

static struct {
    zd_z80 z80;
    sega_vdp vdp;
    sn76496 psg;
    uint8_t *rom;
    int banks;
    bool codemasters, sms_video;
    uint8_t ram[0x2000];
    uint8_t slot2_ram[2][0x4000];
    uint8_t rom_bank[3];
    bool slot2_on;
    uint8_t slot2_bank;
    uint8_t io[8];
    uint8_t keys0, keys1;
    uint16_t fb[GG_W * GG_H];
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
    bool loaded;
} S;

static inline uint8_t bank_rd(int slot, uint16_t a) { return S.rom[(size_t)(S.rom_bank[slot] % S.banks) * 0x4000 + (a & 0x3fff)]; }

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x4000) return bank_rd(0, a);
    if (a < 0x8000) return bank_rd(1, a);
    if (a < 0xc000) {
        if (S.codemasters) {
            if (a >= 0xa000 && S.slot2_on) return S.slot2_ram[0][a & 0x1fff];
        } else if (S.slot2_on) {
            return S.slot2_ram[S.slot2_bank & 1][a & 0x3fff];
        }
        return bank_rd(2, a);
    }
    return S.ram[a & 0x1fff];
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (S.codemasters) {
        if (a == 0x0000) S.rom_bank[0] = (uint8_t)(v % S.banks);
        else if (a == 0x4000) {
            S.rom_bank[1] = (uint8_t)((v & 0x7f) % S.banks);
            S.slot2_on = (v & 0x80) != 0;
        } else if (a == 0x8000) S.rom_bank[2] = (uint8_t)(v % S.banks);
        else if (a >= 0xa000 && a < 0xc000 && S.slot2_on) S.slot2_ram[0][a & 0x1fff] = v;
        else if (a >= 0xc000) S.ram[a & 0x1fff] = v;
        return;
    }
    if (a < 0x8000) return;
    if (a < 0xc000) {
        if (S.slot2_on) S.slot2_ram[S.slot2_bank & 1][a & 0x3fff] = v;
        return;
    }
    S.ram[a & 0x1fff] = v;
    if (a >= 0xfffc) {
        const int r = a & 3;
        if (r == 0) {
            S.slot2_on = (v & 0x08) != 0;
            S.slot2_bank = (uint8_t)((v & 0x04) >> 2);
        } else {
            S.rom_bank[r - 1] = (uint8_t)(v % S.banks);
        }
    }
}

static uint8_t in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p == 0) return S.keys1 | 0x40;
    if (p <= 6) return S.io[p];
    if (p <= 0x3f || (p >= 0xc2 && p <= 0xdb) || p >= 0xde) return 0xff;
    if (p <= 0x7f) return (p & 1) ? S.vdp.hpos : S.vdp.linea_back;
    if (p <= 0xbf) return (p & 1) ? svdp_register_r(&S.vdp) : svdp_vram_r(&S.vdp);
    if (p == 0xc0 || p == 0xdc) return S.keys0;
    return 0xff;
}

static void out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p <= 6) S.io[p] = v;
    else if (p <= 0x3f) return;
    else if (p <= 0x7f) sn76496_write(&S.psg, v);
    else if (p <= 0xbf) {
        if (p & 1) svdp_register_w(&S.vdp, v);
        else svdp_vram_w(&S.vdp, v);
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    svdp_set_hpos(&S.vdp, c);                    // (as dsp-cpp)
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
    S.rom = (uint8_t *)malloc(0x4000);
    if (!S.rom) return false;
    memset(S.rom, 0xff, 0x4000);
    S.banks = 1;
    S.z80.read = rd;
    S.z80.write = wr;
    S.z80.in = in;
    S.z80.out = out;
    S.z80.cycles = cycles;
    S.vdp.irq = vdp_irq;
    svdp_init(&S.vdp);
    svdp_set_gg(&S.vdp, true);
    sn76496_init(&S.psg, CLOCK, RATE, 1.0f);
    zd_init(&S.z80);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    zd_reset(&S.z80);
    S.z80.sp = 0xdfeb;                           // (as the BIOS leaves them)
    S.z80.a = 0x14;
    S.z80.b = 0xff;
    S.z80.c = 0x3c;
    S.z80.h = 0x00;
    S.z80.l = 0x02;
    sn76496_reset(&S.psg);
    svdp_reset(&S.vdp);
    svdp_video_ntsc(&S.vdp, 0);
    svdp_set_gg(&S.vdp, !S.sms_video);
    memset(S.ram, 0, sizeof S.ram);
    memset(S.slot2_ram, 0, sizeof S.slot2_ram);
    memset(S.io, 0, sizeof S.io);
    S.keys0 = 0xff;
    S.keys1 = 0x80;
    S.rom_bank[0] = 0;
    S.rom_bank[1] = (uint8_t)(1 % S.banks);
    S.rom_bank[2] = (uint8_t)(2 % S.banks);
    S.slot2_on = false;
    S.slot2_bank = 0;
    S.audio_acc = 0;
    S.debt = 0;
    memset(S.fb, 0, sizeof S.fb);
}

static bool in_list(uint32_t crc, const uint32_t *l, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (l[i] == crc) return true;
    return false;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    size_t off = 0;
    if (data && size % 0x4000 == 512) {
        off = 512;
        size -= 512;
    }
    if (!data || !size) {
        if (why) *why = "empty file";
        free(data);
        return false;
    }
    if (size >= 0x4000 && size % 0x4000) {       // (down to whole 16 KB banks)
        off += size % 0x4000;
        size -= size % 0x4000;
    }
    const int banks = size < 0x4000 ? 1 : (int)(size / 0x4000);
    if (banks > MAX_BANKS) {
        if (why) *why = "ROM too large";
        free(data);
        return false;
    }
    uint8_t *rom = (uint8_t *)malloc((size_t)banks * 0x4000);
    if (!rom) {
        if (why) *why = "out of memory";
        free(data);
        return false;
    }
    memset(rom, 0xff, (size_t)banks * 0x4000);
    memcpy(rom, data + off, size);
    const uint32_t crc = cons_crc32(data + off, size);
    free(data);
    free(S.rom);
    S.rom = rom;
    S.banks = banks;
    static const uint32_t kCodemasters[] = {
        0x5e53c7f7, 0xdbe8895c, 0xf7c524f6, 0xc888222b, 0xaa140c9c, 0x8813514b, 0x9fa727a0,
        0xfb481971, 0xd9a7f170, 0x76c5bdfb, 0xc1756bee, 0x6caa625b, 0x152f0dcc, 0x72981057,
    };
    static const uint32_t kSmsVideo[] = {
        0xe5f789b9, 0x9942b69b, 0x5877b10d, 0x59840fd6, 0xaa140c9c, 0xc8381def, 0xc888222b, 0x76c5bdfb,
        0x1d93246e, 0xce97efe8, 0xa2f9c7af, 0x3382d73f, 0x01eab89d, 0xf037ec00, 0x2aa12d7e, 0x0189931e,
        0x86e5b455, 0x45f058d6, 0x311d2863, 0xba6344fc, 0x1c6c149c, 0x9c76fb3a, 0x56201996, 0x4902b7a2,
        0xfb481971, 0x9fa727a0, 0x10dbbef4, 0xbd1cc7df, 0x8230384e, 0xda8e95a9, 0x6f8e46cf, 0x7bb81e3d,
        0x44fbe8f6, 0x3b627808, 0x18086b70, 0x8813514b,
    };
    S.codemasters = in_list(crc, kCodemasters, sizeof kCodemasters / sizeof kCodemasters[0]);
    S.sms_video = in_list(crc, kSmsVideo, sizeof kSmsVideo / sizeof kSmsVideo[0]);
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    memset(S.rom, 0xff, (size_t)S.banks * 0x4000);
    S.loaded = S.codemasters = S.sms_video = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    for (int line = 0; line < S.vdp.y_total; line++) {
        const int want = LINE_CYCLES - S.debt;
        S.debt = want > 0 ? zd_run(&S.z80, want) - want : -want;
        svdp_refresh(&S.vdp, line);
        const uint16_t *src = S.vdp.line;
        if (S.sms_video) {
            // the 256x192 picture onto the LCD: 8 of every 5 pixels / 4 of every 3 lines
            if (line < 192 && line % 4 != 3) {
                uint16_t *d = S.fb + (line - line / 4) * GG_W;
                for (int x = 0; x < GG_W; x++) d[x] = src[SVDP_LEFT + x * 8 / 5];
            }
        } else if (line >= CROP_Y && line < CROP_Y + GG_H) {
            memcpy(S.fb + (line - CROP_Y) * GG_W, src + CROP_X, GG_W * sizeof(uint16_t));
        }
    }
    *audio = S.audio;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = GG_W;
    *h = GG_H;
    *stride = GG_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    if (player != 0) return;
    uint8_t k = 0xff;
    if (b & CP_UP) k &= 0xfe;
    if (b & CP_DOWN) k &= 0xfd;
    if (b & CP_LEFT) k &= 0xfb;
    if (b & CP_RIGHT) k &= 0xf7;
    if (b & CP_B1) k &= 0xef;
    if (b & CP_B2) k &= 0xdf;
    S.keys0 = k;
    S.keys1 = (b & (CP_START | CP_COIN)) ? (uint8_t)(S.keys1 & 0x7f) : (uint8_t)(S.keys1 | 0x80);
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return NULL; }
