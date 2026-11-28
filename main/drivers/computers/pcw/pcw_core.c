// pcw_core.c - Amstrad PCW 8256 / 8512: C port of dsp-cpp's
// drivers/computers/pcw (dspc.h). The Z80 (z80dsp) at 3.4 MHz, 256 / 512 KB
// of banked RAM, the roller-RAM 720 x 256 bitmap, the uPD765 (zx_nec765)
// with .DSK images, the 300 Hz timer, the beeper and the joystick on the
// AY's port A. The boot program comes from the printer MCU's ROM, as in MAME.
// Includes dsp-cpp's two patches for La Abadia del Crimen.
// DSPC_MODEL: 0 = PCW8256, 1 = PCW8512.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/zx_nec765.h"
#include "../../../sound/ay8910.h"

#ifndef DSPC_MODEL
#define DSPC_MODEL 0
#endif
#define RAM_BANKS (DSPC_MODEL == 1 ? 32 : 16)
#define RAM_SIZE ((size_t)RAM_BANKS * 0x4000)
#define BORDER_W 8
#define BORDER_H 8
#define DISP_W 720
#define DISP_H 256
#define SCR_W (DISP_W + 2 * BORDER_W)
#define SCR_H (DISP_H + 2 * BORDER_H)
#define CPU_CLOCK 3400000
#define FPS 50
#define LINES 288
#define CYCLES_PER_FRAME (CPU_CLOCK / FPS)
#define RATE 44100
#define PEN_BLACK 0xff000000u
#define PEN_GREEN 0xff4aff00u

static const char *const kExts[] = { ".dsk", ".edsk", NULL };
const dspc_info dspc_machine = {
#if DSPC_MODEL == 1
    "pcw8512", "Amstrad PCW8512",
#else
    "pcw8256", "Amstrad PCW8256",
#endif
    SCR_W, SCR_H, 2, FPS, RATE, "DSK", kExts, "*.dsk *.DSK *.edsk *.EDSK", ".dsk,.edsk" };

static struct {
    zd_z80 cpu;
    zx_nec765 fdc;
    ay8910 ay;
    uint8_t ram[32 * 0x4000];
    uint8_t mcu[0x400];
    uint8_t banks[4];
    int read_bank[4], write_bank[4];
    uint8_t bank_force;
    int interrupt_counter;
    uint8_t status_bits;
    int fdc_code;
    bool timer_irq, nmi_flag;
    int timer_pulse_lines;
    unsigned roller_addr;
    unsigned short roller_offset;
    uint8_t vdu_control;
    bool beeper_on, motor, frame_50hz, in_vblank;
    uint8_t keyboard[16];
    uint8_t joy;
    int beeper_phase, timer_line_counter;
    bool blit_patched, abadia_kb_patched, abadia_ingame;
    int16_t audio[2048];
    int naudio;
    uint32_t fb[SCR_W * SCR_H];
} S;

static inline uint8_t *bank_ptr(int bank) { return S.ram + (size_t)(bank % RAM_BANKS) * 0x4000; }

static uint8_t read_byte(uint16_t address)
{
    const int bank = S.read_bank[address >> 14];
    const uint16_t off = address & 0x3fff;
    if (bank == 3 && off >= 0x3ff0) {
        const int row = off - 0x3ff0;
        return row < 11 ? S.keyboard[row] : 0xff;
    }
    if (bank == 0 && address >= 0x33d3 && address <= 0x33e6) {
        const uint16_t pc = S.cpu.pc;
        if (pc >= 0x3430 && pc <= 0x34c0) {
            int idx = address - 0x33d3;
            if (idx >= 10) idx -= 10;
            return S.keyboard[idx];
        }
    }
    return bank_ptr(bank)[off];
}

static void maybe_patch_blit_setup(void)
{
    if (S.blit_patched) return;
    if (S.read_bank[0] != 0 || S.read_bank[1] != 1) return;
    for (int i = 0; i < 4; i++)
        if (S.read_bank[i] != S.write_bank[i]) return;
    if (read_byte(0x32b3) != 0xfd || read_byte(0x32b4) != 0x21 || read_byte(0x32b5) != 0xe7 ||
        read_byte(0x32b6) != 0x33 || read_byte(0x32b7) != 0x3e || read_byte(0x32b8) != 0x09 ||
        read_byte(0x32bc) != 0xf3)
        return;
    int patched = 0;
    for (int a = 0; a <= 0xfffd; ++a) {
        if (a == 0x3311) continue;
        if (read_byte((uint16_t)a) == 0xcd && read_byte((uint16_t)(a + 1)) == 0xbc && read_byte((uint16_t)(a + 2)) == 0x32) {
            bank_ptr(S.read_bank[a >> 14])[(a & 0x3fff) + 1] = 0xb3;
            ++patched;
        }
    }
    if (patched > 0) {
        if (read_byte(0x2509) == 0xf3) bank_ptr(0)[0x2509] = 0x00;
        S.blit_patched = true;
    }
}

static void maybe_patch_abadia_keyboard(void)
{
    if (S.abadia_kb_patched) return;
    if (S.read_bank[0] != 0 || S.write_bank[0] != 0) return;
    if (read_byte(0x3427) != 0x1e || read_byte(0x3428) != 0x2f) return;
    bool found = false;
    for (int a = 0; a <= 0xfffc && !found; ++a)
        found = read_byte((uint16_t)a) == 0x3e && read_byte((uint16_t)(a + 1)) == 0x2f &&
                read_byte((uint16_t)(a + 2)) == 0xcd && read_byte((uint16_t)(a + 3)) == 0x82 &&
                read_byte((uint16_t)(a + 4)) == 0x34;
    if (!found) return;
    bank_ptr(0)[0x3427] = 0x2f;
    S.abadia_kb_patched = true;
}

static void update_mem(int block, uint8_t data)
{
    if (data & 0x80) {
        S.read_bank[block] = S.write_bank[block] = data & 0x7f;
    } else {
        static const int kMask[4] = {1 << 6, 1 << 4, 1 << 5, 1 << 7};
        S.read_bank[block] = (S.bank_force & kMask[block]) ? (data & 0x07) : ((data >> 4) & 0x07);
        S.write_bank[block] = data & 0x07;
    }
    maybe_patch_blit_setup();
    maybe_patch_abadia_keyboard();
}

static void update_irqs(void)
{
    if (nec_irq_pending(&S.fdc)) {
        S.status_bits |= 0x20;
        if (S.fdc_code == 0) S.nmi_flag = true;
    } else {
        S.status_bits &= (uint8_t)~0x20;
    }
    zd_set_nmi(&S.cpu, S.nmi_flag ? ZD_ASSERT : ZD_CLEAR);
    const bool fdc_irq = S.fdc_code == 1 && (S.status_bits & 0x20);
    zd_set_irq(&S.cpu, (fdc_irq || S.timer_irq) ? ZD_HOLD : ZD_CLEAR, 0xff);
}

static void timer_tick(void)
{
    if (S.interrupt_counter < 0x0f) ++S.interrupt_counter;
    S.timer_irq = true;
    S.timer_pulse_lines = 0;
    update_irqs();
}

static uint8_t system_status(void)
{
    uint8_t st = (uint8_t)(S.interrupt_counter & 0x0f);
    if (S.in_vblank) st |= 0x40;
    if (S.frame_50hz) st |= 0x10;
    if (S.status_bits & 0x20) st |= 0x20;
    return st;
}

static void system_control(uint8_t data)
{
    int prev;
    switch (data) {
    case 1:
        S.nmi_flag = true;
        update_irqs();
        S.nmi_flag = false;
        zd_set_nmi(&S.cpu, ZD_PULSE);
        break;
    case 2:
        prev = S.fdc_code;
        S.fdc_code = 0;
        if (prev == 1) update_irqs();
        break;
    case 3:
        prev = S.fdc_code;
        S.fdc_code = 1;
        if (prev == 0) S.nmi_flag = false;
        update_irqs();
        break;
    case 4:
        prev = S.fdc_code;
        S.fdc_code = 2;
        if (prev == 0 || prev == 1) S.nmi_flag = false;
        update_irqs();
        break;
    case 5: nec_tc_w(&S.fdc, true); update_irqs(); break;
    case 6: nec_tc_w(&S.fdc, false); break;
    case 9: S.motor = true; nec_write_motor(&S.fdc, 1); break;
    case 10: S.motor = false; nec_write_motor(&S.fdc, 0); break;
    case 11: S.beeper_on = true; break;
    case 12: S.beeper_on = false; break;
    default: break;
    }
}

static uint8_t z_read(void *u, uint16_t a) { (void)u; return read_byte(a); }
static void z_write(void *u, uint16_t a, uint8_t v) { (void)u; bank_ptr(S.write_bank[a >> 14])[a & 0x3fff] = v; }

static uint8_t z_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p <= 0x7f) return (p & 1) == 0 ? nec_read_status(&S.fdc) : nec_read_data(&S.fdc);
    if (p == 0xa9) {
        if (S.ay.latch == 14) S.ay.regs[14] = S.joy;
        return ay8910_data_r(&S.ay);
    }
    switch (p) {
    case 0xf4: {
        const uint8_t d = system_status();
        S.interrupt_counter = 0;
        S.timer_irq = false;
        update_irqs();
        return d;
    }
    case 0xf8: return system_status();
    case 0xfc: return 0xf8;
    case 0xfd: return 0xc4;
    default: return 0xff;
    }
}

static void z_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p <= 0x7f) {
        if (p & 1) {
            nec_write_data(&S.fdc, v);
            update_irqs();
        }
        return;
    }
    switch (p) {
    case 0xf0: case 0xf1: case 0xf2: case 0xf3:
        S.banks[p - 0xf0] = v;
        update_mem(p - 0xf0, v);
        break;
    case 0xf4:
        S.bank_force = v;
        for (int i = 0; i < 4; ++i) update_mem(i, S.banks[i]);
        break;
    case 0xf5: S.roller_addr = (((unsigned)v >> 5 & 7u) << 14) | (((unsigned)v & 0x1fu) << 9); break;
    case 0xf6: S.roller_offset = v; break;
    case 0xf7: S.vdu_control = v; break;
    case 0xf8: system_control(v); break;
    case 0xaa: ay8910_address_w(&S.ay, v & 0x0f); break;
    case 0xab: ay8910_data_w(&S.ay, v); break;
    default: break;
    }
}

static void z_irq_ack(void *u)
{
    (void)u;
    S.timer_irq = false;
    update_irqs();
}

static void render_screen(void)
{
    uint32_t pen0 = PEN_BLACK, pen1 = PEN_GREEN;
    if (S.vdu_control & 0x80) { pen0 = PEN_GREEN; pen1 = PEN_BLACK; }
    if ((S.vdu_control & 0x40) == 0) {
        for (int i = 0; i < SCR_W * SCR_H; i++) S.fb[i] = pen1;
        return;
    }
    for (int i = 0; i < SCR_W * BORDER_H; i++) S.fb[i] = pen0;
    for (int i = SCR_W * (BORDER_H + DISP_H); i < SCR_W * SCR_H; i++) S.fb[i] = pen0;
    unsigned roller = ((unsigned)S.roller_offset << 1) & 511u;
    for (int y = 0; y < DISP_H; ++y) {
        uint32_t *dst = S.fb + (size_t)(y + BORDER_H) * SCR_W;
        for (int x = 0; x < BORDER_W; ++x) dst[x] = dst[BORDER_W + DISP_W + x] = pen0;
        const unsigned addr = (S.roller_addr + roller) % RAM_SIZE;
        const uint16_t ld = (uint16_t)(S.ram[addr] | (S.ram[(addr + 1) % RAM_SIZE] << 8));
        unsigned lp = (((unsigned)ld & 0xe000u) << 1) + (((unsigned)ld & 0x1ff8u) << 1) + ((unsigned)ld & 0x07u);
        lp %= RAM_SIZE;
        int x = BORDER_W;
        for (int by = 0; by < 90; ++by) {
            uint8_t bits = S.ram[lp % RAM_SIZE];
            for (int b = 0; b < 8; ++b, bits = (uint8_t)(bits << 1)) dst[x + b] = (bits & 0x80) ? pen1 : pen0;
            x += 8;
            lp = (lp + 8) % RAM_SIZE;
        }
        roller = (roller + 2) & 511u;
    }
}

bool dspc_init(void)
{
    memset(&S, 0, sizeof S);
    static const char *const sets[] = {
#if DSPC_MODEL == 1
        "pcw8512",
#endif
        "pcw8256", NULL };
    uint32_t len;
    uint8_t *p = romfs_get(sets, "40026.ic701", 0xee8890ae, 0x400, &len);
    if (!p) {
        romfs_missing(sets, "40026.ic701");
        return false;
    }
    memcpy(S.mcu, p, 0x400);
    free(p);
    romfs_release();
    S.cpu.read = z_read;
    S.cpu.write = z_write;
    S.cpu.in = z_in;
    S.cpu.out = z_out;
    S.cpu.irq_ack = z_irq_ack;
    zd_init(&S.cpu);
    nec_init(&S.fdc);
    ay8910_init(&S.ay, CPU_CLOCK / 2, RATE);
    S.frame_50hz = true;
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    zd_reset(&S.cpu);
    nec_reset(&S.fdc);
    memset(S.ram, 0, sizeof S.ram);
    S.bank_force = 0xf0;
    for (int i = 0; i < 4; ++i) S.banks[i] = (uint8_t)(0x80 + i);
    S.blit_patched = S.abadia_kb_patched = S.abadia_ingame = false;
    for (int i = 0; i < 4; ++i) update_mem(i, S.banks[i]);
    S.interrupt_counter = 0;
    S.status_bits = 0;
    S.fdc_code = 2;
    S.timer_irq = S.nmi_flag = false;
    S.timer_pulse_lines = 0;
    S.roller_addr = 0;
    S.roller_offset = 0;
    S.vdu_control = 0;
    S.beeper_on = S.motor = S.in_vblank = false;
    S.timer_line_counter = 0;
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    S.naudio = 0;
    S.beeper_phase = 0;
    ay8910_reset(&S.ay);
    S.blit_patched = S.abadia_kb_patched = S.abadia_ingame = false;
    for (int x = 0; x < 256; ++x) S.ram[x + 2] = S.mcu[0x300 + x];
    zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
    zd_set_nmi(&S.cpu, ZD_CLEAR);
    for (int i = 0; i < SCR_W * SCR_H; i++) S.fb[i] = PEN_BLACK;
}

void dspc_set_inputs(const dspc_inputs *in)
{
    typedef struct { uint8_t key, mask; } kb;
    static const kb k0[] = {{DK_F3, 1}, {DK_0, 2}, {DK_F1, 4}, {DK_F9, 8}, {DK_9, 0x10}, {DK_6, 0x20}, {DK_3, 0x40}, {DK_RIGHT, 0x40}, {DK_2, 0x80}, {0, 0}};
    static const kb k1[] = {{DK_F10, 1}, {DK_BACKSLASH, 2}, {DK_F11, 4}, {DK_F12, 8}, {DK_8, 0x10}, {DK_4, 0x20}, {DK_5, 0x40}, {DK_UP, 0x40}, {DK_1, 0x80}, {DK_LEFT, 0x80}, {0, 0}};
    static const kb k2[] = {{DK_DELETE, 1}, {DK_ENTER, 4}, {DK_7, 0x10}, {DK_LSHIFT, 0x20}, {DK_RSHIFT, 0x20}, {DK_F2, 0x80}, {0, 0}};
    static const kb k3[] = {{DK_EQUALS, 1}, {DK_MINUS, 2}, {DK_AT, 4}, {DK_P, 8}, {DK_QUOTE, 0x10}, {DK_SEMICOLON, 0x20}, {DK_SLASH, 0x40}, {DK_PERIOD, 0x80}, {0, 0}};
    static const kb k4[] = {{DK_0, 1}, {DK_9, 2}, {DK_O, 4}, {DK_I, 8}, {DK_L, 0x10}, {DK_K, 0x20}, {DK_M, 0x40}, {DK_COMMA, 0x80}, {0, 0}};
    static const kb k5[] = {{DK_8, 1}, {DK_7, 2}, {DK_U, 4}, {DK_Y, 8}, {DK_H, 0x10}, {DK_J, 0x20}, {DK_N, 0x40}, {DK_SPACE, 0x80}, {0, 0}};
    static const kb k6[] = {{DK_6, 1}, {DK_5, 2}, {DK_R, 4}, {DK_T, 8}, {DK_G, 0x10}, {DK_F, 0x20}, {DK_B, 0x40}, {DK_V, 0x80}, {0, 0}};
    static const kb k7[] = {{DK_4, 1}, {DK_3, 2}, {DK_E, 4}, {DK_W, 8}, {DK_S, 0x10}, {DK_D, 0x20}, {DK_C, 0x40}, {DK_X, 0x80}, {0, 0}};
    static const kb k8[] = {{DK_1, 1}, {DK_2, 2}, {DK_ESCAPE, 4}, {DK_Q, 8}, {DK_TAB, 0x10}, {DK_A, 0x20}, {DK_CAPSLOCK, 0x40}, {DK_Z, 0x80}, {0, 0}};
    static const kb k9[] = {{DK_F5, 1}, {DK_LCTRL, 2}, {DK_RCTRL, 2}, {DK_F4, 8}, {DK_F7, 0x10}, {DK_DOWN, 0x40}, {DK_BACKSPACE, 0x80}, {0, 0}};
    static const kb k10[] = {{DK_CBM, 0x80}, {DK_RALT, 0x80}, {0, 0}};
    static const kb *const lines[11] = {k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, k10};
    const bool *k = in->keys;
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    for (int row = 0; row < 11; ++row) {
        uint8_t v = 0xff;
        for (const kb *e = lines[row]; e->mask; ++e)
            if (k[e->key]) v &= (uint8_t)~e->mask;
        S.keyboard[row] = v;
    }
    uint8_t joy = 0xff;
    if (k[DK_UP] || k[DK_Q] || in->p1.up) joy &= (uint8_t)~0x21;
    if (k[DK_DOWN] || k[DK_A] || in->p1.down) joy &= (uint8_t)~0x42;
    if (k[DK_LEFT] || k[DK_O] || in->p1.left) joy &= (uint8_t)~0x04;
    if (k[DK_RIGHT] || k[DK_P] || in->p1.right) joy &= (uint8_t)~0x08;
    if (k[DK_SPACE] || in->p1.b1 || in->p1.b2) joy &= (uint8_t)~0x90;
    S.joy = joy;
    if (S.abadia_kb_patched) {
        if (k[DK_Q] || k[DK_UP]) S.keyboard[1] &= (uint8_t)~0x40;
        if (k[DK_A] || k[DK_DOWN]) S.keyboard[9] &= (uint8_t)~0x40;
        if (k[DK_O] || k[DK_LEFT]) S.keyboard[1] &= (uint8_t)~0x80;
        if (k[DK_P] || k[DK_RIGHT]) S.keyboard[0] &= (uint8_t)~0x40;
    }
}

int dspc_run_frame(const int16_t **audio)
{
    const int cycles_per_line = CYCLES_PER_FRAME / LINES;
    int cycles_left = CYCLES_PER_FRAME;
    const int lines_per_timer = LINES / 6 > 1 ? LINES / 6 : 1;
    S.naudio = 0;
    S.in_vblank = false;
    maybe_patch_blit_setup();
    maybe_patch_abadia_keyboard();
    if (S.blit_patched && !S.abadia_ingame) {
        const uint16_t pc = S.cpu.pc;
        if (pc == 0x2517 || (pc >= 0x2518 && pc < 0x2560) || pc == 0x381e) {
            if (bank_ptr(0)[0x32b8] == 0x09) bank_ptr(0)[0x32b8] = 0x00;
            if (bank_ptr(0)[0x3309] == 0x09) bank_ptr(0)[0x3309] = 0x00;
            S.abadia_ingame = true;
        }
    }
    for (int line = 0; line < LINES; ++line) {
        S.in_vblank = line >= BORDER_H + DISP_H;
        if (S.timer_pulse_lines > 0 && --S.timer_pulse_lines == 0) {
            S.timer_irq = false;
            update_irqs();
        }
        if (++S.timer_line_counter >= lines_per_timer) {
            S.timer_line_counter = 0;
            timer_tick();
        }
        update_irqs();
        int slice = cycles_per_line;
        if (line >= BORDER_H && line < BORDER_H + DISP_H) slice = slice - 47 > 1 ? slice - 47 : 1;
        if (slice > cycles_left) slice = cycles_left;
        if (slice > 0) {
            const int ran = zd_run(&S.cpu, slice);
            cycles_left -= ran > 0 ? ran : slice;
        }
        if (S.beeper_on) {
            const int samples = (int)(((int64_t)slice * RATE) / CPU_CLOCK);
            for (int s = 0; s < samples && S.naudio < 2048; ++s) {
                S.beeper_phase = (S.beeper_phase + 1) % (RATE / 800);
                S.audio[S.naudio++] = S.beeper_phase < RATE / 1600 ? 2000 : -2000;
            }
        }
    }
    if (cycles_left > 0) zd_run(&S.cpu, cycles_left);
    S.in_vblank = true;
    render_screen();
    while (S.naudio < RATE / FPS) S.audio[S.naudio++] = 0;    // the frame's length, silent past the beeper
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint32_t *dspc_frame(void) { return S.fb; }

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    (void)name;
    const int drive = nec_disk_inserted(&S.fdc, 0) ? 1 : 0;
    if (drive == 1 && nec_disk_inserted(&S.fdc, 1)) return "PCW already has disks in A: and B:";
    const char *err = NULL;
    if (!nec_load_disk(&S.fdc, drive, data, size, &err)) return err ? err : "not a .DSK image";
    return NULL;
}
