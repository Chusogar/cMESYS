// coleco_core.c - ColecoVision (1982): a C11 migration of dsp-cpp's
// drivers/consoles/colecovision (from coleco.pas). Z80 at 3.58 MHz
// (cpu/z80dsp), TMS9918A on the NMI (video/tms9918), SN76489 (sound/sn76496),
// the 8 KB BIOS (rom_data.c), 1 KB of RAM mirrored over $6000-$7FFF; the
// Super Game Module (AY-3-8910, 24 KB more RAM, the BIOS paged out), MegaCart
// bank switching (up to 512 KB) and the 24C08 / 24C256 EEPROM of Black Onix
// and Boxxle (not kept between runs here). Two controllers (joystick, two
// fire buttons) and player 1's keypad on the keys 0-9, Q (*) and W (#).
//
// Without the BIOS built in, a small stand-in jumps to the cartridge's start
// and its interrupt vectors (homebrew that does not call the BIOS runs; most
// commercial games need the real one).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../video/tms9918.h"
#include "../../../sound/sn76496.h"
#include "../../../sound/ay8910.h"
#include "../../../machine/i2cmem.h"
#include "rom_data.h"

#define CLOCK 3579545
#define LINE_CYCLES 228
#define LINES 262
#define RATE 44100
#define MAX_CART 0x80000

static const char *const kExts[] = { ".col", ".rom", ".bin", NULL };
const cons_info cons_machine = {
    "coleco", "ColecoVision", "Coleco, 1982", kExts, "COL ROM BIN", TMS_W, TMS_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "LEFT FIRE", "RIGHT FIRE", NULL, NULL }, NULL, NULL,
    CONS_KEYPAD, 2, "Keypad: 0-9, Q = *, W = #",
};

static struct {
    zd_z80 z80;
    tms9918 vdp;
    sn76496 psg;
    ay8910 ay;
    uint8_t bios[0x2000];
    bool real_bios;
    uint8_t *mem;               // the Z80's 64 KB ($2000 up)
    uint8_t *vram;
    uint16_t *fb;
    uint16_t pal[16];
    uint8_t *cart;              // the cartridge as loaded
    size_t cart_size;
    int mega_size;              // banks - 1
    bool mega;
    bool joymode, rom_enabled, sgm_ram, last_nmi;
    uint8_t joystick[2];
    uint16_t keypad[2];
    unsigned pads[2];
    bool keys[CK_COUNT];
    int eeprom;                 // 0 none, 8: 24C08, 256: 24C256
    i2cmem i2c;
    uint8_t *eeprom_mem;
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
} S;

static const uint8_t *bank(int n) { return S.cart + (size_t)n * 0x4000; }

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff) return S.rom_enabled ? S.bios[a] : S.mem[a];
    if (a <= 0x5fff) return S.sgm_ram ? S.mem[a] : 0xff;
    if (a <= 0x7fff) return S.sgm_ram ? S.mem[a] : S.mem[0x6000 + (a & 0x3ff)];
    if (a == 0xff80) return S.eeprom ? (uint8_t)(i2cmem_read_sda(&S.i2c) ? 1 : 0) : S.mem[a];
    if (a >= 0xffc0) {
        if (S.mega) {
            const int b = S.mega_size - ((0xffff - a) & S.mega_size);
            memcpy(S.mem + 0xc000, bank(b), 0x4000);
        }
        return S.mem[a];
    }
    return S.mem[a];
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) { if (!S.rom_enabled) S.mem[a] = v; return; }
    if (a <= 0x5fff) { if (S.sgm_ram) S.mem[a] = v; return; }
    if (a <= 0x7fff) {
        if (S.sgm_ram) S.mem[a] = v;
        else S.mem[0x6000 + (a & 0x3ff)] = v;
        return;
    }
    if (S.eeprom) {
        switch (a) {
        case 0xff90: case 0xffa0: case 0xffb0:
            memcpy(S.mem + 0xc000, bank(((a >> 4) & 3) & S.mega_size), 0x4000);
            return;
        case 0xffc0: i2cmem_write_scl(&S.i2c, false); return;
        case 0xffd0: i2cmem_write_scl(&S.i2c, true); return;
        case 0xffe0: i2cmem_write_sda(&S.i2c, false); return;
        case 0xfff0: i2cmem_write_sda(&S.i2c, true); return;
        default: break;
        }
    }
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    p &= 0xff;
    switch (p & 0xe0) {
    case 0x40:
        if (p == 0x52) return ay8910_data_r(&S.ay);
        break;
    case 0xa0:
        return (p & 1) ? tms_status_read(&S.vdp) : tms_vram_read(&S.vdp);
    case 0xe0: {
        const int pl = (p >> 1) & 1;
        if (S.joymode) return (uint8_t)(S.joystick[pl] & 0x7f);
        uint8_t data = 0x0f;
        const uint16_t k = S.keypad[pl];
        if (!(k & 0x0001)) data &= 0x0a;
        if (!(k & 0x0002)) data &= 0x0d;
        if (!(k & 0x0004)) data &= 0x07;
        if (!(k & 0x0008)) data &= 0x0c;
        if (!(k & 0x0010)) data &= 0x02;
        if (!(k & 0x0020)) data &= 0x03;
        if (!(k & 0x0040)) data &= 0x0e;
        if (!(k & 0x0080)) data &= 0x05;
        if (!(k & 0x0100)) data &= 0x01;
        if (!(k & 0x0200)) data &= 0x0b;
        if (!(k & 0x0400)) data &= 0x06;
        if (!(k & 0x0800)) data &= 0x09;
        return (uint8_t)(((k & 0x4000) >> 8) | 0x30 | data);
    }
    default: break;
    }
    return 0xff;
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    p &= 0xff;
    switch (p & 0xe0) {
    case 0x40:
        if (p == 0x50) ay8910_address_w(&S.ay, (uint8_t)(v & 0x0f));
        else if (p == 0x51) ay8910_data_w(&S.ay, v);
        else if (p == 0x53) S.sgm_ram = (v & 1) != 0;
        break;
    case 0x60: S.rom_enabled = (v & 2) != 0; break;
    case 0x80: case 0xc0: S.joymode = (p & 0x40) != 0; break;
    case 0xa0:
        if (p & 1) tms_register_write(&S.vdp, v);
        else tms_vram_write(&S.vdp, v);
        break;
    case 0xe0: sn76496_write(&S.psg, v); break;
    default: break;
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (uint64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        int32_t v = sn76496_update(&S.psg) + ay8910_render(&S.ay);
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
}

// The stand-in BIOS: start at the cartridge's address ($800A), RST 08-38 and
// the NMI through the cartridge's jump table ($800C-$8023)
static void stand_in_bios(void)
{
    static const uint8_t boot[] = { 0xf3, 0x31, 0xb9, 0x73, 0x2a, 0x0a, 0x80, 0xe9 };   // di; ld sp,73B9h; ld hl,(800Ah); jp (hl)
    memset(S.bios, 0xff, sizeof S.bios);
    memcpy(S.bios, boot, sizeof boot);
    for (int i = 1; i <= 7; i++) {                  // RST 08h..38h: jp 800Ch + 3 * (i - 1)
        const int t = 0x800c + 3 * (i - 1);
        S.bios[i * 8] = 0xc3;
        S.bios[i * 8 + 1] = (uint8_t)t;
        S.bios[i * 8 + 2] = (uint8_t)(t >> 8);
    }
    S.bios[0x66] = 0xc3;                            // NMI: jp 8021h
    S.bios[0x67] = 0x21;
    S.bios[0x68] = 0x80;
}

bool cons_init(void)
{
    S.mem = (uint8_t *)calloc(1, 0x10000);
    S.vram = (uint8_t *)calloc(1, 0x4000);
    S.fb = (uint16_t *)calloc(TMS_W * TMS_H, sizeof(uint16_t));
    if (!S.mem || !S.vram || !S.fb) return false;
    coleco_rom_load();                     // (the BIOS from storage when not built in: romfs)
    if (coleco_rom_bios) {
        memcpy(S.bios, coleco_rom_bios, sizeof S.bios);
        S.real_bios = true;
    } else {
        stand_in_bios();
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
    ay8910_init(&S.ay, CLOCK / 2, RATE);
    ay8910_set_amplitude(&S.ay, 2.0f);
    zd_init(&S.z80);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    zd_reset(&S.z80);
    sn76496_reset(&S.psg);
    ay8910_reset(&S.ay);
    tms_reset(&S.vdp);
    if (S.eeprom) i2cmem_reset(&S.i2c);
    // (the 1 KB of RAM comes up random: The Yolk's on You stops otherwise)
    for (int i = 0; i < 0x400; i++) S.mem[0x6000 + i] = (uint8_t)(rand() & 0xff);
    S.joymode = false;
    S.rom_enabled = true;
    S.sgm_ram = false;
    S.last_nmi = false;
    S.joystick[0] = S.joystick[1] = 0xff;
    S.keypad[0] = S.keypad[1] = 0xffff;
    S.audio_acc = 0;
    S.debt = 0;
}

static bool valid_header(const uint8_t *d)
{
    return (d[0] == 0x55 && d[1] == 0xaa) || (d[0] == 0xaa && d[1] == 0x55) || (d[0] == 0x66 && d[1] == 0x99);
}

void cons_eject(void)
{
    free(S.cart);
    S.cart = NULL;
    S.cart_size = 0;
    free(S.eeprom_mem);
    S.eeprom_mem = NULL;
    S.eeprom = 0;
    S.mega = false;
    memset(S.mem + 0x8000, 0, 0x8000);
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    const char *dummy;
    if (!why) why = &dummy;
    cons_eject();
    if (size > MAX_CART) size = MAX_CART;
    if (size <= 32768) {
        if (size < 2 || !valid_header(data)) {
            *why = "not a valid ColecoVision cartridge";
            free(data);
            return false;
        }
        memcpy(S.mem + 0x8000, data, size);
        S.cart = data;
        S.cart_size = size;
        cons_reset();
        return true;
    }
    // over 32 KB: a MegaCart (banks switched by reads of $FFC0-$FFFF) or a
    // Super Game Module game with an EEPROM (by writes to $FF90 / $FFA0 / $FFB0)
    const int banks = (int)(size / 0x4000);
    S.mega_size = banks - 1;
    // the banks are whole 16 KB pieces: pad the last one
    uint8_t *c = (uint8_t *)realloc(data, (size_t)banks * 0x4000);
    if (!c) {
        *why = "out of memory";
        free(data);
        return false;
    }
    S.cart = c;
    S.cart_size = (size_t)banks * 0x4000;
    const uint32_t crc = cons_crc32(c, size);
    if (crc == 0x62dacf07 || crc == 0xdddd1396) {   // Boxxle (24C256), Black Onix (24C08)
        S.eeprom = crc == 0x62dacf07 ? 256 : 8;
        const size_t n = S.eeprom == 256 ? 32768 : 1024;
        S.eeprom_mem = (uint8_t *)malloc(n);
        if (!S.eeprom_mem) {
            *why = "out of memory";
            cons_eject();
            return false;
        }
        i2cmem_init(&S.i2c, S.eeprom_mem, n);
        memcpy(S.mem + 0x8000, bank(0), 0x4000);
        cons_reset();
        return true;
    }
    S.mega = true;
    if (!valid_header(bank(S.mega_size))) {
        *why = "not a valid ColecoVision MegaCart";
        cons_eject();
        return false;
    }
    memcpy(S.mem + 0x8000, bank(S.mega_size), 0x4000);
    cons_reset();
    return true;
}

bool cons_loaded(void) { return S.cart != NULL; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
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

static void update_inputs(void)
{
    for (int p = 0; p < 2; p++) {
        const unsigned b = S.pads[p];
        uint8_t joy = 0xff;
        if (b & CP_UP) joy &= 0xfe;
        if (b & CP_RIGHT) joy &= 0xfd;
        if (b & CP_DOWN) joy &= 0xfb;
        if (b & CP_LEFT) joy &= 0xf7;
        if (b & CP_B1) joy &= 0xbf;
        S.joystick[p] = joy;
        S.keypad[p] = (b & CP_B2) ? 0xbfff : 0xffff;
    }
    // player 1's keypad: the keys 0-9, Q (*) and W (#)
    for (int d = 0; d < 10; d++)
        if (S.keys[CK_0 + d]) S.keypad[0] &= (uint16_t)~(1u << d);
    if (S.keys[CK_Q]) S.keypad[0] &= (uint16_t)~(1u << 10);
    if (S.keys[CK_W]) S.keypad[0] &= (uint16_t)~(1u << 11);
}

void cons_pad(int player, unsigned bits)
{
    S.pads[player & 1] = bits;
    update_inputs();
}

void cons_key(int key, bool down)
{
    if (key >= 0 && key < CK_COUNT) S.keys[key] = down;
    update_inputs();
}

const char *cons_status(void) { return S.real_bios ? NULL : "no BIOS built in: a stand-in (homebrew only)"; }
