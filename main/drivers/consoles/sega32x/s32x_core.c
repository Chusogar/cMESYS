// s32x_core.c - Sega 32X (1994): a C11 migration of dsp-cpp's
// drivers/consoles/sega32x, on the Mega Drive machine (genesis/md_machine)
// through its ext hooks. Two SH-2s (cpu/sh2) at 23 MHz with the 256 KB
// SDRAM, the two 128 KB frame buffers (packed / direct colour / run-length
// modes, the auto fill, the overwrite image), the 256-colour palette, the
// system registers (COMM, the interrupts, the 68000 -> SH-2 DREQ FIFO) and
// the PWM sound. The 32X picture is laid over the Mega Drive's (its
// priority bit, the backdrop). The BIOS (32x_g_bios.bin, 32x_m_bios.bin,
// 32x_s_bios.bin: rom_data.c) is needed.
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../genesis/md_machine.h"
#include "../../../cpu/sh2/sh2.h"
#include "rom_data.h"

#define SDRAM_SIZE 0x40000
#define FB_SIZE 0x20000
#define SLICES 8

enum { IRQ_PWM = 0, IRQ_CMD, IRQ_H, IRQ_V, IRQ_VRES };
static const int kIrqLevel[5] = { 6, 8, 10, 12, 14 };

static const char *const kExts[] = { ".32x", ".bin", NULL };
const cons_info cons_machine = {
    "sega32x", "Sega 32X", "Sega, 1994", kExts, "32X BIN", MD_VDP_W, MD_VDP_H,
    MD_FPS_NTSC, MD_RATE, { "A", "B", "C", NULL }, "START", NULL, CONS_NO_KEYS, 2, NULL,
};

typedef struct {
    sh2 cpu[2];
    const uint8_t *bios_g, *bios_m, *bios_s;
    bool bios_loaded;
    uint8_t *cart;                  // the SH-2s' view (1-4 MB, a power of two)
    uint32_t cart_size;
    uint8_t sdram[SDRAM_SIZE];
    uint8_t dram[2][FB_SIZE];
    uint16_t palette[256];
    bool aden, sh2_running, fm, rv;
    uint16_t int_ctl, bank;
    uint16_t int_mask[2];
    uint16_t hcount;
    int hint_counter;
    uint16_t pending[2];
    uint16_t comm[8];
    uint16_t sega_tv;
    uint16_t dreq_ctl;
    uint32_t dreq_src, dreq_dst;
    uint16_t dreq_len;
    uint32_t dreq_left;
    uint16_t fifo[8];
    int fifo_head, fifo_count;
    uint16_t bitmap_mode, shift, fill_len, fill_addr, fill_data;
    int fs, fs_pending;
    bool vblank, hblank;
    uint16_t pwm_ctl, pwm_cycle;
    uint16_t pwm_fifo[2][3];
    int pwm_count[2], pwm_out[2];
    int pwm_acc, pwm_tm;
    int64_t pwm_dc, sh2_debt;
    uint16_t *fb;
} s32x_state;
static s32x_state *Xp;              // (on the heap: the RP2350 puts it in the PSRAM)
#define X (*Xp)

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint16_t merge(uint16_t o, uint16_t v, uint16_t m) { return (uint16_t)((o & ~m) | (v & m)); }

static inline uint16_t rgb555(uint16_t c)
{
    const uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return cons_rgb565(((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31));
}

// ---- interrupts, reset, FIFO ----
static void update_irls(void)
{
    for (int cpu = 0; cpu < 2; cpu++) {
        const uint16_t active = (uint16_t)(X.pending[cpu] & ((X.int_mask[cpu] & 0x0f) | 0x10));
        int level = 0;
        for (int bit = 4; bit >= 0; bit--)
            if (active & (1 << bit)) {
                level = kIrqLevel[bit];
                break;
            }
        sh2_set_irl(&X.cpu[cpu], level);
    }
}

static void raise_irq(int bit)
{
    for (int cpu = 0; cpu < 2; cpu++) X.pending[cpu] = (uint16_t)(X.pending[cpu] | (1 << bit));
    update_irls();
}

static void sh2_reset_line(bool released)
{
    if (released && !X.sh2_running && X.bios_loaded) {
        sh2_reset(&X.cpu[0]);
        sh2_reset(&X.cpu[1]);
        X.sh2_running = true;
    } else if (!released) {
        X.sh2_running = false;
    }
}

static void fifo_push(uint16_t v)
{
    if ((X.dreq_ctl & 4) == 0 || X.fifo_count >= 8) return;
    X.fifo[(X.fifo_head + X.fifo_count) & 7] = v;
    X.fifo_count++;
    if (X.sh2_running) sh2_dma_request(&X.cpu[0], 0);
}

static uint16_t fifo_pop(void)
{
    if (X.fifo_count == 0) return 0;
    const uint16_t v = X.fifo[X.fifo_head];
    X.fifo_head = (X.fifo_head + 1) & 7;
    X.fifo_count--;
    if (X.dreq_left > 0 && --X.dreq_left == 0) X.dreq_ctl = (uint16_t)(X.dreq_ctl & ~4);
    return v;
}

// ---- PWM ----
static uint16_t pwm_status(int ch)
{
    uint16_t s = 0;
    if (X.pwm_count[ch] >= 3) s |= 0x8000;
    if (X.pwm_count[ch] == 0) s |= 0x4000;
    return s;
}

static uint16_t pwm_read(uint32_t o)
{
    switch (o & 0xe) {
    case 0x0: return X.pwm_ctl;
    case 0x2: return X.pwm_cycle;
    case 0x4: return pwm_status(0);
    case 0x6: return pwm_status(1);
    case 0x8: return (uint16_t)(pwm_status(0) & pwm_status(1));
    default: return 0;
    }
}

static void pwm_push(int ch, uint16_t value)
{
    uint16_t *f = X.pwm_fifo[ch];
    int *n = &X.pwm_count[ch];
    if (*n < 3) f[(*n)++] = (uint16_t)(value & 0xfff);
    else f[2] = (uint16_t)(value & 0xfff);
}

static void pwm_write(uint32_t o, uint16_t v, uint16_t mask)
{
    switch (o & 0xe) {
    case 0x0:
        X.pwm_ctl = (uint16_t)(merge(X.pwm_ctl, v, mask) & 0x0f8f);
        X.pwm_tm = 0;
        return;
    case 0x2: X.pwm_cycle = (uint16_t)(merge(X.pwm_cycle, v, mask) & 0xfff); return;
    case 0x4: pwm_push(0, v); return;
    case 0x6: pwm_push(1, v); return;
    case 0x8: pwm_push(0, v); pwm_push(1, v); return;
    default: return;
    }
}

static void pwm_advance(int sh2_cycles)
{
    // both channels off: the timer is stopped and raises no interrupts
    if ((X.pwm_ctl & 0x0f) == 0) return;
    const int period = (int)((X.pwm_cycle - 1) & 0xfff);
    if (period < 16) return;
    X.pwm_acc += sh2_cycles;
    while (X.pwm_acc >= period) {
        X.pwm_acc -= period;
        for (int ch = 0; ch < 2; ch++) {
            if (X.pwm_count[ch] == 0) continue;
            uint16_t *f = X.pwm_fifo[ch];
            X.pwm_out[ch] = f[0];
            f[0] = f[1];
            f[1] = f[2];
            X.pwm_count[ch]--;
        }
        if (--X.pwm_tm <= 0) {
            const int tm = (X.pwm_ctl >> 8) & 15;
            X.pwm_tm = tm ? tm : 16;
            raise_irq(IRQ_PWM);
        }
    }
}

static int32_t ext_audio(void)
{
    const int period = (int)((X.pwm_cycle - 1) & 0xfff);
    int32_t sum = 0;
    if (period >= 16) {
        if (X.pwm_ctl & 3) sum += X.pwm_out[0] < period ? X.pwm_out[0] : period;
        if (X.pwm_ctl & 0xc) sum += X.pwm_out[1] < period ? X.pwm_out[1] : period;
        sum = sum * 6000 / period;
    }
    X.pwm_dc += (sum * 1024 - X.pwm_dc) >> 9;     // a one-pole high-pass (the DC level)
    return sum - (int32_t)(X.pwm_dc >> 10);
}

// ---- registers ----
static uint16_t sys_read(int cpu, uint32_t o)
{
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) return X.comm[(o - 0x20) >> 1];
    if (o >= 0x30) return pwm_read(o);
    switch (o) {
    case 0x00: return (uint16_t)((X.fm ? 0x8000 : 0) | (X.aden ? 0x0200 : 0) | X.int_mask[cpu]);
    case 0x04: return X.hcount;
    case 0x06: return (uint16_t)((X.fifo_count >= 8 ? 0x4000 : 0) | (X.dreq_ctl & 6) | (X.rv ? 1 : 0));
    case 0x08: return (uint16_t)((X.dreq_src >> 16) & 0xff);
    case 0x0a: return (uint16_t)X.dreq_src;
    case 0x0c: return (uint16_t)((X.dreq_dst >> 16) & 0xff);
    case 0x0e: return (uint16_t)X.dreq_dst;
    case 0x10: return X.dreq_len;
    case 0x12: return fifo_pop();
    default: return 0;
    }
}

static void sys_write(int cpu, uint32_t o, uint16_t v, uint16_t mask)
{
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) {
        X.comm[(o - 0x20) >> 1] = merge(X.comm[(o - 0x20) >> 1], v, mask);
        return;
    }
    if (o >= 0x30) {
        pwm_write(o, v, mask);
        return;
    }
    switch (o) {
    case 0x00:
        if (mask & 0xff00) X.fm = (v & 0x8000) != 0;
        if (mask & 0x00ff) X.int_mask[cpu] = (uint16_t)(v & 0x8f);
        update_irls();
        return;
    case 0x04:
        if (mask & 0x00ff) X.hcount = (uint16_t)(v & 0xff);
        return;
    case 0x14: case 0x16: case 0x18: case 0x1a: case 0x1c: {
        static const int kBit[5] = { IRQ_VRES, IRQ_V, IRQ_H, IRQ_CMD, IRQ_PWM };
        const int bit = kBit[(o - 0x14) >> 1];
        X.pending[cpu] = (uint16_t)(X.pending[cpu] & ~(1 << bit));
        if (bit == IRQ_CMD) X.int_ctl = (uint16_t)(X.int_ctl & ~(1 << cpu));
        update_irls();
        return;
    }
    default: return;
    }
}

static uint16_t m68k_reg_read(uint32_t o)
{
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) return X.comm[(o - 0x20) >> 1];
    if (o >= 0x30) return pwm_read(o);
    switch (o) {
    case 0x00: return (uint16_t)((X.fm ? 0x8000 : 0) | 0x0080 | (X.sh2_running ? 0x0002 : 0) | (X.aden ? 0x0001 : 0));
    case 0x02: return X.int_ctl;
    case 0x04: return X.bank;
    case 0x06: return (uint16_t)((X.fifo_count >= 8 ? 0x0080 : 0) | (X.dreq_ctl & 6) | (X.rv ? 1 : 0));
    case 0x08: return (uint16_t)((X.dreq_src >> 16) & 0xff);
    case 0x0a: return (uint16_t)X.dreq_src;
    case 0x0c: return (uint16_t)((X.dreq_dst >> 16) & 0xff);
    case 0x0e: return (uint16_t)X.dreq_dst;
    case 0x10: return X.dreq_len;
    case 0x1a: return X.sega_tv;
    default: return 0;
    }
}

static void m68k_reg_write(uint32_t o, uint16_t v, uint16_t mask)
{
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) {
        X.comm[(o - 0x20) >> 1] = merge(X.comm[(o - 0x20) >> 1], v, mask);
        return;
    }
    if (o >= 0x30) {
        pwm_write(o, v, mask);
        return;
    }
    switch (o) {
    case 0x00:
        if (mask & 0xff00) X.fm = (v & 0x8000) != 0;
        if (mask & 0x00ff) {
            if (v & 1) X.aden = true;      // only a reset clears ADEN
            sh2_reset_line((v & 2) != 0);
        }
        return;
    case 0x02:
        if (mask & 0x00ff) {
            X.int_ctl = (uint16_t)(v & 3);
            for (int cpu = 0; cpu < 2; cpu++)
                if (v & (1 << cpu)) X.pending[cpu] = (uint16_t)(X.pending[cpu] | (1 << IRQ_CMD));
            update_irls();
        }
        return;
    case 0x04:
        if (mask & 0x00ff) X.bank = (uint16_t)(v & 3);
        return;
    case 0x06:
        if (mask & 0x00ff) {
            X.rv = (v & 1) != 0;
            const bool start = (v & 4) != 0 && (X.dreq_ctl & 4) == 0;
            X.dreq_ctl = (uint16_t)(v & 6);
            if (start) {
                X.fifo_head = X.fifo_count = 0;
                X.dreq_left = X.dreq_len;
            }
            if ((v & 4) == 0) X.fifo_head = X.fifo_count = 0;
        }
        return;
    case 0x08: X.dreq_src = (X.dreq_src & 0xffff) | ((uint32_t)(v & 0xff) << 16); return;
    case 0x0a: X.dreq_src = (X.dreq_src & 0xff0000) | (v & 0xfffe); return;
    case 0x0c: X.dreq_dst = (X.dreq_dst & 0xffff) | ((uint32_t)(v & 0xff) << 16); return;
    case 0x0e: X.dreq_dst = (X.dreq_dst & 0xff0000) | v; return;
    case 0x10: X.dreq_len = (uint16_t)(v & 0xfffc); return;
    case 0x12: fifo_push(v); return;
    case 0x1a: X.sega_tv = (uint16_t)(v & 1); return;
    default: return;
    }
}

static uint16_t vdp_read(uint32_t o)
{
    switch (o & 0xe) {
    case 0x0: return (uint16_t)((MD.pal ? 0 : 0x8000) | X.bitmap_mode);
    case 0x2: return X.shift;
    case 0x4: return X.fill_len;
    case 0x6: return X.fill_addr;
    case 0x8: return X.fill_data;
    default: {
        const bool pen = X.vblank || X.hblank || (X.bitmap_mode & 3) == 0;
        return (uint16_t)((X.vblank ? 0x8000 : 0) | (X.hblank ? 0x4000 : 0) | (pen ? 0x2000 : 0) | X.fs);
    }
    }
}

static void vdp_write(uint32_t o, uint16_t v, uint16_t mask)
{
    switch (o & 0xe) {
    case 0x0: X.bitmap_mode = (uint16_t)(merge(X.bitmap_mode, v, mask) & 0xc3); return;
    case 0x2: X.shift = (uint16_t)(merge(X.shift, v, mask) & 1); return;
    case 0x4: X.fill_len = (uint16_t)(merge(X.fill_len, v, mask) & 0xff); return;
    case 0x6: X.fill_addr = merge(X.fill_addr, v, mask); return;
    case 0x8: {
        X.fill_data = merge(X.fill_data, v, mask);
        // the auto fill: len + 1 words, the address wraps in its 256-word row
        uint8_t *d = X.dram[X.fs ^ 1];
        uint16_t addr = X.fill_addr;
        for (int i = 0; i <= X.fill_len; i++) {
            d[((uint32_t)addr * 2) & 0x1fffe] = (uint8_t)(X.fill_data >> 8);
            d[(((uint32_t)addr * 2) & 0x1fffe) + 1] = (uint8_t)X.fill_data;
            addr = (uint16_t)((addr & 0xff00) | ((addr + 1) & 0xff));
        }
        X.fill_addr = addr;
        return;
    }
    default:
        if (mask & 0x00ff) {
            const int fs = v & 1;
            if (X.vblank || (X.bitmap_mode & 3) == 0) {
                X.fs = fs;
                X.fs_pending = -1;
            } else {
                X.fs_pending = fs;
            }
        }
        return;
    }
}

// ---- the frame buffers ----
static uint16_t fb_read16(uint32_t off) { return be16(X.dram[X.fs ^ 1] + (off & 0x1fffe)); }

static void fb_write16(uint32_t off, uint16_t v, uint16_t mask, bool overwrite)
{
    uint8_t *d = X.dram[X.fs ^ 1] + (off & 0x1fffe);
    // the overwrite image: zero bytes leave the frame buffer as it is
    if ((mask & 0xff00) && !(overwrite && (v & 0xff00) == 0)) d[0] = (uint8_t)(v >> 8);
    if ((mask & 0x00ff) && !(overwrite && (v & 0x00ff) == 0)) d[1] = (uint8_t)v;
}

// ---- the SH-2s' address space (bits 28-0) ----
static uint16_t sh2_read16(int cpu, uint32_t a)
{
    a &= 0x1ffffffe;
    if (a < 0x4000) {
        const uint8_t *bios = cpu == 0 ? X.bios_m : X.bios_s;
        const uint32_t size = cpu == 0 ? 0x800 : 0x400;
        if (!bios) return 0xffff;
        return be16(&bios[a & (size - 1)]);
    }
    if (a < 0x4100) return sys_read(cpu, a & 0xff);
    if (a < 0x4200) return vdp_read(a & 0xf);
    if (a < 0x4400) return X.palette[(a & 0x1ff) >> 1];
    const uint32_t page = a >> 24;
    if (page == 2) return X.cart ? be16(&X.cart[a & (X.cart_size - 1)]) : 0xffff;
    if (page == 4) return fb_read16(a & 0x1ffff);
    if (page == 6) return be16(&X.sdram[a & (SDRAM_SIZE - 1)]);
    return 0;
}

static void sh2_write16(int cpu, uint32_t a, uint16_t v)
{
    a &= 0x1ffffffe;
    if (a < 0x4000) return;
    if (a < 0x4100) { sys_write(cpu, a & 0xff, v, 0xffff); return; }
    if (a < 0x4200) { vdp_write(a & 0xf, v, 0xffff); return; }
    if (a < 0x4400) { X.palette[(a & 0x1ff) >> 1] = v; return; }
    const uint32_t page = a >> 24;
    if (page == 4) { fb_write16(a & 0x1ffff, v, 0xffff, (a & 0x20000) != 0); return; }
    if (page == 6) {
        X.sdram[a & (SDRAM_SIZE - 1)] = (uint8_t)(v >> 8);
        X.sdram[(a & (SDRAM_SIZE - 1)) + 1] = (uint8_t)v;
    }
}

static void sh2_write8(int cpu, uint32_t a, uint8_t v)
{
    a &= 0x1fffffff;
    const uint16_t mask = (a & 1) ? 0x00ff : 0xff00;
    const uint16_t w = (a & 1) ? v : (uint16_t)(v << 8);
    const uint32_t even = a & ~1u;
    if (even < 0x4000) return;
    if (even < 0x4100) { sys_write(cpu, even & 0xff, w, mask); return; }
    if (even < 0x4200) { vdp_write(even & 0xf, w, mask); return; }
    if (even < 0x4400) {
        X.palette[(even & 0x1ff) >> 1] = merge(X.palette[(even & 0x1ff) >> 1], w, mask);
        return;
    }
    const uint32_t page = a >> 24;
    if (page == 4) { fb_write16(even & 0x1ffff, w, mask, (a & 0x20000) != 0); return; }
    if (page == 6) X.sdram[a & (SDRAM_SIZE - 1)] = v;
}

static int cpu_of(void *u) { return u == &X.cpu[1]; }
static uint8_t b_read8(void *u, uint32_t a)
{
    const uint16_t w = sh2_read16(cpu_of(u), a);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static uint16_t b_read16(void *u, uint32_t a) { return sh2_read16(cpu_of(u), a); }
static uint32_t b_read32(void *u, uint32_t a)
{
    const uint32_t hi = sh2_read16(cpu_of(u), a);
    return (hi << 16) | sh2_read16(cpu_of(u), a + 2);
}
static void b_write8(void *u, uint32_t a, uint8_t v) { sh2_write8(cpu_of(u), a, v); }
static void b_write16(void *u, uint32_t a, uint16_t v) { sh2_write16(cpu_of(u), a, v); }
static void b_write32(void *u, uint32_t a, uint32_t v)
{
    sh2_write16(cpu_of(u), a, (uint16_t)(v >> 16));
    sh2_write16(cpu_of(u), a + 2, (uint16_t)v);
}
static bool b_dreq(void *u, int channel)
{
    if (cpu_of(u) != 0) return false;
    return channel == 0 && X.fifo_count > 0 && (X.dreq_ctl & 4) != 0;
}

// ---- the 68000's side ----
static uint16_t rom16(uint32_t off) { return off + 1 < MD.rom_size ? be16(&MD.rom[off]) : 0xffff; }

static bool ext_read16(uint32_t a, uint16_t *value)
{
    if (a >= 0xa15100 && a < 0xa15400) {
        if (a < 0xa15180) *value = m68k_reg_read(a - 0xa15100);
        else if (a < 0xa15200) *value = vdp_read(a & 0xf);
        else *value = X.palette[(a & 0x1ff) >> 1];
        return true;
    }
    if (a == 0xa130ec) { *value = 0x4d41; return true; }    // "MA"
    if (a == 0xa130ee) { *value = 0x5253; return true; }    // "RS"
    if (!X.aden) return false;
    if (a < 0x100 && !X.rv) {
        *value = X.bios_g ? be16(&X.bios_g[a]) : 0xffff;
        return true;
    }
    if (a >= 0x840000 && a < 0x880000) { *value = fb_read16(a & 0x1ffff); return true; }
    if (a >= 0x880000 && a < 0x900000) { *value = rom16(a - 0x880000); return true; }
    if (a >= 0x900000 && a < 0xa00000) { *value = rom16((uint32_t)X.bank * 0x100000 + (a - 0x900000)); return true; }
    return false;
}

static bool ext_read8(uint32_t a, uint8_t *value)
{
    const bool mine = (a >= 0xa15100 && a < 0xa15400) || (a & ~3u) == 0xa130ec ||
                      (X.aden && ((a < 0x100 && !X.rv) || (a >= 0x840000 && a < 0xa00000)));
    if (!mine) return false;
    uint16_t w = 0;
    ext_read16(a & ~1u, &w);
    *value = (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    return true;
}

static bool ext_write16(uint32_t a, uint16_t v)
{
    if (a >= 0xa15100 && a < 0xa15400) {
        if (a < 0xa15180) m68k_reg_write(a - 0xa15100, v, 0xffff);
        else if (a < 0xa15200) vdp_write(a & 0xf, v, 0xffff);
        else X.palette[(a & 0x1ff) >> 1] = v;
        return true;
    }
    if (!X.aden) return false;
    if (a < 0x100 && !X.rv) return true;
    if (a >= 0x840000 && a < 0x880000) {
        fb_write16(a & 0x1ffff, v, 0xffff, a >= 0x860000);
        return true;
    }
    return a >= 0x880000 && a < 0xa00000;
}

static bool ext_write8(uint32_t a, uint8_t v)
{
    const uint16_t mask = (a & 1) ? 0x00ff : 0xff00;
    const uint16_t w = (a & 1) ? v : (uint16_t)(v << 8);
    const uint32_t even = a & ~1u;
    if (even >= 0xa15100 && even < 0xa15400) {
        if (even < 0xa15180) m68k_reg_write(even - 0xa15100, w, mask);
        else if (even < 0xa15200) vdp_write(even & 0xf, w, mask);
        else X.palette[(even & 0x1ff) >> 1] = merge(X.palette[(even & 0x1ff) >> 1], w, mask);
        return true;
    }
    if (!X.aden) return false;
    if (even < 0x100 && !X.rv) return true;
    if (even >= 0x840000 && even < 0x880000) {
        fb_write16(even & 0x1ffff, w, mask, even >= 0x860000);
        return true;
    }
    return even >= 0x880000 && even < 0xa00000;
}

// ---- reset ----
static void ext_reset(void)
{
    sh2 c0 = X.cpu[0], c1 = X.cpu[1];
    uint8_t *cart = X.cart;
    const uint32_t cart_size = X.cart_size;
    const uint8_t *g = X.bios_g, *m = X.bios_m, *s = X.bios_s;
    const bool bl = X.bios_loaded;
    uint16_t *fb = X.fb;
    memset(&X, 0, sizeof X);
    X.cpu[0] = c0;
    X.cpu[1] = c1;
    X.cart = cart;
    X.cart_size = cart_size;
    X.bios_g = g, X.bios_m = m, X.bios_s = s;
    X.bios_loaded = bl;
    X.fb = fb;
    X.fs_pending = -1;
    sh2_set_irl(&X.cpu[0], 0);
    sh2_set_irl(&X.cpu[1], 0);
}

// ---- the picture ----
static void render_32x_line(int line, uint16_t *out, const uint8_t *backdrop)
{
    const int mode = X.bitmap_mode & 3;
    if (mode == 0) return;
    const uint8_t *d = X.dram[X.fs];
    const uint32_t lt = be16(d + ((uint32_t)line * 2 & 0x1ff));
    // PRI = 0: the Mega Drive in front; the 32X shows on its backdrop and
    // wherever a 32X pixel has its priority bit. PRI = 1: the 32X in front,
    // only the pixels with the bit set opaque.
    const bool pri = (X.bitmap_mode & 0x80) != 0;
#define PUT(x, c) do { const uint16_t c_ = (c); if ((c_ & 0x8000) || (!pri && backdrop[x])) out[x] = rgb555(c_); } while (0)
    if (mode == 1) {                        // packed pixels
        const uint32_t base = lt * 2 + (X.shift & 1);
        for (int x = 0; x < 320; x++) PUT(x, X.palette[d[(base + (uint32_t)x) & 0x1ffff]]);
    } else if (mode == 2) {                 // direct colour
        for (int x = 0; x < 320; x++) PUT(x, be16(d + ((lt + (uint32_t)x) * 2 & 0x1fffe)));
    } else {                                // run length: colour, length - 1
        uint32_t addr = lt * 2;
        int x = 0;
        while (x < 320) {
            const uint16_t w = be16(d + (addr & 0x1fffe));
            addr += 2;
            const uint16_t c = X.palette[w >> 8];
            for (int i = 0; i <= (w & 0xff) && x < 320; i++) {
                PUT(x, c);
                x++;
            }
        }
    }
#undef PUT
}

static int run_frame(void)
{
    MD.naudio = 0;
    const int lines = MD.vdp.total_scanlines;
    const double fps = md_fps();
    int m_line = (int)((double)md_m68k_clock() / fps / lines);
    int z_line = (int)((double)((MD.pal ? MD_MASTER_PAL : MD_MASTER_NTSC) / 15) / fps / lines);
    if (m_line < 1) m_line = 1;
    if (z_line < 1) z_line = 1;
    const double sh2_line = (double)((uint64_t)(MD.pal ? MD_MASTER_PAL : MD_MASTER_NTSC) * 3 / 7) / fps / lines;
    const int height = md_vdp_height(&MD.vdp);
    for (int line = 0; line < lines; line++) {
        MD.cycles_on_line = 0;
        md_vdp_set_hpos(&MD.vdp, 0);
        md_vdp_scanline(&MD.vdp, line);
        if (line < height && line < MD_VDP_H) {
            uint16_t *row = X.fb + line * MD_VDP_W;
            memcpy(row, MD.vdp.line, MD_VDP_W * sizeof(uint16_t));
            if (X.aden) render_32x_line(line, row, MD.vdp.line_backdrop);
        }
        if (line == 0) {
            X.vblank = false;
            X.hint_counter = X.hcount;
        }
        if (line == height) {
            X.vblank = true;
            if (X.fs_pending >= 0) {
                X.fs = X.fs_pending;
                X.fs_pending = -1;
            }
            raise_irq(IRQ_V);
        }
        const bool hen = ((X.int_mask[0] | X.int_mask[1]) & 0x80) != 0;
        if (line < height || hen) {
            if (X.hint_counter <= 0) {
                X.hint_counter = X.hcount;
                raise_irq(IRQ_H);
            } else {
                X.hint_counter--;
            }
        }
        for (int s = 0; s < SLICES; s++) {
            X.hblank = s == SLICES - 1;
            md_run_m68k(m_line / SLICES);
            X.sh2_debt += (int64_t)(sh2_line * 1024.0 / SLICES);
            const int sh2_cycles = (int)(X.sh2_debt >> 10);
            X.sh2_debt -= (int64_t)sh2_cycles << 10;
            if (X.sh2_running) {
                sh2_run(&X.cpu[0], sh2_cycles);
                sh2_run(&X.cpu[1], sh2_cycles);
            }
            pwm_advance(sh2_cycles);
        }
        X.hblank = false;
        md_run_z80(z_line);
    }
    md_vdp_eof(&MD.vdp);
    return MD.naudio;
}

// ---- the console API ----
bool cons_init(void)
{
    Xp = (s32x_state *)calloc(1, sizeof *Xp);
    if (!Xp || !md_alloc()) return false;
    X.fb = (uint16_t *)calloc(MD_VDP_W * MD_VDP_H, sizeof(uint16_t));
    if (!X.fb) return false;
    X.bios_g = s32x_rom_bios_g;
    X.bios_m = s32x_rom_bios_m;
    X.bios_s = s32x_rom_bios_s;
    X.bios_loaded = X.bios_g && X.bios_m && X.bios_s;
    for (int c = 0; c < 2; c++) {
        X.cpu[c].bus = (sh2_bus){ b_read8, b_read16, b_read32, b_write8, b_write16, b_write32, b_dreq, &X.cpu[c] };
        sh2_map_memory(&X.cpu[c], 6, X.sdram, SDRAM_SIZE, true);
    }
    MD.ext_read16 = ext_read16;
    MD.ext_write16 = ext_write16;
    MD.ext_read8 = ext_read8;
    MD.ext_write8 = ext_write8;
    MD.ext_audio = ext_audio;
    MD.ext_reset = ext_reset;
    md_init(MD_USA);
    md_reset();
    return true;
}

void cons_reset(void)
{
    memset(X.fb, 0, MD_VDP_W * MD_VDP_H * sizeof(uint16_t));
    md_reset();
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!X.bios_loaded) {
        if (why) *why = "the 32X BIOS is not built in (tools/regen.sh sega32x)";
        free(data);
        return false;
    }
    int region = MD_USA;
    if (data && size > 0x1f3) {
        const char *r = (const char *)data + 0x1f0;
        if (memchr(r, 'U', 3) || r[0] == '4') region = MD_USA;
        else if (memchr(r, 'J', 3) || r[0] == '1') region = MD_JAPAN;
        else if (memchr(r, 'E', 3) || r[0] == '8') region = MD_EUROPE;
    }
    md_set_region(region);
    if (!md_load(data, size, why)) return false;
    // the SH-2s' view of the cartridge: a 1-4 MB window, mirrored
    uint32_t sz = 0x100000;
    while (sz < MD.rom_size && sz < 0x400000) sz <<= 1;
    free(X.cart);
    X.cart = (uint8_t *)malloc(sz);
    if (!X.cart) {
        if (why) *why = "out of memory";
        md_eject();
        return false;
    }
    memset(X.cart, 0xff, sz);
    memcpy(X.cart, MD.rom, MD.rom_size < sz ? MD.rom_size : sz);
    X.cart_size = sz;
    for (int c = 0; c < 2; c++) sh2_map_memory(&X.cpu[c], 2, X.cart, sz, false);
    md_reset();
    return true;
}

void cons_eject(void)
{
    md_eject();
    free(X.cart);
    X.cart = NULL;
    X.cart_size = 0;
}

bool cons_loaded(void) { return MD.loaded; }

int cons_run_frame(const int16_t **audio)
{
    *audio = MD.audio;
    if (!MD.loaded) {
        MD.naudio = (int)(MD_RATE / md_fps());
        memset(MD.audio, 0, MD.naudio * sizeof(int16_t));
        return MD.naudio;
    }
    return run_frame();
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = MD_VDP_W;
    *h = md_vdp_height(&MD.vdp);
    *stride = MD_VDP_W;
    return X.fb;
}

void cons_pad(int player, unsigned bits)
{
    if (player < 0 || player > 1) return;
    MD.pad[player] = bits;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return MD.pal ? "PAL 50 Hz" : NULL; }
