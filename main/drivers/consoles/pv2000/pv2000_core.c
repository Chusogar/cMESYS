// pv2000_core.c - Casio PV-2000 (1983): a C11 migration of dsp-cpp's
// drivers/consoles/pv2000 (from pv2000.pas). Z80 at 3.58 MHz (cpu/z80dsp),
// TMS9918A on the NMI (video/tms9918), SN76489 (sound/sn76496), the 16 KB
// BIOS at $0000 (rom_data.c), 4 KB of RAM at $7000, the cartridge at $C000
// (16 KB) and the keyboard matrix (11 columns; the BIOS asks for an IRQ on
// a new key). The joystick shares the cursor keys; its two buttons are the
// matrix's Attack 0 / 1. No cassette.
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../video/tms9918.h"
#include "../../../sound/sn76496.h"
#include "rom_data.h"

#define CLOCK 3579545
#define LINE_CYCLES 228
#define LINES 262
#define RATE 44100
#define MAX_CART 0x4000

static const char *const kExts[] = { ".bin", ".rom", NULL };
const cons_info cons_machine = {
    "pv2000", "Casio PV-2000", "Casio, 1983", kExts, "BIN ROM", TMS_W, TMS_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "ATTACK 0", "ATTACK 1", NULL, NULL }, NULL, NULL,
    CONS_KEYBOARD, 1, "The keyboard: letters, digits, Enter, Backspace (DEL), Tab (HOME), Shift, Caps Lock (Hiragana)",
};

static struct {
    zd_z80 z80;
    tms9918 vdp;
    sn76496 psg;
    uint8_t *mem, *vram;
    uint16_t *fb;
    uint16_t pal[16];
    uint8_t keys[11];
    uint8_t keyb_column, last_key;
    bool last_nmi, has_bios, loaded;
    bool held[CK_COUNT];
    unsigned pad;
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
} S;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a == 0x4000) return tms_vram_read(&S.vdp);
    if (a == 0x4001) return tms_status_read(&S.vdp);
    if (a <= 0x3fff || (a >= 0x7000 && a <= 0x7fff) || a >= 0xc000) return S.mem[a];
    return 0;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a == 0x4000) { tms_vram_write(&S.vdp, v); return; }
    if (a == 0x4001) { tms_register_write(&S.vdp, v); return; }
    if (a >= 0x7000 && a <= 0x7fff) S.mem[a] = v;
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0x10: return S.keyb_column < 10 ? (uint8_t)(S.keys[S.keyb_column] >> 4) : 0;
    case 0x20: return S.keyb_column < 10 ? (uint8_t)(0xf0 | (S.keys[S.keyb_column] & 0x0f)) : 0xf0;
    case 0x40: return (uint8_t)(0xf0 | (S.keys[10] & 0x0f));
    default: return 0;
    }
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0x20:
        S.keyb_column = v;
        zd_set_irq(&S.z80, ZD_CLEAR, 0xff);
        break;
    case 0x40: sn76496_write(&S.psg, v); break;
    default: break;
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
    if (on && !S.last_nmi) zd_set_nmi(&S.z80, ZD_PULSE);
    S.last_nmi = on;
    // with column $0F selected the BIOS wants an IRQ on a new key
    if (S.keyb_column == 0x0f) {
        uint8_t k = 0;
        for (int i = 0; i < 9; i++) k |= S.keys[i];
        if (k && k != S.last_key) zd_set_irq(&S.z80, ZD_ASSERT, 0xff);
        S.last_key = k;
    }
}

bool cons_init(void)
{
    S.mem = (uint8_t *)calloc(1, 0x10000);
    S.vram = (uint8_t *)calloc(1, 0x4000);
    S.fb = (uint16_t *)calloc(TMS_W * TMS_H, sizeof(uint16_t));
    if (!S.mem || !S.vram || !S.fb) return false;
    pv2000_rom_load();                     // (the BIOS from storage when not built in: romfs)
    if (pv2000_rom_bios) {
        memcpy(S.mem, pv2000_rom_bios, 0x4000);
        S.has_bios = true;
    } else {
        // no BIOS: a HALT loop (a cartridge cannot run without it)
        S.mem[0] = 0xf3;
        S.mem[1] = 0x76;
    }
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
    S.last_nmi = false;
    S.keyb_column = 0;
    S.last_key = 0;
    memset(S.keys, 0, sizeof S.keys);
    S.audio_acc = 0;
    S.debt = 0;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name, (void)why;
    if (size > MAX_CART) size = MAX_CART;
    memset(S.mem + 0xc000, 0, 0x4000);
    memcpy(S.mem + 0xc000, data, size);
    free(data);
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    memset(S.mem + 0xc000, 0, 0x4000);
    S.loaded = false;
}

bool cons_loaded(void) { return S.loaded; }

static void matrix(void)
{
    memset(S.keys, 0, sizeof S.keys);
    const bool *k = S.held;
#define PRESS(c, bit, d) do { if (d) S.keys[c] |= (bit); } while (0)
    PRESS(0, 0x01, k[CK_4]); PRESS(0, 0x02, k[CK_3]); PRESS(0, 0x04, k[CK_2]); PRESS(0, 0x08, k[CK_1]);
    PRESS(0, 0x10, k[CK_8]); PRESS(0, 0x20, k[CK_7]); PRESS(0, 0x40, k[CK_6]); PRESS(0, 0x80, k[CK_5]);
    PRESS(1, 0x01, k[CK_R]); PRESS(1, 0x02, k[CK_E]); PRESS(1, 0x04, k[CK_W]); PRESS(1, 0x08, k[CK_Q]);
    PRESS(1, 0x10, k[CK_I]); PRESS(1, 0x20, k[CK_U]); PRESS(1, 0x40, k[CK_Y]); PRESS(1, 0x80, k[CK_T]);
    PRESS(2, 0x01, k[CK_F]); PRESS(2, 0x02, k[CK_D]); PRESS(2, 0x04, k[CK_S]); PRESS(2, 0x08, k[CK_A]);
    PRESS(2, 0x10, k[CK_K]); PRESS(2, 0x20, k[CK_J]); PRESS(2, 0x40, k[CK_H]); PRESS(2, 0x80, k[CK_G]);
    PRESS(3, 0x01, k[CK_C]); PRESS(3, 0x02, k[CK_X]); PRESS(3, 0x04, k[CK_Z]); PRESS(3, 0x08, k[CK_CAPSLOCK]);
    PRESS(3, 0x10, k[CK_SPACE]); PRESS(3, 0x20, k[CK_N]); PRESS(3, 0x40, k[CK_B]); PRESS(3, 0x80, k[CK_V]);
    PRESS(4, 0x08, k[CK_TAB]); PRESS(4, 0x10, k[CK_9]); PRESS(4, 0x20, k[CK_MINUS]); PRESS(4, 0x80, k[CK_0]);
    PRESS(5, 0x10, k[CK_O]); PRESS(5, 0x80, k[CK_P]);
    PRESS(6, 0x01, k[CK_DOWN] || (S.pad & CP_DOWN)); PRESS(6, 0x02, k[CK_RIGHT] || (S.pad & CP_RIGHT));
    PRESS(6, 0x10, k[CK_L]); PRESS(6, 0x20, k[CK_QUOTE]); PRESS(6, 0x80, k[CK_SEMICOLON]);
    PRESS(7, 0x01, k[CK_LEFT] || (S.pad & CP_LEFT)); PRESS(7, 0x02, k[CK_UP] || (S.pad & CP_UP));
    PRESS(7, 0x10, k[CK_M]); PRESS(7, 0x20, k[CK_PERIOD]); PRESS(7, 0x40, k[CK_SLASH]); PRESS(7, 0x80, k[CK_COMMA]);
    PRESS(8, 0x01, S.pad & CP_B1); PRESS(8, 0x02, S.pad & CP_B2); PRESS(8, 0x10, k[CK_ENTER]);
    PRESS(8, 0x40, k[CK_BACKSPACE]);
    PRESS(10, 0x04, k[CK_LSHIFT] || k[CK_RSHIFT]);
#undef PRESS
}

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    matrix();
    uint8_t pen[TMS_W];
    for (int line = 0; line < LINES; line++) {
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

void cons_pad(int player, unsigned bits) { if (player == 0) S.pad = bits; }
void cons_key(int key, bool down) { if (key >= 0 && key < CK_COUNT) S.held[key] = down; }
const char *cons_status(void) { return S.has_bios ? NULL : "no BIOS built in (roms/pv2000/hn613128pc64.bin)"; }
