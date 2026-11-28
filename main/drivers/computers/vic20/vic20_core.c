// vic20_core.c - Commodore VIC-20 (unexpanded, PAL 6561 or NTSC 6560): C port
// of dsp-cpp's drivers/computers/vic20 (dspc.h). A 6502 (m65xx) at the VIC's
// clock, the VIC (sound/mos6560d: video and sound), two 6522 VIAs (keyboard,
// joystick, RESTORE = F12 on VIA 1's CA1), 1 + 4 KB of RAM, the colour RAM,
// BASIC, KERNAL and the character ROM. Media: .prg (injected once BASIC has
// started, then RUN typed) and 4 / 8 KB cartridges at $A000 (.crt .bin .rom).
// DSPC_MODEL: 0 PAL, 1 NTSC.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../machine/via6522.h"
#include "../../../sound/mos6560d.h"

#ifndef DSPC_MODEL
#define DSPC_MODEL 0
#endif
#define PAL (DSPC_MODEL == 0)
#define PRG_INJECT_FRAMES 120

static const char *const kExts[] = { ".prg", ".crt", ".bin", ".rom", NULL };
const dspc_info dspc_machine = {
    PAL ? "vic20" : "vic20n", PAL ? "Commodore VIC-20 (PAL)" : "Commodore VIC-20 (NTSC)", PAL ? 224 : 200,
    PAL ? 296 : 248, 1,
    PAL ? (double)MOS6560_PAL_CLOCK / (312 * 71) : (double)MOS6560_NTSC_CLOCK / (261 * 65), MOS6560_RATE,
    "PRG CRT BIN", kExts, "*.prg *.PRG *.crt *.CRT *.bin *.rom", ".prg,.crt,.bin,.rom" };

static struct {
    m65xx cpu;
    mos6560d vic;
    via6522 via1, via2;
    uint8_t ram0[0x400], ram_main[0x1000], color_ram[0x400];
    uint8_t basic[0x2000], kernal[0x2000], chars[0x1000], cart[0x2000];
    size_t cart_size;
    uint8_t keyboard[8], key_row, key_col, joy;
    bool via1_nmi, via2_irq;
    uint8_t *pending_prg;
    size_t pending_n;
    int boot_frames, debt;
    int64_t audio_acc;
    int16_t audio[2048];
    int naudio;
    uint32_t *fb;
} S;

static uint8_t vic_mem(void *u, uint16_t offset)
{
    (void)u;
    if (offset & 0x2000) {
        if (((offset >> 10) & 7) == 0) return S.ram0[offset & 0x3ff];
        return S.ram_main[offset & 0xfff];
    }
    return S.chars[offset & 0xfff];
}

static uint8_t vic_color(void *u, uint16_t a) { (void)u; return (uint8_t)(S.color_ram[a & 0x3ff] & 0x0f); }

static uint8_t read_byte(uint16_t addr)
{
    switch ((addr >> 13) & 7) {
    case 0:
        if (((addr >> 10) & 7) == 0) return S.ram0[addr & 0x3ff];
        if (addr & 0x1000) return S.ram_main[addr & 0xfff];
        return S.vic.last_data;
    case 1: case 2: case 3: return S.vic.last_data;
    case 4:
        switch ((addr >> 10) & 7) {
        default: return S.chars[addr & 0xfff];
        case 4:
            if (addr & 0x10) return via6522_read(&S.via1, (uint8_t)(addr & 0x0f));
            if (addr & 0x20) return via6522_read(&S.via2, (uint8_t)(addr & 0x0f));
            if (addr >= 0x9000 && addr < 0x9010) return mos6560d_read(&S.vic, (uint8_t)(addr & 0x0f));
            return S.vic.last_data;
        case 5: return (uint8_t)(S.color_ram[addr & 0x3ff] | 0xf0);
        }
    case 5:
        if (S.cart_size && (size_t)(addr & 0x1fff) < S.cart_size) return S.cart[addr & 0x1fff];
        return S.vic.last_data;
    case 6: return S.basic[addr & 0x1fff];
    case 7: return S.kernal[addr & 0x1fff];
    }
    return S.vic.last_data;
}

static void write_byte(uint16_t addr, uint8_t value)
{
    switch ((addr >> 13) & 7) {
    case 0:
        if (((addr >> 10) & 7) == 0) S.ram0[addr & 0x3ff] = value;
        else if (addr & 0x1000) S.ram_main[addr & 0xfff] = value;
        break;
    case 4:
        switch ((addr >> 10) & 7) {
        case 4:
            if (addr & 0x10) via6522_write(&S.via1, (uint8_t)(addr & 0x0f), value);
            else if (addr & 0x20) via6522_write(&S.via2, (uint8_t)(addr & 0x0f), value);
            else if (addr >= 0x9000 && addr < 0x9010) mos6560d_write(&S.vic, (uint8_t)(addr & 0x0f), value);
            break;
        case 5: S.color_ram[addr & 0x3ff] = (uint8_t)(value & 0x0f); break;
        default: break;
        }
        break;
    default: break;
    }
}

static uint8_t c_read(void *u, uint16_t a) { (void)u; return read_byte(a); }
static void c_write(void *u, uint16_t a, uint8_t v) { (void)u; write_byte(a, v); }

static void c_cycles(void *u, int cycles)
{
    (void)u;
    if (cycles <= 0) return;
    via6522_tick(&S.via1, cycles);
    via6522_tick(&S.via2, cycles);
    S.audio_acc += (int64_t)cycles * MOS6560_RATE;
    const int64_t den = (int64_t)S.vic.clock;
    while (S.audio_acc >= den) {
        S.audio_acc -= den;
        const int16_t s = mos6560d_update(&S.vic);
        if (S.naudio < 2048) S.audio[S.naudio++] = s;
    }
}

static uint8_t via1_pa(void *u)
{
    (void)u;
    uint8_t data = 0x43;
    if (S.joy & 0x01) data |= 0x04;
    if (S.joy & 0x02) data |= 0x08;
    if (S.joy & 0x04) data |= 0x10;
    if (S.joy & 0x10) data |= 0x20;
    return data;
}

static uint8_t via2_pa(void *u)
{
    (void)u;
    uint8_t data = 0xff;
    for (int i = 0; i < 8; i++)
        if ((S.key_col & (1 << i)) == 0) data &= S.keyboard[i];
    return data;
}

static uint8_t via2_pb(void *u)
{
    (void)u;
    uint8_t data = (uint8_t)(0x7f | (((S.joy >> 3) & 1) << 7));
    for (int i = 0; i < 8; i++)
        if ((S.key_row & (1 << i)) == 0)
            for (int c = 0; c < 8; c++)
                if ((S.keyboard[c] & (1 << i)) == 0) data &= (uint8_t)~(1 << c);
    return data;
}

static void via2_pa_w(void *u, uint8_t v) { (void)u; S.key_row = v; }
static void via2_pb_w(void *u, uint8_t v) { (void)u; S.key_col = v; }
static void via1_irq(void *u, bool on) { (void)u; S.via1_nmi = on; m65xx_set_nmi(&S.cpu, on ? M65_ASSERT : M65_CLEAR); }
static void via2_irq(void *u, bool on) { (void)u; S.via2_irq = on; m65xx_set_irq(&S.cpu, on ? M65_ASSERT : M65_CLEAR); }

static bool load_roms(void)
{
    static const char *const sets[] = { PAL ? "vic20" : "vic20n", "vic20", NULL };
    static const uint32_t kBasicCrc[] = { 0xdb4c43c1 };
    uint8_t *b = dspc_rom_find(sets, "901486-01.ue11|901486-01|basic|basic.bin|basic.rom", kBasicCrc, 1, 0x2000);
    if (!b) return false;
    memcpy(S.basic, b, 0x2000);
    free(b);
    static const uint32_t kPalCrc[] = { 0x4be07cb4, 0x705e7810, 0xe5e7c174 };
    static const uint32_t kNtscCrc[] = { 0xe5e7c174, 0x683a757f, 0x4be07cb4 };
    uint32_t len = 0;
    uint8_t *k = NULL;
    static const char *const kPalNames[] = { "901486-07.ue12", "901486-07", "jiffydos vic-20 pal.ue12", NULL };
    static const char *const kAnyNames[] = { "901486-06.ue12", "901486-06", "901486-07.ue12", "901486-07", "kernal",
                                             "kernal.bin", "kernal.rom", NULL };
    if (PAL)
        for (int i = 0; !k && kPalNames[i]; i++) k = romfs_get(sets, kPalNames[i], 0, 0x2000, &len);
    for (int i = 0; !k && kAnyNames[i]; i++) k = romfs_get(sets, kAnyNames[i], 0, 0x2000, &len);
    const uint32_t *crcs = PAL ? kPalCrc : kNtscCrc;
    for (int i = 0; !k && i < 3; i++) k = romfs_get(sets, "901486-07.ue12", crcs[i], 0x2000, &len);
    if (!k) {
        romfs_missing(sets, "901486-06/07.ue12 (KERNAL)");
        return false;
    }
    memcpy(S.kernal, k, 0x2000);
    free(k);
    static const uint32_t kCharCrc[] = { 0x83e032a6 };
    uint8_t *c = dspc_rom_find(sets, "901460-03.ud7|901460-03|chargen|chargen.bin|chargen.rom", kCharCrc, 1, 0x1000);
    if (!c) return false;
    memcpy(S.chars, c, 0x1000);
    free(c);
    romfs_release();
    return true;
}

bool dspc_init(void)
{
    memset(&S, 0, sizeof S);
    if (!mos6560d_init(&S.vic, PAL)) return false;
    S.fb = (uint32_t *)calloc((size_t)S.vic.vis_w * S.vic.vis_h, 4);
    if (!S.fb) return false;
    S.cpu.read = c_read;
    S.cpu.write = c_write;
    S.cpu.cycles = c_cycles;
    S.vic.mem = vic_mem;
    S.vic.color = vic_color;
    S.via1.pa_in = via1_pa;
    S.via1.irq = via1_irq;
    S.via2.pa_in = via2_pa;
    S.via2.pa_out = via2_pa_w;
    S.via2.pb_in = via2_pb;
    S.via2.pb_out = via2_pb_w;
    S.via2.irq = via2_irq;
    via6522_reset(&S.via1);
    via6522_reset(&S.via2);
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    if (!load_roms()) return false;
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    memset(S.ram0, 0, sizeof S.ram0);
    memset(S.ram_main, 0, sizeof S.ram_main);
    memset(S.color_ram, 0, sizeof S.color_ram);
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    S.key_row = S.key_col = 0xff;
    S.joy = 0xff;
    S.via1_nmi = S.via2_irq = false;
    free(S.pending_prg);
    S.pending_prg = NULL;
    S.pending_n = 0;
    S.boot_frames = 0;
    S.debt = 0;
    S.audio_acc = 0;
    S.naudio = 0;
    uint8_t data = 0xff;
    for (size_t i = 0; i < sizeof S.ram0; i++) {
        S.ram0[i] = data;
        if ((i % 64) == 0) data ^= 0xff;
    }
    data = 0xff;
    for (size_t i = 0; i < sizeof S.ram_main; i++) {
        S.ram_main[i] = data;
        if ((i % 64) == 0) data ^= 0xff;
    }
    mos6560d_reset(&S.vic);
    via6522_reset(&S.via1);
    via6522_reset(&S.via2);
    m65xx_reset(&S.cpu);
    m65xx_set_irq(&S.cpu, S.via2_irq ? M65_ASSERT : M65_CLEAR);
    m65xx_set_nmi(&S.cpu, S.via1_nmi ? M65_ASSERT : M65_CLEAR);
}

void dspc_set_inputs(const dspc_inputs *in)
{
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    const bool *k = in->keys;
#define P(col, m) (S.keyboard[col] &= (uint8_t)~(m))
    if (k[DK_1]) P(0, 0x01);
    if (k[DK_3]) P(0, 0x02);
    if (k[DK_5]) P(0, 0x04);
    if (k[DK_7]) P(0, 0x08);
    if (k[DK_9]) P(0, 0x10);
    if (k[DK_PLUS] || k[DK_EQUALS]) P(0, 0x20);
    if (k[DK_BACKSLASH]) P(0, 0x40);
    if (k[DK_BACKSPACE] || k[DK_DELETE]) P(0, 0x80);
    if (k[DK_BACKQUOTE]) P(1, 0x01);
    if (k[DK_W]) P(1, 0x02);
    if (k[DK_R]) P(1, 0x04);
    if (k[DK_Y]) P(1, 0x08);
    if (k[DK_I]) P(1, 0x10);
    if (k[DK_P]) P(1, 0x20);
    if (k[DK_ASTERISK]) P(1, 0x40);
    if (k[DK_ENTER]) P(1, 0x80);
    if (k[DK_TAB] || k[DK_LCTRL]) P(2, 0x01);
    if (k[DK_A]) P(2, 0x02);
    if (k[DK_D]) P(2, 0x04);
    if (k[DK_G]) P(2, 0x08);
    if (k[DK_J]) P(2, 0x10);
    if (k[DK_L]) P(2, 0x20);
    if (k[DK_SEMICOLON]) P(2, 0x40);
    if (k[DK_RIGHT] || k[DK_LEFT]) P(2, 0x80);
    if (k[DK_ESCAPE] || k[DK_HOME]) P(3, 0x01);
    if (k[DK_LSHIFT]) P(3, 0x02);
    if (k[DK_X]) P(3, 0x04);
    if (k[DK_V]) P(3, 0x08);
    if (k[DK_N]) P(3, 0x10);
    if (k[DK_COMMA]) P(3, 0x20);
    if (k[DK_SLASH]) P(3, 0x40);
    if (k[DK_DOWN] || k[DK_UP]) P(3, 0x80);
    if (k[DK_SPACE]) P(4, 0x01);
    if (k[DK_Z]) P(4, 0x02);
    if (k[DK_C]) P(4, 0x04);
    if (k[DK_B]) P(4, 0x08);
    if (k[DK_M]) P(4, 0x10);
    if (k[DK_PERIOD]) P(4, 0x20);
    if (k[DK_RSHIFT]) P(4, 0x40);
    if (k[DK_F1]) P(4, 0x80);
    if (k[DK_CBM] || k[DK_LGUI] || k[DK_RCTRL]) P(5, 0x01);
    if (k[DK_S]) P(5, 0x02);
    if (k[DK_F]) P(5, 0x04);
    if (k[DK_H]) P(5, 0x08);
    if (k[DK_K]) P(5, 0x10);
    if (k[DK_QUOTE]) P(5, 0x20);
    if (k[DK_MINUS]) P(5, 0x40);
    if (k[DK_F3]) P(5, 0x80);
    if (k[DK_Q]) P(6, 0x01);
    if (k[DK_E]) P(6, 0x02);
    if (k[DK_T]) P(6, 0x04);
    if (k[DK_U]) P(6, 0x08);
    if (k[DK_O]) P(6, 0x10);
    if (k[DK_AT]) P(6, 0x20);
    if (k[DK_DELETE]) P(6, 0x40);
    if (k[DK_F5]) P(6, 0x80);
    if (k[DK_2]) P(7, 0x01);
    if (k[DK_4]) P(7, 0x02);
    if (k[DK_6]) P(7, 0x04);
    if (k[DK_8]) P(7, 0x08);
    if (k[DK_0]) P(7, 0x10);
    if (k[DK_MINUS]) P(7, 0x20);
    if (k[DK_HOME]) P(7, 0x40);
    if (k[DK_F7]) P(7, 0x80);
    if (k[DK_LEFT] || k[DK_UP]) P(3, 0x02);
#undef P
    S.joy = 0xff;
    if (in->p1.up || k[DK_UP]) S.joy &= (uint8_t)~0x01;
    if (in->p1.down || k[DK_DOWN]) S.joy &= (uint8_t)~0x02;
    if (in->p1.left || k[DK_LEFT]) S.joy &= (uint8_t)~0x04;
    if (in->p1.right || k[DK_RIGHT]) S.joy &= (uint8_t)~0x08;
    if (in->p1.b1) S.joy &= (uint8_t)~0x10;
    via6522_write_ca1(&S.via1, !k[DK_F12]);
}

static void inject_prg(const uint8_t *data, size_t size)
{
    const uint16_t addr = (uint16_t)(data[0] | (data[1] << 8));
    const size_t n = size - 2;
    for (size_t i = 0; i < n; i++) write_byte((uint16_t)(addr + i), data[i + 2]);
    const uint16_t end = (uint16_t)(addr + n);
    if (addr != 0x1001) return;
    for (int o = 0x2d; o <= 0x31; o += 2) {
        S.ram0[o] = (uint8_t)end;
        S.ram0[o + 1] = (uint8_t)(end >> 8);
    }
    S.ram0[0xae] = (uint8_t)end;
    S.ram0[0xaf] = (uint8_t)(end >> 8);
    static const uint8_t kRun[] = { 'R', 'U', 'N', 0x0d };
    for (size_t i = 0; i < sizeof kRun; i++) S.ram0[0x277 + i] = kRun[i];
    S.ram0[0xc6] = (uint8_t)sizeof kRun;
}

int dspc_run_frame(const int16_t **audio)
{
    if (S.pending_prg && ++S.boot_frames >= PRG_INJECT_FRAMES && S.ram0[0x2b] == 0x01 && S.ram0[0x2c] == 0x10) {
        uint8_t *p = S.pending_prg;
        const size_t n = S.pending_n;
        S.pending_prg = NULL;
        S.pending_n = 0;
        inject_prg(p, n);
        free(p);
    }
    S.naudio = 0;
    const int lines = S.vic.total_lines, cpl = S.vic.cycles_per_line;
    for (int line = 0; line < lines; ++line) {
        mos6560d_update_line(&S.vic, line);
        S.debt += cpl;
        if (S.debt > 0) S.debt -= m65xx_run(&S.cpu, S.debt);
    }
    mos6560d_blit(&S.vic, S.fb);
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint32_t *dspc_frame(void) { return S.fb; }

static bool ends_ci(const char *s, const char *ext)
{
    const size_t n = strlen(ext), l = strlen(s);
    if (l < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[l - n + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    if ((ends_ci(name, ".crt") || ends_ci(name, ".bin") || ends_ci(name, ".rom")) && (size == 0x1000 || size == 0x2000)) {
        memset(S.cart, 0xff, sizeof S.cart);
        memcpy(S.cart, data, size);
        S.cart_size = size;
        dspc_reset();
        return NULL;
    }
    if (size < 3) return "PRG too small";
    uint8_t *p = (uint8_t *)malloc(size);
    if (!p) return "out of memory";
    memcpy(p, data, size);
    free(S.pending_prg);
    S.pending_prg = p;
    S.pending_n = size;
    S.boot_frames = 0;
    return NULL;
}
