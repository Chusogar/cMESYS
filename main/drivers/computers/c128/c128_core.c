// c128_core.c - Commodore 128 in C128 mode, 40 columns: C port of dsp-cpp's
// drivers/computers/c128 and machine/mos8722 (dspc.h). The 8502 (m65xx) at
// 985 kHz, the MOS 8722 MMU (simplified, as dsp-cpp's: CR, the page
// pointers, shared RAM; the Z80 is not run), 128 KB of RAM, the VIC-II
// (video/mos6569), the SID (sound/sid6581), two CIAs (machine/mos6526), BASIC
// 7.0, KERNAL / editor and the 8 KB character ROM (the VDC is not there).
// Media: .prg (injected once BASIC has started, then RUN typed).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../video/mos6569.h"
#include "../../../machine/mos6526.h"
#include "../../../sound/sid6581.h"

#define CPU_CLOCK 985248
#define LINES 312
#define CPL 63
#define RATE 44100
#define PRG_INJECT_FRAMES 120

static const char *const kExts[] = { ".prg", NULL };
const dspc_info dspc_machine = { "c128", "Commodore 128", VIC_W, VIC_H, 1, (double)CPU_CLOCK / (LINES * CPL), RATE,
                                 "PRG", kExts, "*.prg *.PRG", ".prg" };

enum { CR = 0, PCRA, PCRB, PCRC, PCRD, MCR, RCR, P0L, P0H, P1L, P1H, VR };

static struct {
    m65xx cpu;
    mos6569 vic;
    sid6581 sid;
    mos6526 cia1, cia2;
    uint8_t mmu[16], p0h_latch, p1h_latch;
    uint8_t *ram;                                      // 128 KB
    uint8_t basic_lo[0x4000], basic_hi[0x4000], kernal[0x4000], c64_rom[0x4000], chars[0x2000];
    uint8_t color_ram[0x800];
    uint8_t port_bits, port_val;
    bool cia_irq, vic_irq, cia_nmi;
    uint8_t keyboard[8];
    uint8_t *pending_prg;
    size_t pending_n;
    int boot_frames, debt;
    int64_t audio_acc;
    int16_t audio[2048];
    int naudio;
    uint32_t fb[VIC_W * VIC_H];
    uint8_t pens[VIC_W];
} S;

// --- the MMU (dsp-cpp's Mos8722) -----------------------------------------------
static inline bool c64_mode(void) { return (S.mmu[MCR] & 0x40) != 0; }
static inline bool io_enabled(void) { return (S.mmu[CR] & 0x01) == 0; }
static inline bool rom_lo(void) { return (S.mmu[CR] & 0x02) == 0; }
static inline uint8_t rom_mid(void) { return (S.mmu[CR] >> 2) & 3; }
static inline uint8_t rom_hi(void) { return (S.mmu[CR] >> 4) & 3; }

static void mmu_reset(void)
{
    memset(S.mmu, 0, sizeof S.mmu);
    S.mmu[P1L] = 0x01;
    S.p0h_latch = S.p1h_latch = 0;
    S.mmu[MCR] = 0x01;
}

static uint8_t mmu_read(uint16_t addr, uint8_t bus)
{
    if (c64_mode()) return bus;
    if (addr >= 0xd500 && addr < 0xd50c && io_enabled()) {
        switch (addr & 0x0f) {
        case CR: return (uint8_t)(S.mmu[CR] | 0x80);
        case MCR: return (uint8_t)((S.mmu[MCR] | 0x06) | 0x80);
        case VR: return 0x20;
        default: return S.mmu[addr & 0x0f];
        }
    }
    if (addr >= 0xff00 && addr < 0xff05) return (addr & 0x0f) == CR ? (uint8_t)(S.mmu[CR] | 0x80) : S.mmu[addr & 0x0f];
    return bus;
}

static void mmu_write(uint16_t addr, uint8_t value)
{
    if (c64_mode()) return;
    if (addr >= 0xd500 && addr < 0xd50c && io_enabled()) {
        switch (addr & 0x0f) {
        case CR: S.mmu[CR] = value & 0x7f; break;
        case PCRA: case PCRB: case PCRC: case PCRD: S.mmu[addr & 0x0f] = value & 0x7f; break;
        case MCR: S.mmu[MCR] = (uint8_t)((value & ~0x01) | 0x01); break;
        case RCR: S.mmu[RCR] = value & 0x4f; break;
        case P0L: S.mmu[P0L] = value; S.mmu[P0H] = S.p0h_latch; break;
        case P0H: S.p0h_latch = value & 0x01; break;
        case P1L: S.mmu[P1L] = value; S.mmu[P1H] = S.p1h_latch; break;
        case P1H: S.p1h_latch = value & 0x01; break;
        default: S.mmu[addr & 0x0f] = value; break;
        }
        return;
    }
    if (addr >= 0xff00 && addr < 0xff05) {
        if ((addr & 0x0f) == CR) S.mmu[CR] = value & 0x7f;
        else S.mmu[CR] = S.mmu[addr & 0x0f];
    }
}

static uint32_t translate(uint16_t addr)
{
    static const uint16_t kBottom[4] = {0x0400, 0x1000, 0x0400, 0x1000};
    static const uint16_t kTop[4] = {0xf000, 0xf000, 0xe000, 0xc000};
    int bank = (S.mmu[CR] >> 6) & 1;
    uint16_t page = (uint16_t)(addr & 0xff00);
    if (addr < 0x0100) {
        page = (uint16_t)(S.mmu[P0L] << 8);
        bank = S.mmu[P0H] & 1;
    } else if (addr < 0x0200) {
        page = (uint16_t)(S.mmu[P1L] << 8);
        bank = S.mmu[P1H] & 1;
    } else {
        if ((addr >> 8) == S.mmu[P0L]) {
            page = 0x0000;
            bank = S.mmu[P0H] & 1;
        } else if ((addr >> 8) == S.mmu[P1L]) {
            page = 0x0100;
            bank = S.mmu[P1H] & 1;
        }
        const int share = S.mmu[RCR] & 3;
        const bool bottom = (S.mmu[RCR] & 0x04) != 0, top = (S.mmu[RCR] & 0x08) != 0;
        if ((bottom && addr < kBottom[share]) || (top && addr >= kTop[share])) bank = 0;
    }
    return (uint32_t)bank * 0x10000u + (uint32_t)(page | (addr & 0x00ff));
}

// --- the machine ----------------------------------------------------------------
static void update_irq(void) { m65xx_set_irq(&S.cpu, (S.cia_irq || S.vic_irq) ? M65_ASSERT : M65_CLEAR); }
static void vic_irq(void *u, bool on) { (void)u; S.vic_irq = on; update_irq(); }
static void cia1_irq(void *u, bool on) { (void)u; S.cia_irq = on; update_irq(); }
static void cia2_irq(void *u, bool on) { (void)u; S.cia_nmi = on; m65xx_set_nmi(&S.cpu, on ? M65_ASSERT : M65_CLEAR); }

static uint8_t vic_mem(void *u, uint16_t a)
{
    (void)u;
    if ((a & 0x7000) == 0x1000) return S.chars[0x1000 + (a & 0x0fff)];
    return S.ram[translate(a) & 0x1ffff];
}

static uint8_t cia1_pb(void *u)
{
    (void)u;
    uint8_t ret = 0xff;
    const uint8_t pa = mos6526_pa(&S.cia1);
    for (int i = 0; i < 8; i++)
        if ((pa & (1 << i)) == 0) ret &= S.keyboard[i];
    return ret;
}

static uint8_t cia2_pa(void *u) { (void)u; return 0xff; }
static void cia2_pa_w(void *u, uint8_t v) { (void)u; mos6569_changed_va(&S.vic, (uint16_t)(~v & 3)); }

static uint8_t read_io(uint16_t addr)
{
    const uint16_t a = addr & 0x0fff;
    if (a < 0x400) return mos6569_read(&S.vic, (uint8_t)(addr & 0x3f));
    if (a < 0x500) return sid6581_read(&S.sid, (uint8_t)(addr & 0x1f));
    if (a < 0x50c) return mmu_read(addr, 0xff);
    if (a >= 0x600 && a < 0x700) return 0xff;
    if (a >= 0x800 && a < 0xc00) return (uint8_t)(S.color_ram[addr & 0x3ff] | 0xf0);
    if (a >= 0xc00 && a < 0xd00) return mos6526_read(&S.cia1, (uint8_t)(addr & 0x0f));
    if (a >= 0xd00 && a < 0xe00) return mos6526_read(&S.cia2, (uint8_t)(addr & 0x0f));
    return 0xff;
}

static void write_io(uint16_t addr, uint8_t value)
{
    const uint16_t a = addr & 0x0fff;
    if (a < 0x400) mos6569_write(&S.vic, (uint8_t)(addr & 0x3f), value);
    else if (a < 0x500) sid6581_write(&S.sid, (uint8_t)(addr & 0x1f), value);
    else if (a < 0x50c) mmu_write(addr, value);
    else if (a >= 0x600 && a < 0x700) return;
    else if (a >= 0x800 && a < 0xc00) S.color_ram[addr & 0x3ff] = value & 0x0f;
    else if (a >= 0xc00 && a < 0xd00) mos6526_write(&S.cia1, (uint8_t)(addr & 0x0f), value);
    else if (a >= 0xd00 && a < 0xe00) mos6526_write(&S.cia2, (uint8_t)(addr & 0x0f), value);
}

static uint8_t c_read(void *u, uint16_t addr)
{
    (void)u;
    if (addr == 0) return S.port_bits;
    if (addr == 1) return (uint8_t)((S.port_val & S.port_bits) | (0xff & (uint8_t)~S.port_bits));
    if (addr >= 0xff00 && addr < 0xff05) return mmu_read(addr, S.kernal[0x2000 + (addr & 0x1fff)]);
    if (c64_mode()) {
        if (addr >= 0xa000 && addr <= 0xbfff) return S.c64_rom[addr & 0x1fff];
        if (addr >= 0xd000 && addr <= 0xdfff) return read_io(addr);
        if (addr >= 0xe000) return S.c64_rom[0x2000 + (addr & 0x1fff)];
        return S.ram[translate(addr)];
    }
    const uint8_t hi = rom_hi(), mid = rom_mid();
    if (addr >= 0x4000 && addr <= 0x7fff) return rom_lo() ? S.basic_lo[addr & 0x3fff] : S.ram[translate(addr)];
    if (addr >= 0x8000 && addr <= 0xbfff) return mid == 0 ? S.basic_hi[addr & 0x3fff] : S.ram[translate(addr)];
    if (addr >= 0xc000 && addr <= 0xcfff) return hi == 0 ? S.kernal[addr & 0x0fff] : S.ram[translate(addr)];
    if (addr >= 0xd000 && addr <= 0xdfff) {
        if (io_enabled()) {
            const uint8_t data = read_io(addr);
            return mmu_read(addr, data);
        }
        return S.chars[0x1000 + (addr & 0x0fff)];
    }
    if (addr >= 0xe000) return hi == 0 ? S.kernal[0x2000 + (addr & 0x1fff)] : S.ram[translate(addr)];
    return S.ram[translate(addr)];
}

static void c_write(void *u, uint16_t addr, uint8_t value)
{
    (void)u;
    if (addr == 0) { S.port_bits = value; return; }
    if (addr == 1) { S.port_val = value; return; }
    if (addr >= 0xff00 && addr < 0xff05) {
        mmu_write(addr, value);
        S.ram[translate(addr)] = value;
        return;
    }
    if (addr >= 0xd000 && addr <= 0xdfff && (c64_mode() || io_enabled())) write_io(addr, value);
    S.ram[translate(addr)] = value;
}

static void c_cycles(void *u, int cycles)
{
    (void)u;
    if (cycles <= 0) return;
    mos6526_tick(&S.cia1, cycles);
    mos6526_tick(&S.cia2, cycles);
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= CPU_CLOCK) {
        S.audio_acc -= CPU_CLOCK;
        int32_t s = sid6581_update(&S.sid);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (S.naudio < 2048) S.audio[S.naudio++] = (int16_t)s;
    }
}

typedef struct { const char *name; uint32_t crc; } cand;

static bool load_first(const char *const *sets, const cand *c, int n, uint8_t *dst, const char *label)
{
    uint32_t len = 0;
    const uint32_t want = (uint32_t)(dst == S.chars ? 0x2000 : 0x4000);
    for (int i = 0; i < n; i++) {                  // by name (any CRC), then by CRC
        uint8_t *p = romfs_get(sets, c[i].name, 0, 0, &len);
        if (p && len >= want) {
            memcpy(dst, p, want);
            free(p);
            return true;
        }
        free(p);
    }
    for (int i = 0; i < n; i++) {
        uint8_t *p = romfs_get(sets, "-", c[i].crc, want, &len);
        if (p) {
            memcpy(dst, p, want);
            free(p);
            return true;
        }
    }
    romfs_missing(sets, label);
    return false;
}

static bool load_roms(void)
{
    static const char *const sets[] = { "c128", "c128p", NULL };
    static const cand kBasicLo[] = {{"318018-04.u33", 0x9f9c355b}, {"318018-02.u33", 0x2ee6e2fa}};
    static const cand kBasicHi[] = {{"318019-04.u34", 0x6e2c91a7}, {"318019-02.u34", 0xd551fce0}};
    static const cand kKernal[] = {{"318020-05.u35", 0xba456b8e}, {"318020-03.u35", 0x1e94bb02}};
    static const cand kC64[] = {{"251913-01.u32", 0x0010ec31}};
    static const cand kChar[] = {{"390059-01.u18", 0x6aaaafe6}};
    if (!load_first(sets, kBasicLo, 2, S.basic_lo, "BASIC LO (318018-04/02.u33)")) return false;
    if (!load_first(sets, kBasicHi, 2, S.basic_hi, "BASIC HI (318019-04/02.u34)")) return false;
    if (!load_first(sets, kKernal, 2, S.kernal, "KERNAL (318020-05/03.u35)")) return false;
    if (!load_first(sets, kC64, 1, S.c64_rom, "C64 ROM (251913-01.u32)")) return false;
    if (!load_first(sets, kChar, 1, S.chars, "CHAROM (390059-01.u18)")) return false;
    romfs_release();
    return true;
}

bool dspc_init(void)
{
    memset(&S, 0, sizeof S);
    S.ram = (uint8_t *)calloc(1, 0x20000);
    if (!S.ram) return false;
    S.cpu.read = c_read;
    S.cpu.write = c_write;
    S.cpu.cycles = c_cycles;
    S.vic.mem = vic_mem;
    S.vic.irq = vic_irq;
    S.vic.color_ram = S.color_ram;
    S.cia1.irq = cia1_irq;
    S.cia1.pb_in = cia1_pb;
    S.cia2.irq = cia2_irq;
    S.cia2.pa_in = cia2_pa;
    S.cia2.pa_out = cia2_pa_w;
    sid6581_init(&S.sid, CPU_CLOCK, RATE);
    if (!load_roms()) return false;
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    S.port_bits = 0x2f;
    S.port_val = 0x37;
    mmu_reset();
    mos6569_reset(&S.vic);
    sid6581_reset(&S.sid);
    mos6526_reset(&S.cia1);
    mos6526_reset(&S.cia2);
    S.cia_irq = S.vic_irq = S.cia_nmi = false;
    m65xx_set_irq(&S.cpu, M65_CLEAR);
    m65xx_set_nmi(&S.cpu, M65_CLEAR);
    S.debt = 0;
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    memset(S.color_ram, 0, sizeof S.color_ram);
    free(S.pending_prg);
    S.pending_prg = NULL;
    S.pending_n = 0;
    S.boot_frames = 0;
    S.naudio = 0;
    S.audio_acc = 0;
    for (int i = 0; i < VIC_W * VIC_H; i++) S.fb[i] = 0xff000000u | vic_palette[0];
    m65xx_reset(&S.cpu);
}

void dspc_set_inputs(const dspc_inputs *in)
{
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    const bool *k = in->keys;
#define P(col, m) (S.keyboard[col] &= (uint8_t)~(m))
    if (k[DK_BACKSPACE]) P(0, 0x01);
    if (k[DK_ENTER]) P(0, 0x02);
    if (k[DK_RIGHT] || k[DK_LEFT]) P(0, 0x04);
    if (k[DK_F7]) P(0, 0x08);
    if (k[DK_F1]) P(0, 0x10);
    if (k[DK_F3]) P(0, 0x20);
    if (k[DK_F5]) P(0, 0x40);
    if (k[DK_DOWN] || k[DK_UP]) P(0, 0x80);
    if (k[DK_LEFT] || k[DK_UP]) P(1, 0x80);
    if (k[DK_3]) P(1, 0x01);
    if (k[DK_W]) P(1, 0x02);
    if (k[DK_A]) P(1, 0x04);
    if (k[DK_4]) P(1, 0x08);
    if (k[DK_Z]) P(1, 0x10);
    if (k[DK_S]) P(1, 0x20);
    if (k[DK_E]) P(1, 0x40);
    if (k[DK_LSHIFT]) P(1, 0x80);
    if (k[DK_5]) P(2, 0x01);
    if (k[DK_R]) P(2, 0x02);
    if (k[DK_D]) P(2, 0x04);
    if (k[DK_6]) P(2, 0x08);
    if (k[DK_C]) P(2, 0x10);
    if (k[DK_F]) P(2, 0x20);
    if (k[DK_T]) P(2, 0x40);
    if (k[DK_X]) P(2, 0x80);
    if (k[DK_7]) P(3, 0x01);
    if (k[DK_Y]) P(3, 0x02);
    if (k[DK_G]) P(3, 0x04);
    if (k[DK_8]) P(3, 0x08);
    if (k[DK_B]) P(3, 0x10);
    if (k[DK_H]) P(3, 0x20);
    if (k[DK_U]) P(3, 0x40);
    if (k[DK_V]) P(3, 0x80);
    if (k[DK_9]) P(4, 0x01);
    if (k[DK_I]) P(4, 0x02);
    if (k[DK_J]) P(4, 0x04);
    if (k[DK_0]) P(4, 0x08);
    if (k[DK_M]) P(4, 0x10);
    if (k[DK_K]) P(4, 0x20);
    if (k[DK_O]) P(4, 0x40);
    if (k[DK_N]) P(4, 0x80);
    if (k[DK_EQUALS]) P(5, 0x01);
    if (k[DK_P]) P(5, 0x02);
    if (k[DK_L]) P(5, 0x04);
    if (k[DK_MINUS]) P(5, 0x08);
    if (k[DK_PERIOD]) P(5, 0x10);
    if (k[DK_COMMA]) P(5, 0x80);
    if (k[DK_ASTERISK]) P(6, 0x02);
    if (k[DK_SEMICOLON]) P(6, 0x04);
    if (k[DK_HOME]) P(6, 0x08);
    if (k[DK_RSHIFT]) P(6, 0x10);
    if (k[DK_EQUALS]) P(6, 0x20);
    if (k[DK_TAB]) P(6, 0x40);
    if (k[DK_SLASH]) P(6, 0x80);
    if (k[DK_1]) P(7, 0x01);
    if (k[DK_LCTRL]) P(7, 0x04);
    if (k[DK_2]) P(7, 0x08);
    if (k[DK_SPACE]) P(7, 0x10);
    if (k[DK_Q]) P(7, 0x40);
    if (k[DK_ESCAPE]) P(7, 0x80);
#undef P
#define JOY(p) (uint8_t)(0xff & ~(((p).up ? 0x01 : 0) | ((p).down ? 0x02 : 0) | ((p).left ? 0x04 : 0) | \
                                   ((p).right ? 0x08 : 0) | ((p).b1 ? 0x10 : 0)))
    S.cia1.joystick1 = JOY(in->p1);
    S.cia1.joystick2 = JOY(in->p2);
#undef JOY
}

static void inject_prg(const uint8_t *data, size_t size)
{
    const uint16_t addr = (uint16_t)(data[0] | (data[1] << 8));
    const size_t n = size - 2;
    for (size_t i = 0; i < n; i++) S.ram[(uint16_t)(addr + i)] = data[i + 2];
    const uint16_t end = (uint16_t)(addr + n);
    if (addr != 0x1c01) return;
    for (int o = 0x2f; o <= 0x33; o += 2) {
        S.ram[o] = (uint8_t)end;
        S.ram[o + 1] = (uint8_t)(end >> 8);
    }
    S.ram[0xae] = (uint8_t)end;
    S.ram[0xaf] = (uint8_t)(end >> 8);
    static const uint8_t kRun[] = { 'R', 'U', 'N', 0x0d };
    for (size_t i = 0; i < sizeof kRun; i++) S.ram[0x034a + i] = kRun[i];
    S.ram[0xd0] = (uint8_t)sizeof kRun;
}

int dspc_run_frame(const int16_t **audio)
{
    if (S.pending_prg && ++S.boot_frames >= PRG_INJECT_FRAMES && S.ram[0x2d] == 0x01 && S.ram[0x2e] == 0x1c) {
        uint8_t *p = S.pending_prg;
        const size_t n = S.pending_n;
        S.pending_prg = NULL;
        S.pending_n = 0;
        inject_prg(p, n);
        free(p);
    }
    S.naudio = 0;
    for (int line = 0; line < LINES; ++line) {
        int cpu_cycles = CPL;
        const int vis_y = line - 16;
        const bool shown = vis_y >= 0 && vis_y < VIC_H;
        const int stolen = mos6569_update_line(&S.vic, line, shown ? S.pens : NULL);
        if (shown) {
            uint32_t *row = S.fb + (size_t)vis_y * VIC_W;
            for (int x = 0; x < VIC_W; ++x) row[x] = 0xff000000u | vic_palette[S.pens[x]];
        }
        cpu_cycles -= stolen;
        if (cpu_cycles < 0) cpu_cycles = 0;
        if (stolen > 0) c_cycles(NULL, stolen);
        S.debt += cpu_cycles;
        if (S.debt > 0) S.debt -= m65xx_run(&S.cpu, S.debt);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint32_t *dspc_frame(void) { return S.fb; }

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    (void)name;
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
