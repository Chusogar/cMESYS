// sms_core.c - Sega Master System / Mark III (1985): a C11 migration of
// dsp-cpp's drivers/consoles/sms (from sms.pas). Z80 at 3.58 MHz
// (cpu/z80dsp), the 315-5124 VDP in Mode 4 (video/sega_vdp), the SN76489
// PSG (sound/sn76496) and the FM unit's YM2413 (sound/ym2413). The export
// NTSC model. The Sega mapper (and the Codemasters, Korean, 4-Pak,
// Cyborg Z and Nemesis ones by CRC, as dsp-cpp); a BIOS image loaded as a
// cartridge boots as the BIOS. PAUSE is the Z80's NMI.
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../video/sega_vdp.h"
#include "../../../sound/sn76496.h"
#include "../../../sound/ym2413.h"

#define CLOCK 3579545
#define LINE_CYCLES 228          // (dsp-cpp: 3579545 / 59.922743 / 262, truncated: 227)
#define LINES SVDP_LINES_NTSC
#define RATE 44100
#define FB_H 243
#define MAX_BANKS 64

enum { M_SEGA, M_CODEMASTERS, M_KOREAN, M_FOURPACK, M_CYBORGZ, M_NEMESIS };

static const char *const kExts[] = { ".sms", ".bin", ".rom", NULL };
const cons_info cons_machine = {
    "sms", "Sega Master System", "Sega, 1985", kExts, "SMS BIN", SVDP_W, FB_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "1", "2", NULL, NULL }, "PAUSE", NULL,
    CONS_NO_KEYS, 2, NULL,
};

static struct {
    zd_z80 z80;
    sega_vdp vdp;
    sn76496 psg;
    ym2413 opll;
    uint8_t *rom;                // banks x 16 KB
    int banks;
    bool has_bios;               // (the cartridge is a BIOS)
    int mapper;
    uint8_t ram[0x2000];
    uint8_t slot2_ram[2][0x4000];
    uint8_t rom_bank[4], bios_bank[4];
    uint8_t slot2_bank;
    bool bslot2_ram, bios_enabled, cart_enabled, io_enabled;
    uint8_t keys[2];
    bool push_pause;
    uint8_t old_3f, old_f2;
    uint16_t *fb;
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
    bool loaded;
} S;

static inline uint8_t bank_rd(int bank, uint16_t off) { return S.rom[(size_t)(bank % S.banks) * 0x4000 + (off & 0x3fff)]; }

static uint8_t rd_sega(uint16_t a)
{
    switch (a >> 14) {
    case 0:
        if (S.bios_enabled || S.cart_enabled) {
            if (a < 0x400) return S.rom[a];
            return bank_rd(S.bios_enabled ? S.bios_bank[0] : S.rom_bank[0], a);
        }
        return 0xff;
    case 1:
        return bank_rd(S.bios_enabled ? S.bios_bank[1] : S.rom_bank[1], a);
    case 2:
        if (S.bios_enabled) return bank_rd(S.bios_bank[2], a);
        if (S.bslot2_ram) return S.slot2_ram[S.slot2_bank & 1][a & 0x3fff];
        return bank_rd(S.rom_bank[2], a);
    default:
        return S.ram[a & 0x1fff];
    }
}

static void wr_sega(uint16_t a, uint8_t v)
{
    if (a < 0x8000) return;
    if (a < 0xc000) {
        if (S.bslot2_ram) S.slot2_ram[S.slot2_bank & 1][a & 0x3fff] = v;
        return;
    }
    S.ram[a & 0x1fff] = v;
    if (a < 0xfffc) return;
    const int r = a & 3;
    if (r == 0) {
        if (S.cart_enabled) {
            S.bslot2_ram = (v & 0x08) != 0;
            S.slot2_bank = (uint8_t)((v & 0x04) >> 2);
        }
        return;
    }
    if (S.cart_enabled) S.rom_bank[r - 1] = (uint8_t)(v % S.banks);
    if (S.bios_enabled) S.bios_bank[r - 1] = (uint8_t)(v % S.banks);
}

// Cyborg Z / Nemesis: 8 KB banks
static inline uint8_t half_rd(int slot, uint16_t a)
{
    const int bank = S.rom_bank[slot] % (S.banks * 2);
    return S.rom[(size_t)(bank >> 1) * 0x4000 + (a & 0x1fff) + 0x2000 * (bank & 1)];
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    switch (S.mapper) {
    case M_SEGA:
        return rd_sega(a);
    case M_CYBORGZ:
    case M_NEMESIS:
        switch (a >> 13) {
        case 0:
            if (S.mapper == M_NEMESIS) return S.rom[(size_t)(S.banks - 1) * 0x4000 + (a & 0x1fff) + 0x2000];
            return S.rom[a & 0x3fff];
        case 1:
            if (S.mapper == M_NEMESIS) return S.rom[(a & 0x1fff) + 0x2000];
            return S.rom[a & 0x3fff];
        case 2: return half_rd(0, a);
        case 3: return half_rd(1, a);
        case 4: return half_rd(2, a);
        case 5: return half_rd(3, a);
        default: return S.ram[a & 0x1fff];
        }
    default:
        if (a >= 0xc000) return S.ram[a & 0x1fff];
        return bank_rd(S.rom_bank[a >> 14], a);
    }
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    switch (S.mapper) {
    case M_SEGA:
        wr_sega(a, v);
        return;
    case M_CODEMASTERS:
        if (a == 0x0000) S.rom_bank[0] = (uint8_t)(v % S.banks);
        else if (a == 0x4000) S.rom_bank[1] = (uint8_t)(v % S.banks);
        else if (a == 0x8000) S.rom_bank[2] = (uint8_t)(v % S.banks);
        else if (a >= 0xc000) S.ram[a & 0x1fff] = v;
        return;
    case M_KOREAN:
        if (a == 0x4000) S.rom_bank[1] = (uint8_t)(v % S.banks);
        else if (a == 0xa000) S.rom_bank[2] = (uint8_t)(v % S.banks);
        else if (a >= 0xc000) S.ram[a & 0x1fff] = v;
        return;
    case M_FOURPACK:
        if (a == 0x3ffe) {
            S.rom_bank[0] = (uint8_t)(v % S.banks);
            S.rom_bank[2] = (uint8_t)(((v & 0x30) + S.rom_bank[2]) % S.banks);
        } else if (a == 0x7fff) {
            S.rom_bank[1] = (uint8_t)(v % S.banks);
        } else if (a == 0xbfff) {
            S.rom_bank[2] = (uint8_t)(((S.rom_bank[0] & 0x30) + v) % S.banks);
        } else if (a >= 0xc000) {
            S.ram[a & 0x1fff] = v;
        }
        return;
    default: {                   // Cyborg Z, Nemesis
        const int max = S.banks << 1;
        if (a <= 3) {
            static const uint8_t slot[4] = { 2, 3, 0, 1 };
            S.rom_bank[slot[a]] = (uint8_t)(v % max);
        } else if (a >= 0xc000) {
            S.ram[a & 0x1fff] = v;
        }
        return;
    }
    }
}

static void config_io(uint8_t v)
{
    if ((v & 5) == 5) {          // (the export model)
        S.keys[1] = (uint8_t)((S.keys[1] & 0x7f) | (v & 0x80));
        S.keys[1] = (uint8_t)((S.keys[1] & 0xbf) | ((v & 0x20) << 1));
    }
    if (!(S.old_3f & 0x02) && (v & 0x02)) svdp_set_hpos(&S.vdp, 0);
    S.old_3f = v;
}

static uint8_t in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    switch (p >> 6) {
    case 0: return 0xff;
    case 1: return (p & 1) ? S.vdp.hpos : S.vdp.linea_back;
    case 2: return (p & 1) ? svdp_register_r(&S.vdp) : svdp_vram_r(&S.vdp);
    default:
        if (S.io_enabled) return S.keys[p & 1];
        if (p == 0xf2) return S.old_f2;
        return 0xff;
    }
}

static void out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    switch (p >> 6) {
    case 0:
        if (p & 1) {
            config_io(v);
        } else {
            S.bios_enabled = !(v & 0x08) && S.has_bios;   // (dsp-cpp: with no BIOS it reads the empty one)
            S.io_enabled = !(v & 0x04);
            S.cart_enabled = (v & 0xe0) != 0xe0;
        }
        break;
    case 1:
        sn76496_write(&S.psg, v);
        break;
    case 2:
        if (p & 1) svdp_register_w(&S.vdp, v);
        else svdp_vram_w(&S.vdp, v);
        break;
    default:
        if (p == 0xf0) ym2413_address(&S.opll, v);
        else if (p == 0xf1) ym2413_write(&S.opll, v);
        else if (p == 0xf2) S.old_f2 = v;
        break;
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (uint64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        int32_t v = sn76496_update(&S.psg) + ym2413_update(&S.opll);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        if (S.naudio < 1024) S.audio[S.naudio++] = (int16_t)v;
    }
}

static void vdp_irq(void *u, bool on)
{
    (void)u;
    zd_set_irq(&S.z80, on ? ZD_HOLD : ZD_CLEAR, 0xff);
}

bool cons_init(void)
{
    S.fb = (uint16_t *)calloc(SVDP_W * FB_H, sizeof(uint16_t));
    S.rom = (uint8_t *)calloc(1, 0x4000);
    if (!S.fb || !S.rom) return false;
    S.banks = 1;
    S.z80.read = rd;
    S.z80.write = wr;
    S.z80.in = in;
    S.z80.out = out;
    S.z80.cycles = cycles;
    S.vdp.irq = vdp_irq;
    svdp_init(&S.vdp);
    sn76496_init(&S.psg, CLOCK, RATE, 1.0f);
    ym2413_init(&S.opll, 1.0f);
    zd_init(&S.z80);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    S.keys[0] = S.keys[1] = 0xff;
    S.push_pause = false;
    S.old_3f = S.old_f2 = 0;
    S.io_enabled = true;
    S.bios_enabled = S.has_bios;
    S.cart_enabled = S.loaded && !S.bios_enabled;
    S.rom_bank[0] = S.bios_bank[0] = 0;
    S.rom_bank[1] = S.bios_bank[1] = (uint8_t)(S.banks > 1 ? 1 : 0);
    S.rom_bank[2] = S.bios_bank[2] = (uint8_t)(S.banks > 1 ? 2 % S.banks : 0);
    S.rom_bank[3] = S.bios_bank[3] = 0;
    S.slot2_bank = 0;
    S.bslot2_ram = false;
    memset(S.ram, 0xf0, sizeof S.ram);           // (Alibaba and others want $F0)
    memset(S.slot2_ram, 0, sizeof S.slot2_ram);
    S.audio_acc = 0;
    S.debt = 0;
    svdp_reset(&S.vdp);
    sn76496_reset(&S.psg);
    ym2413_reset(&S.opll);
    zd_reset(&S.z80);
}

static int detect_mapper(uint32_t crc)
{
    switch (crc) {
    case 0x58fa27c6: case 0xa577ce46: case 0x29822980: case 0xea5c3a6f: case 0x8813514b: case 0xb9664ae1:
        return M_CODEMASTERS;
    case 0x565c799f: case 0xdbbf4dd1: case 0x18fb98a3: case 0x97d03541: case 0x89b79e77: case 0x060d6a7c:
        return M_KOREAN;
    case 0xa67f2a5c:
        return M_FOURPACK;
    case 0x0a77fa5e: case 0xa05258f5: case 0x9195c34c: case 0x83f0eede: case 0x5ac99fc4:
    case 0x445525e2: case 0xf89af3cc: case 0x77efe84a: case 0x06965ed9:
        return M_CYBORGZ;
    case 0xe316c06d:
        return M_NEMESIS;
    default:
        return M_SEGA;
    }
}

static bool is_bios(uint32_t crc)
{
    static const uint32_t k[] = { 0x81c3476b, 0xcf4a09ea, 0x9c5bad91, 0x8edf7ac6, 0x91e93385, 0xe79bb689 };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (crc == k[i]) return true;
    return false;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    size_t off = 0;
    if (data && size % 0x4000 == 512) {          // a copier's header
        off = 512;
        size -= 512;
    }
    if (!data || !size) {
        if (why) *why = "empty file";
        free(data);
        return false;
    }
    int banks = size < 0x4000 ? 1 : (int)(size / 0x4000);
    if (banks > MAX_BANKS) {
        if (why) *why = "cartridge larger than 1 MB";
        free(data);
        return false;
    }
    uint8_t *rom = (uint8_t *)calloc((size_t)banks, 0x4000);
    if (!rom) {
        if (why) *why = "out of memory";
        free(data);
        return false;
    }
    memcpy(rom, data + off, size < (size_t)banks * 0x4000 ? size : (size_t)banks * 0x4000);
    const uint32_t crc = cons_crc32(data + off, size);
    free(data);
    free(S.rom);
    S.rom = rom;
    S.banks = banks;
    S.mapper = detect_mapper(crc);
    S.has_bios = is_bios(crc);
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    memset(S.rom, 0, (size_t)S.banks * 0x4000);
    S.loaded = S.has_bios = false;
    S.mapper = M_SEGA;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    for (int line = 0; line < LINES; line++) {
        const int want = LINE_CYCLES - S.debt;
        S.debt = want > 0 ? zd_run(&S.z80, want) - want : -want;
        svdp_refresh(&S.vdp, line);
        // (dsp-cpp's mapping of the VDP's lines into the picture)
        const int top = S.vdp.top_border, yp = S.vdp.y_pixels, vis = S.vdp.visible_y_total;
        int y = -1;
        if (line < yp) {
            y = line + top;
        } else if (line < yp + (vis - top - yp)) {
            y = line + top;
        } else {
            const int upper = line - (LINES - top);
            if (upper >= 0 && upper < top) y = upper;
        }
        if (y >= 0 && y < FB_H) memcpy(S.fb + y * SVDP_W, S.vdp.line, SVDP_W * sizeof(uint16_t));
    }
    *audio = S.audio;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = SVDP_W;
    *h = FB_H;
    *stride = SVDP_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    static unsigned pads[2];
    pads[player & 1] = b;
    const unsigned p1 = pads[0], p2 = pads[1];
    uint8_t k0 = 0xff, k1 = (uint8_t)(S.keys[1] | 0x0f);
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
    if (player == 0) {                           // PAUSE: the NMI on the press
        if (p1 & (CP_START | CP_COIN)) {
            if (!S.push_pause) zd_set_nmi(&S.z80, ZD_PULSE);
            S.push_pause = true;
        } else {
            S.push_pause = false;
        }
    }
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return S.has_bios ? "BIOS" : NULL; }
