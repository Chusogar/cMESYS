// nes_core.c - Nintendo Entertainment System / Famicom (NTSC, 1983): a C11
// migration of dsp-cpp's drivers/consoles/nes and machine/nes_mapper (from
// nes.pas, nes_mappers.pas and n2a03.pas). The 2A03 (cpu/m6502/m65xx in
// its NES mode, sound/nes_apu), the 2C02 (video/nes_ppu) and the
// cartridge's mapper, banked as dsp-cpp does it (the windows copied into
// the CPU's and the PPU's memory): NROM, MMC1, UxROM, CNROM, MMC3 / MMC6,
// AxROM, MMC2, MMC4, Color Dreams, CPROM, 15, 34, GxROM, 68, 70, 71, 76, 79,
// 87, 88, 93, 94, 95, 113, 146, 180, 184, 185, 206. iNES / NES 2.0 files.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../video/nes_ppu.h"
#include "../../../sound/nes_apu.h"

#define CLOCK 1789773
#define RATE 44100

static const char *const kExts[] = { ".nes", NULL };
const cons_info cons_machine = {
    "nes", "Nintendo NES", "Nintendo, 1983", kExts, "NES", NES_W, NES_H, NES_FPS, RATE,
    { "A", "B", NULL, NULL }, "START", "SELECT", CONS_NO_KEYS, 2, NULL,
};

static struct {
    m65xx cpu;
    nes_ppu ppu;
    nes_apu apu;
    uint8_t *mem;                // the CPU's 64 KB (PRG windows at $8000)
    uint16_t *fb;
    uint8_t *cart;               // the file
    const uint8_t *prg, *chr;
    int last_prg, last_chr;
    // mapper
    int mapper, submapper;
    bool prg_ram_enable, prg_ram_writable;
    uint8_t chr_map[2];
    uint8_t regs[0x31], dregs[16];
    uint8_t serial_cnt, valor_map, latch0, latch1;
    int counter;
    bool irq_ena, reload, chr_extra_ena;
    // machine
    uint8_t joy1, joy2, joy1_read, joy2_read;
    bool val_4016, even_frame, mapper_irq;
    uint64_t cycle_count, apu_cycles, frame_irq_cycles, audio_acc;
    double t_want, t_done;
    int16_t audio[1024];
    int naudio;
    bool loaded;
} S;

// ---- mapper (nes_mapper) ----
static void set_irq(int line)
{
    S.mapper_irq = line != M65_CLEAR;
    m65xx_set_irq(&S.cpu, (uint8_t)line);
}

static void set_prg_16(uint16_t pos, int bank)
{
    if (S.last_prg <= 0) return;
    memcpy(S.mem + pos, S.prg + (size_t)(bank % S.last_prg) * 0x4000, 0x4000);
}

static void set_prg_32(int bank)
{
    if (S.last_prg <= 1) {
        set_prg_16(0x8000, bank);
        return;
    }
    const int b = (bank % (S.last_prg >> 1)) << 1;
    set_prg_16(0x8000, b);
    set_prg_16(0xc000, b | 1);
}

static void set_prg_8(uint16_t pos, int bank)
{
    if (S.last_prg <= 0) return;
    const int b = bank % (S.last_prg << 1);
    memcpy(S.mem + pos, S.prg + (size_t)(b >> 1) * 0x4000 + 0x2000 * (b & 1), 0x2000);
}

static inline uint8_t *chr_bank(int i) { return S.ppu.chr[i & 3]; }

static void set_chr_8(int bank)
{
    if (S.last_chr <= 0) return;
    const uint8_t *c = S.chr + (size_t)(bank % S.last_chr) * 0x2000;
    memcpy(chr_bank(S.chr_map[0]), c, 0x1000);
    memcpy(chr_bank(S.chr_map[1]), c + 0x1000, 0x1000);
}

static void set_chr_4(uint16_t pos, int bank)
{
    if (S.last_chr <= 0) return;
    const int b = bank % (S.last_chr << 1);
    memcpy(chr_bank(S.chr_map[(pos >> 12) & 1]), S.chr + (size_t)(b >> 1) * 0x2000 + 0x1000 * (b & 1), 0x1000);
}

static void set_chr_2(uint16_t pos, int bank)
{
    if (S.last_chr <= 0) return;
    const int b = bank % (S.last_chr << 2);
    memcpy(chr_bank(S.chr_map[(pos >> 12) & 1]) + 0x800 * ((pos >> 11) & 1),
           S.chr + (size_t)(b >> 2) * 0x2000 + 0x800 * (b & 3), 0x800);
}

static void set_chr_1(uint16_t pos, int bank)
{
    if (S.last_chr <= 0) return;
    const int b = bank % (S.last_chr << 3);
    memcpy(chr_bank(S.chr_map[(pos >> 12) & 1]) + 0x400 * ((pos >> 10) & 3),
           S.chr + (size_t)(b >> 3) * 0x2000 + 0x400 * (b & 7), 0x400);
}

static void m1_chr(void)
{
    if (S.regs[0] & 0x10) {
        if (S.ppu.write_chr) {
            S.chr_map[0] = S.regs[1];
            S.chr_map[1] = S.regs[2];
        } else {
            set_chr_4(0x0000, S.regs[1]);
            set_chr_4(0x1000, S.regs[2]);
        }
    } else if (S.ppu.write_chr) {
        S.chr_map[0] = 0;
        S.chr_map[1] = 1;
    } else {
        set_chr_8((S.regs[1] & 0x1f) >> 1);
    }
}

static void m1_prg(void)
{
    if (S.last_prg <= 0) return;
    const int extra = S.last_prg > 16 ? (S.regs[1] & 0x10) : 0;
    const int b = ((S.regs[3] & 0x0f) | extra) % S.last_prg;
    switch ((S.regs[0] >> 2) & 3) {
    case 0: case 1:
        set_prg_16(0x8000, b & 0xfe);
        set_prg_16(0xc000, b | 1);
        break;
    case 2:
        set_prg_16(0x8000, extra);
        set_prg_16(0xc000, b);
        break;
    default:
        set_prg_16(0x8000, b);
        set_prg_16(0xc000, (0x0f | extra) % S.last_prg);
        break;
    }
    if (S.submapper == 0) S.prg_ram_enable = !(S.regs[3] & 0x10);
}

static void m1_write(uint16_t a, uint8_t v)
{
    if (S.counter < 2 && S.serial_cnt == 0) return;
    if (v & 0x80) {
        S.serial_cnt = 0;
        S.valor_map = 0;
        S.regs[0] |= 0x0c;
        S.counter = 0;
        if (S.last_prg > 0) set_prg_16(0xc000, 0x0f % S.last_prg);
        return;
    }
    S.valor_map |= (uint8_t)((v & 1) << S.serial_cnt);
    S.serial_cnt++;
    S.counter = 0;
    if (S.serial_cnt != 5) return;
    S.regs[(a >> 13) & 3] = S.valor_map;
    static const uint8_t mir[4] = { NM_LOW, NM_HIGH, NM_VERTICAL, NM_HORIZONTAL };
    S.ppu.mirror = mir[S.regs[0] & 3];
    m1_chr();
    m1_prg();
    S.valor_map = 0;
    S.serial_cnt = 0;
}

static void m4_chr(uint8_t v)
{
    if (S.ppu.write_chr) return;
    const uint16_t b = (uint16_t)((v & 0x80) << 5);
    set_chr_1(0x0000 ^ b, S.dregs[0] & 0xfe);
    set_chr_1(0x0400 ^ b, S.dregs[0] | 1);
    set_chr_1(0x0800 ^ b, S.dregs[1] & 0xfe);
    set_chr_1(0x0c00 ^ b, S.dregs[1] | 1);
    set_chr_1(0x1000 ^ b, S.dregs[2]);
    set_chr_1(0x1400 ^ b, S.dregs[3]);
    set_chr_1(0x1800 ^ b, S.dregs[4]);
    set_chr_1(0x1c00 ^ b, S.dregs[5]);
}

static void m4_prg(uint8_t v)
{
    if (S.last_prg <= 0) return;
    const int t1 = S.dregs[6] % (S.last_prg << 1), t2 = S.dregs[7] % (S.last_prg << 1);
    set_prg_8(0xa000, t2);
    if (!(v & 0x40)) {
        set_prg_8(0x8000, t1);
        set_prg_8(0xc000, (S.last_prg << 1) - 2);
    } else {
        set_prg_8(0x8000, (S.last_prg << 1) - 2);
        set_prg_8(0xc000, t1);
    }
}

static void m4_write(uint16_t a, uint8_t v, bool mmc6)
{
    switch (a & 0xe001) {
    case 0x8000:
        if ((v & 0x40) != (S.regs[0] & 0x40)) m4_prg(v);
        if ((v & 0x80) != (S.regs[0] & 0x80)) m4_chr(v);
        if (mmc6) S.prg_ram_enable = (v & 0x20) != 0;
        S.regs[0] = v;
        break;
    case 0x8001:
        if (!mmc6 && (S.regs[0] & 7) < 2) v &= 0xfe;
        S.dregs[S.regs[0] & 7] = v;
        m4_prg(S.regs[0]);
        m4_chr(S.regs[0]);
        break;
    case 0xa000:
        if (S.ppu.mirror != NM_FOUR) S.ppu.mirror = (v & 1) ? NM_HORIZONTAL : NM_VERTICAL;
        break;
    case 0xa001:
        if (!mmc6) {
            S.prg_ram_enable = (v & 0x80) != 0;
            S.prg_ram_writable = !(v & 0x40);
        }
        S.regs[3] = v;
        break;
    case 0xc000: S.regs[2] = v; break;
    case 0xc001: S.reload = true; break;
    case 0xe000:
        S.irq_ena = false;
        set_irq(M65_CLEAR);
        break;
    case 0xe001: S.irq_ena = true; break;
    default: break;
    }
}

static void m4_line(void)
{
    if (!(S.ppu.control2 & 0x18)) return;
    if (S.counter == 0 || S.reload) {
        S.counter = S.regs[2];
        S.reload = false;
    } else {
        S.counter--;
    }
    if (S.counter == 0) {
        S.reload = true;
        if (S.irq_ena) set_irq(M65_ASSERT);
    }
}

static void m9_ppu(uint16_t a)
{
    switch (a & 0x3ff0) {
    case 0x0fd0: S.latch0 = 0xfd; set_chr_4(0x0000, S.regs[0]); break;
    case 0x0fe0: S.latch0 = 0xfe; set_chr_4(0x0000, S.regs[1]); break;
    case 0x1fd0: S.latch1 = 0xfd; set_chr_4(0x1000, S.regs[2]); break;
    case 0x1fe0: S.latch1 = 0xfe; set_chr_4(0x1000, S.regs[3]); break;
    default: break;
    }
}

// MMC2 / MMC4's latch registers (r: 0-3 for $B000-$E000), mirroring (r 4)
static void m9_reg(int r, uint8_t v)
{
    if (r == 4) {
        S.ppu.mirror = (v & 1) ? NM_HORIZONTAL : NM_VERTICAL;
        return;
    }
    S.regs[r] = v & 0x1f;
    if (r < 2 && S.latch0 == (r ? 0xfe : 0xfd)) set_chr_4(0x0000, v & 0x1f);
    if (r >= 2 && S.latch1 == (r == 3 ? 0xfe : 0xfd)) set_chr_4(0x1000, v & 0x1f);
}

static void m95_nametable(void)
{
    switch (S.regs[1] + (S.regs[2] << 1)) {
    case 0: S.ppu.mirror = NM_LOW; break;
    case 1: S.ppu.mirror = NM_HORIZONTAL; break;
    case 2: S.ppu.mirror = NM_MAP95; break;
    case 3: S.ppu.mirror = NM_HIGH; break;
    default: break;
    }
}

// the Namco 108 family (76, 88, 95, 206): $8000 / $8001
static void n108_write(uint16_t a, uint8_t v)
{
    a &= 0x8001;
    if (a == 0x8000) {
        S.regs[0] = S.mapper == 88 ? v : (v & 7);
        return;
    }
    if (a != 0x8001) return;
    const int r = S.regs[0] & 7;
    if (S.mapper == 76) {
        switch (r) {
        case 2: set_chr_2(0x0000, v & 0x3f); break;
        case 3: set_chr_2(0x0800, v & 0x3f); break;
        case 4: set_chr_2(0x1000, v & 0x3f); break;
        case 5: set_chr_2(0x1800, v & 0x3f); break;
        case 6: set_prg_8(0x8000, v & 0x3f); break;
        case 7: set_prg_8(0xa000, v & 0x3f); break;
        default: break;
        }
        return;
    }
    if (S.mapper == 88) {
        switch (r) {
        case 0: set_chr_2(0x0000, (v >> 1) & 0x3f); break;
        case 1: set_chr_2(0x0800, (v >> 1) & 0x3f); break;
        case 2: set_chr_1(0x1000, v | 0x40); break;
        case 3: set_chr_1(0x1400, v | 0x40); break;
        case 4: set_chr_1(0x1800, v | 0x40); break;
        case 5: set_chr_1(0x1c00, v | 0x40); break;
        case 6: set_prg_8(0x8000, v & 0x0f); break;
        default: set_prg_8(0xa000, v & 0x0f); break;
        }
        return;
    }
    switch (r) {                 // 95, 206
    case 0:
        set_chr_2(0x0000, (v >> 1) & 0x1f);
        if (S.mapper == 95) {
            S.regs[1] = (v >> 5) & 1;
            m95_nametable();
        }
        break;
    case 1:
        set_chr_2(0x0800, (v >> 1) & 0x1f);
        if (S.mapper == 95) {
            S.regs[2] = (v >> 5) & 1;
            m95_nametable();
        }
        break;
    case 2: set_chr_1(0x1000, v & 0x3f); break;
    case 3: set_chr_1(0x1400, v & 0x3f); break;
    case 4: set_chr_1(0x1800, v & 0x3f); break;
    case 5: set_chr_1(0x1c00, v & 0x3f); break;
    case 6: set_prg_8(0x8000, v & 0x0f); break;
    default: set_prg_8(0xa000, v & 0x0f); break;
    }
}

static void m34_write(uint16_t a, uint8_t v)
{
    if (a < 0x8000) S.mem[a] = v;
    if (a == 0x7ffd) S.regs[0] = v & 1;
    else if (a == 0x7ffe) S.regs[1] = v & 0x0f;
    else if (a == 0x7fff) S.regs[2] = v & 0x0f;
    else if (S.last_chr == 0) S.regs[0] = v & 0x03;
    set_prg_32(S.regs[0]);
    if (S.last_chr) {
        set_chr_4(0x0000, S.regs[1]);
        set_chr_4(0x1000, S.regs[2]);
    }
}

static void m15_write(uint16_t a, uint8_t v)
{
    if (S.last_prg <= 0) return;
    const int b = (v & 0x3f) % S.last_prg;
    switch (a & 3) {
    case 0:
        set_prg_16(0x8000, b);
        set_prg_16(0xc000, b | 1);
        break;
    case 1:
        set_prg_16(0x8000, b);
        set_prg_16(0xc000, ((v & 0x3f) | 7) % S.last_prg);
        break;
    case 2:
        memcpy(S.mem + 0x8000, S.prg + (size_t)b * 0x4000 + 0x2000 * ((v & 0x80) >> 7), 0x2000);
        memcpy(S.mem + 0xa000, S.mem + 0x8000, 0x2000);
        memcpy(S.mem + 0xc000, S.mem + 0x8000, 0x2000);
        memcpy(S.mem + 0xe000, S.mem + 0x8000, 0x2000);
        break;
    default:
        set_prg_16(0x8000, b);
        memcpy(S.mem + 0xc000, S.mem + 0x8000, 0x4000);
        break;
    }
    S.ppu.mirror = (v & 0x40) ? NM_HORIZONTAL : NM_VERTICAL;
}

static void m68_write(uint16_t a, uint8_t v)
{
    switch ((a >> 12) & 0x0f) {
    case 0x8: set_chr_2(0x0000, v); break;
    case 0x9: set_chr_2(0x0800, v); break;
    case 0xa: set_chr_2(0x1000, v); break;
    case 0xb: set_chr_2(0x1800, v); break;
    case 0xc: case 0xd:
        if (S.chr_extra_ena && S.last_chr > 0) {
            const int b = (0x80 | (v & 0x7f)) % (S.last_chr << 3);
            memcpy(S.ppu.name_table[((a >> 12) & 0x0f) - 0xc], S.chr + (size_t)(b >> 3) * 0x2000 + 0x400 * (b & 7), 0x400);
        }
        break;
    case 0xe:
        S.ppu.mirror = (v & 3) == 0 ? NM_VERTICAL : (v & 3) == 1 ? NM_HORIZONTAL : NM_LOW;
        S.chr_extra_ena = (v & 0x10) != 0;
        break;
    case 0xf:
        set_prg_16(0x8000, v & 0x0f);
        S.prg_ram_enable = (v & 0x10) != 0;
        break;
    default: break;
    }
}

static void write_rom(uint16_t a, uint8_t v)
{
    switch (S.mapper) {
    case 1: m1_write(a, v); break;
    case 2: set_prg_16(0x8000, v); break;
    case 3: set_chr_8(v & 0x03); break;
    case 4: m4_write(a, v, S.submapper == 1); break;
    case 7:
        set_prg_32(v & 0x0f);
        S.ppu.mirror = (v & 0x10) ? NM_HIGH : NM_LOW;
        break;
    case 9:
        if ((a >> 12) == 0x0a) set_prg_8(0x8000, v & 0x0f);
        else if ((a >> 12) >= 0x0b) m9_reg((a >> 12) - 0x0b, v);
        break;
    case 10: {
        const int r = (a >> 12) & 7;
        if (r == 2) set_prg_16(0x8000, v & 0x0f);
        else if (r >= 3) m9_reg(r - 3, v);
        break;
    }
    case 11:
        set_prg_32(v & 0x03);
        if (S.last_chr) set_chr_8(v >> 4);
        break;
    case 13: S.chr_map[1] = v & 3; break;
    case 15: m15_write(a, v); break;
    case 34: m34_write(a, v); break;
    case 66:
        set_chr_8(v & 0x03);
        set_prg_32((v & 0x30) >> 4);
        break;
    case 68: m68_write(a, v); break;
    case 70:
        set_chr_8(v & 0x0f);
        set_prg_16(0x8000, (v & 0xf0) >> 4);
        break;
    case 71:
        if (a >= 0x9000 && a <= 0x9fff) S.ppu.mirror = (v & 0x10) ? NM_HIGH : NM_LOW;
        else if (a >= 0xc000) set_prg_16(0x8000, v & 0x0f);
        break;
    case 76: case 88: case 95: case 206: n108_write(a, v); break;
    case 93:
        set_prg_16(0x8000, (v & 0x70) >> 4);
        S.ppu.write_chr = (v & 1) != 0;
        break;
    case 94: set_prg_16(0x8000, (v >> 2) & 0x07); break;
    case 180: set_prg_16(0xc000, v & 0x07); break;
    case 185:
        S.ppu.disable_chr = true;
        if ((((v & 0x0f) != 0) && v != 0x13) || S.latch1 == 0x21) {
            if (!(v == 0x21 && S.latch1 != 0x13)) {
                S.ppu.disable_chr = false;
                set_chr_8(v & 0x03);
            }
        }
        S.latch1 = v;
        break;
    default: break;
    }
}

static uint8_t read_prg_ram(uint16_t a)
{
    if (S.mapper == 4 && S.submapper == 1) {        // MMC6
        if (!S.prg_ram_enable || !(S.regs[3] & 0xa0)) return (uint8_t)a;
        if (a <= 0x6fff) return (uint8_t)a;
        if (!(a & 0x0200)) return (S.regs[3] & 0x20) ? S.mem[0x7000 + (a & 0x1ff)] : 0;
        return (S.regs[3] & 0x80) ? S.mem[0x7200 + (a & 0x1ff)] : 0;
    }
    if (!S.prg_ram_enable) return S.ppu.open_bus;
    return S.mem[a];
}

static void write_prg_ram(uint16_t a, uint8_t v)
{
    switch (S.mapper) {
    case 4:
        if (S.submapper == 1) {
            if (!S.prg_ram_enable || a <= 0x6fff) return;
            if (!(a & 0x0200)) {
                if ((S.regs[3] & 0x30) == 0x30) S.mem[0x7000 + (a & 0x1ff)] = v;
            } else if ((S.regs[3] & 0xc0) == 0xc0) {
                S.mem[0x7200 + (a & 0x1ff)] = v;
            }
            return;
        }
        break;
    case 34: m34_write(a, v); return;
    case 87:
        set_chr_8((v >> 1) | ((v & 1) << 1));
        return;
    case 184:
        set_chr_4(0x0000, v & 0x07);
        set_chr_4(0x1000, 4 | ((v >> 4) & 0x07));
        return;
    default: break;
    }
    if (!S.prg_ram_writable || !S.prg_ram_enable) return;
    S.mem[a] = v;
}

static void write_expansion(uint16_t a, uint8_t v)
{
    if ((a & 0xe100) != 0x4100) return;
    if (S.mapper == 79 || S.mapper == 146) {
        set_prg_32((v >> 3) & 1);
        set_chr_8(v & 0x07);
    } else if (S.mapper == 113) {
        S.ppu.mirror = (v & 0x80) ? NM_HORIZONTAL : NM_VERTICAL;
        set_prg_32((v >> 3) & 7);
        set_chr_8((v & 7) | ((v >> 3) & 8));
    }
}

static bool mapper_known(int m)
{
    static const short k[] = { 0, 1, 2, 3, 4, 7, 9, 10, 11, 13, 15, 34, 66, 68, 70, 71, 76, 79, 87, 88,
                               93, 94, 95, 113, 146, 180, 184, 185, 206 };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == m) return true;
    return false;
}

static void line_ack(void *u, bool force)
{
    (void)u, (void)force;
    if (S.mapper == 4) m4_line();
}

static void ppu_read(void *u, uint16_t a)
{
    (void)u;
    if (S.mapper == 9 || S.mapper == 10) m9_ppu(a);
}

static void mapper_reset(void)
{
    S.prg_ram_writable = S.prg_ram_enable = false;
    S.latch0 = S.latch1 = 0;
    S.reload = false;
    S.counter = 0;
    S.irq_ena = false;
    S.serial_cnt = S.valor_map = 0;
    memset(S.dregs, 0, sizeof S.dregs);
    memset(S.regs, 0, sizeof S.regs);
    S.chr_map[0] = 0;
    S.chr_map[1] = 1;
    S.chr_extra_ena = false;
    switch (S.mapper) {
    case 1:
        S.prg_ram_writable = true;
        S.regs[0] = 0x0c;
        set_prg_16(0x8000, 0);
        set_prg_16(0xc000, S.last_prg - 1);
        break;
    case 2: case 70: case 76: case 79: case 88: case 93: case 94: case 95: case 146: case 180: case 206:
        set_prg_16(0x8000, 0);
        set_prg_16(0xc000, S.last_prg - 1);
        break;
    case 4:
        S.prg_ram_writable = S.prg_ram_enable = true;
        S.dregs[1] = 2;
        S.dregs[2] = 4;
        S.dregs[3] = 5;
        S.dregs[4] = 6;
        S.dregs[5] = 7;
        S.dregs[7] = 1;
        m4_chr(0);
        m4_prg(0);
        set_prg_16(0x8000, S.last_prg - 1);
        set_prg_16(0xc000, S.last_prg - 1);
        break;
    case 7: case 15: case 184:
        set_prg_32(0);
        break;
    case 9: case 10:
        set_prg_8(0x8000, 0);
        set_prg_8(0xa000, (S.last_prg << 1) - 3);
        set_prg_8(0xc000, (S.last_prg << 1) - 2);
        set_prg_8(0xe000, (S.last_prg << 1) - 1);
        set_chr_8(0);
        S.latch0 = S.latch1 = 0xfe;
        break;
    case 11: case 66:
        set_prg_32(0);
        if (S.last_chr) set_chr_8(0);
        break;
    case 34:
        S.regs[2] = 1;
        S.prg_ram_enable = S.prg_ram_writable = true;
        set_prg_32(S.last_prg >> 1);
        break;
    case 68:
        set_prg_16(0x8000, 0);
        set_prg_16(0xc000, S.last_prg - 1);
        S.prg_ram_writable = true;
        break;
    case 71:
        if (S.last_prg > 0) set_prg_16(0xc000, S.last_prg - 1);
        break;
    default:
        break;
    }
}

// ---- the machine ----
static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    S.ppu.open_bus = (uint8_t)(a >> 8);
    if (a <= 0x1fff) return S.mem[a & 0x7ff];
    if (a <= 0x3fff) {
        switch (a & 7) {
        case 2: {
            const uint8_t st = S.ppu.status;
            S.ppu.status &= 0x60;
            S.ppu.dir_first = true;
            m65xx_set_nmi(&S.cpu, M65_CLEAR);
            return st;
        }
        case 4: return S.ppu.sprite_ram[S.ppu.sprite_ram_pos];
        case 7: return nes_ppu_read(&S.ppu);
        default: return S.ppu.open_bus;
        }
    }
    if (a <= 0x4013 || a == 0x4015) return nes_apu_read(&S.apu, a);
    if (a == 0x4016) {
        const uint8_t bit = (uint8_t)((S.joy1_read >> S.joy1) & 1);
        S.joy1++;
        return (uint8_t)((S.ppu.open_bus & 0xe0) | bit);
    }
    if (a == 0x4017) {
        const uint8_t bit = (uint8_t)((S.joy2_read >> S.joy2) & 1);
        S.joy2++;
        return (uint8_t)((S.ppu.open_bus & 0xe0) | bit);
    }
    if (a <= 0x5fff) return S.ppu.open_bus;
    if (a <= 0x7fff) return read_prg_ram(a);
    return S.mem[a];
}

static void nmi_now(void)
{
    m65xx_set_nmi(&S.cpu, M65_PULSE);
    S.cpu.after_ei = true;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) {
        S.mem[a & 0x7ff] = v;
        return;
    }
    nes_ppu *p = &S.ppu;
    if (a <= 0x3fff) {
        switch (a & 7) {
        case 0:
            if ((p->status & 0x80) && !(p->control1 & 0x80) && (v & 0x80)) nmi_now();
            p->control1 = v;
            p->sprite_size = (uint8_t)(8 << ((v >> 5) & 1));
            p->pos_bg = (v >> 4) & 1;
            p->pos_spt = (v >> 3) & 1;
            p->address_temp = (uint16_t)((p->address_temp & 0x73ff) | ((v & 0x03) << 10));
            break;
        case 1:
            p->control2 = v;
            p->pal_mask = (v & 1) ? 0x30 : 0x3f;
            break;
        case 3:
            p->sprite_ram_pos = v;
            break;
        case 4:
            if (p->linea < 240 && (p->control2 & 0x18)) {
                p->sprite_ram_pos += 4;
                break;
            }
            p->sprite_ram[p->sprite_ram_pos++] = v;
            break;
        case 5:
            if (p->dir_first) {
                p->address_temp = (uint16_t)((p->address_temp & 0x7fe0) | ((v & 0xf8) >> 3));
                p->tile_x_offset = v & 7;
            } else {
                p->address_temp = (uint16_t)((p->address_temp & 0x0c1f) | ((v & 0xf8) << 2) | ((v & 7) << 12));
            }
            p->dir_first = !p->dir_first;
            break;
        case 6:
            if (p->dir_first) {
                p->address_temp = (uint16_t)((p->address_temp & 0x00ff) | ((v & 0x3f) << 8));
            } else {
                p->address_temp = (uint16_t)((p->address_temp & 0x7f00) | v);
                p->address = p->address_temp;
                if (p->address & 0x1000) line_ack(NULL, true);
            }
            p->dir_first = !p->dir_first;
            break;
        case 7:
            nes_ppu_write(p, v);
            if (p->address & 0x1000) line_ack(NULL, true);
            break;
        default:
            break;
        }
        return;
    }
    if (a <= 0x4013 || a == 0x4015 || a == 0x4017) {
        nes_apu_write(&S.apu, a, v);
        return;
    }
    if (a == 0x4014) {
        nes_ppu_dma(p, S.mem + v * 0x100);
        m65xx_steal_cycles(&S.cpu, 513 + (int)(S.cycle_count & 1));
        return;
    }
    if (a == 0x4016) {
        if (!(v & 1) && S.val_4016) S.joy1 = S.joy2 = 0;
        S.val_4016 = v & 1;
        return;
    }
    if (a <= 0x5fff) {
        write_expansion(a, v);
        return;
    }
    if (a <= 0x7fff) {
        write_prg_ram(a, v);
        return;
    }
    write_rom(a, v);
}

static void cycles(void *u, int c)
{
    (void)u;
    S.cycle_count += (uint64_t)c;
    if (S.mapper == 1) S.counter += c;
    S.apu_cycles += (uint64_t)c;
    while (S.apu_cycles >= 4) {
        S.apu_cycles -= 4;
        nes_apu_advance(&S.apu);
    }
    S.audio_acc += (uint64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        const int16_t s = nes_apu_update(&S.apu);
        if (S.naudio < 1024) S.audio[S.naudio++] = s;
    }
    const uint64_t period = (uint64_t)((double)CLOCK / NES_FPS + 0.5);
    S.frame_irq_cycles += (uint64_t)c;
    if (S.frame_irq_cycles >= period) {
        S.frame_irq_cycles -= period;
        if (S.apu.frame_irq_timer && S.apu.frame_irq && !S.mapper_irq) m65xx_set_irq(&S.cpu, M65_HOLD);
    }
}

static uint8_t dpcm_read(void *u, uint16_t a) { return rd(u, a); }

static void apu_irq(void *u)
{
    (void)u;
    if (!S.mapper_irq) m65xx_set_irq(&S.cpu, M65_HOLD);
}

bool cons_init(void)
{
    S.mem = (uint8_t *)calloc(1, 0x10000);
    S.fb = (uint16_t *)calloc(NES_W * NES_H, sizeof(uint16_t));
    if (!S.mem || !S.fb) return false;
    nes_ppu_init();
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.cycles = cycles;
    S.cpu.nes = true;
    S.ppu.line_ack = line_ack;
    S.ppu.ppu_read = ppu_read;
    S.ppu.chr_map = S.chr_map;
    S.ppu.mirror = NM_VERTICAL;
    S.apu.dpcm_read = dpcm_read;
    S.apu.irq = apu_irq;
    nes_apu_init(&S.apu);
    nes_ppu_reset(&S.ppu);
    return true;
}

void cons_reset(void)
{
    if (!S.loaded) return;
    mapper_reset();
    m65xx_reset(&S.cpu);
    nes_ppu_reset(&S.ppu);
    nes_apu_reset(&S.apu);
    S.joy1 = S.joy2 = 0;
    S.val_4016 = false;
    S.even_frame = true;
    S.mapper_irq = false;
    S.cycle_count = S.apu_cycles = S.frame_irq_cycles = S.audio_acc = 0;
    S.t_want = S.t_done = 0;
    m65xx_set_irq(&S.cpu, M65_CLEAR);
    m65xx_set_nmi(&S.cpu, M65_CLEAR);
}

static void patches(uint32_t crc, uint32_t chr_crc, int *mapper, int *sub)
{
    switch (crc) {
    case 0x3fc29044: case 0x2ed79b73: case 0x76124d08: case 0x42edbce2: case 0xacc2b74a: case 0xd8dfd3d1:
        *sub = 1;
        break;
    case 0x50f66538: S.mem[0xfffd] = 0xca; break;
    case 0x7a5cc019: S.mem[0xfb14] = S.mem[0xfb15] = 0x04; break;
    case 0x51ce0655: case 0x761e1fc9: case 0x57d8330a: case 0xe1539190:
        *mapper = 206;
        S.ppu.mirror = NM_FOUR;
        break;
    case 0x0d327f0a: *mapper = 154; break;
    case 0x4433ba0a: *mapper = 87; break;
    case 0x3c7b0120: case 0xad893bf7: case 0x2fb7d5b9: case 0x0977f982: case 0xd994d5ff: case 0xf07d31b2:
    case 0xe476313e: case 0x103f0755: case 0x63d71cda: case 0xa8a1c2eb: case 0xc8e5e815: case 0x6fdf50d0:
    case 0x154a31b6:
        *mapper = 206;
        break;
    case 0xd122ba8d: case 0x62e7aec5: case 0x6ee61da3: *mapper = 152; break;
    default: break;
    }
    if (*mapper == 243 && chr_crc != 0x282dcb3a && chr_crc != 0x331802e2) *mapper = 150;
    switch (chr_crc) {
    case 0x19c5c4aa: if (*mapper == 25) { *sub = 1; *mapper = 23; } break;
    case 0x824324fa: case 0x87c17609: case 0x3b31f998: if (*mapper == 25) *sub = 3; break;
    case 0xf82b8e59: if (*mapper == 21) *sub = 1; break;
    case 0xae17c652: case 0x23f896a7: if (*mapper == 25) *sub = 4; break;
    case 0xa30927de: case 0x7b790220: case 0xc2cf279a: case 0x88b512d6: case 0xeb9fd289:
        if (*mapper == 23) { *sub = 2; *mapper = 21; }
        break;
    case 0xbd493548: *sub = 1; break;
    case 0x7ff2dc2b: case 0x6add6cd6: case 0x1557191a: case 0x8f03a735: case 0xe8d170d8: case 0xcc06cf3e:
        if (*mapper == 33) *mapper = 48;
        break;
    case 0xf47f0bca: if (*mapper == 173) *mapper = 132; break;
    case 0x1a145504: case 0x19c33692: if (*mapper == 79) *mapper = 173; break;
    case 0x479fb8e6: *mapper = 133; break;
    default: break;
    }
}

static bool fail(uint8_t *data, const char **why, const char *msg)
{
    if (why) *why = msg;
    free(data);
    return false;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || size < 16 || memcmp(data, "NES\x1a", 4)) return fail(data, why, "not an iNES cartridge");
    const uint8_t f6 = data[6], f7 = data[7];
    size_t off = 16;
    if ((f6 & 0x04) && size < off + 0x200) return fail(data, why, "truncated iNES trainer");
    const int last_prg = data[4], last_chr = data[5];
    if (last_prg > 32 || last_prg == 0) return fail(data, why, "unsupported PRG ROM size");
    if (last_chr > 63) return fail(data, why, "unsupported CHR ROM size");
    const size_t prg_off = off + ((f6 & 0x04) ? 0x200 : 0);
    if (size < prg_off + (size_t)last_prg * 0x4000 + (size_t)last_chr * 0x2000) return fail(data, why, "truncated cartridge");
    int mapper, sub = 0;
    if ((f7 & 0x0c) == 8) {
        sub = (data[8] & 0xf0) >> 4;
        mapper = (f6 >> 4) | (f7 & 0xf0) | ((data[8] & 0x0f) << 8);
    } else if (data[13] && data[14] && data[15] && data[12]) {
        mapper = f6 >> 4;
    } else {
        mapper = (f6 >> 4) | (f7 & 0xf0);
    }
    // the new cartridge from here on
    free(S.cart);
    S.cart = data;
    S.loaded = true;
    memset(S.mem, 0, 0x10000);
    S.prg_ram_enable = false;
    if (f6 & 0x04) {
        memcpy(S.mem + 0x7000, data + off, 0x200);
        S.prg_ram_enable = true;
    }
    S.prg = data + prg_off;
    S.last_prg = last_prg;
    S.chr = S.prg + (size_t)last_prg * 0x4000;
    S.last_chr = last_chr;
    memcpy(S.mem + 0x8000, S.prg, 0x4000);
    memcpy(S.mem + 0xc000, S.prg + (last_prg == 1 ? 0 : 0x4000), 0x4000);
    memset(S.ppu.chr, 0, sizeof S.ppu.chr);
    memset(S.ppu.name_table, 0, sizeof S.ppu.name_table);
    S.ppu.write_chr = last_chr == 0;
    if (last_chr) {
        memcpy(S.ppu.chr[0], S.chr, 0x1000);
        memcpy(S.ppu.chr[1], S.chr + 0x1000, 0x1000);
    }
    S.ppu.mirror = (f6 & 0x08) ? NM_FOUR : (f6 & 0x01) ? NM_VERTICAL : NM_HORIZONTAL;
    const uint32_t crc = cons_crc32(data, size);
    uint32_t chr_crc;
    if (last_chr) {
        chr_crc = cons_crc32(S.chr, 0x2000);
    } else {
        uint8_t *z = (uint8_t *)calloc(1, 0x2000);
        chr_crc = z ? cons_crc32(z, 0x2000) : 0;
        free(z);
    }
    patches(crc, chr_crc, &mapper, &sub);
    S.mapper = mapper;
    S.submapper = sub;
    if (mapper == 13) S.ppu.write_chr = true;
    if (!mapper_known(mapper)) {
        static char msg[48];
        snprintf(msg, sizeof msg, "mapper %d is not supported", mapper);
        S.loaded = false;
        if (why) *why = msg;
        return false;
    }
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(S.cart);
    S.cart = NULL;
    S.loaded = false;
    memset(S.fb, 0, NES_W * NES_H * sizeof(uint16_t));
}

bool cons_loaded(void) { return S.loaded; }

// dsp-cpp's run_cpu(), the time kept as a whole (dsp-cpp starts every slice
// from scratch, a few % fast)
static void run_cpu(double c)
{
    S.t_want += c;
    const int n = (int)(S.t_want - S.t_done + 0.5);
    if (n > 0) S.t_done += m65xx_run(&S.cpu, n);
}

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    if (!S.loaded) {
        for (int i = 0; i < 735; i++) S.audio[i] = 0;
        return 735;
    }
    const double line_t = (double)CLOCK / NES_FPS / NES_LINES;
    const double half = 128.0 * NES_PIXEL_TIMING;
    nes_ppu *p = &S.ppu;
    for (p->linea = 0; p->linea < NES_LINES; p->linea++) {
        const int line = p->linea;
        if (line <= 239) {
            run_cpu(half);
            nes_ppu_draw_line(p, line, S.fb + line * NES_W);
            nes_ppu_end_y_coarse(p);
            run_cpu(line_t - half);
        } else if (line == 240) {
            run_cpu(line_t);
        } else if (line == 241) {
            run_cpu(NES_PIXEL_TIMING);
            p->status |= 0x80;
            if (p->control1 & 0x80) nmi_now();
            run_cpu(line_t - NES_PIXEL_TIMING);
        } else if (line <= 260) {
            run_cpu(line_t);
        } else {
            run_cpu(NES_PIXEL_TIMING);
            p->status &= 0x1f;
            p->sprite_over_flow = false;
            double rest = line_t - NES_PIXEL_TIMING;
            if (S.even_frame) rest -= NES_PIXEL_TIMING;
            S.even_frame = !S.even_frame;
            run_cpu(rest);
            if (p->control2 & 0x18) p->address = (uint16_t)((p->address & 0x41f) | (p->address_temp & 0x7be0));
            line_ack(NULL, false);
        }
    }
    p->linea = 0;
    // (keep the clocks small)
    S.t_want -= S.t_done;
    S.t_done = 0;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = NES_W;
    *h = NES_H;
    *stride = NES_W;
    return S.fb;
}

static uint8_t pack(unsigned b)
{
    uint8_t v = 0;
    if (b & CP_B1) v |= 0x01;
    if (b & CP_B2) v |= 0x02;
    if (b & (CP_SELECT | CP_COIN)) v |= 0x04;
    if (b & CP_START) v |= 0x08;
    if (b & CP_UP) v |= 0x10;
    if (b & CP_DOWN) v |= 0x20;
    if (b & CP_LEFT) v |= 0x40;
    if (b & CP_RIGHT) v |= 0x80;
    return v;
}

void cons_pad(int player, unsigned b)
{
    if (player == 0) S.joy1_read = pack(b);
    else S.joy2_read = pack(b);
}

void cons_key(int key, bool down) { (void)key, (void)down; }

const char *cons_status(void)
{
    static char s[16];
    if (!S.loaded) return NULL;
    snprintf(s, sizeof s, "mapper %d", S.mapper);
    return s;
}
