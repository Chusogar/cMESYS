// taito68705.c - Taito MC68705P5 MCU hookup. See taito68705.h.
// Follows MAME's m68705p_device (ports, DDRs, open-drain port A, 4-bit
// port C, unmapped reads high) and taito68705_mcu_device (latches, flags).
#include "taito68705.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define T68_HOT IRAM_ATTR
#else
#define T68_HOT
#endif

static const uint8_t port_mask[3] = {0x00, 0x00, 0xf0};    // port C is 4 bits wide

static inline uint8_t pa_value(const taito68705 *t)
{
    return t->pa_output & (t->latch_driven ? t->host_latch : 0xff);
}

static inline void set_irq(taito68705 *t)
{
    m6805_set_int(&t->cpu, t->host_flag);
}

// latch_control(data, pb_output, host bit 1, mcu bit 2)
static void portb_w(taito68705 *t, uint8_t data)
{
    const uint8_t old_pa = pa_value(t);
    if (data & 0x02) {
        t->latch_driven = false;
        t->input[0] = 0xff;
        if (!(t->pb_output & 0x02)) {
            t->host_flag = false;
            set_irq(t);
        }
    } else {
        t->latch_driven = true;
        t->input[0] = t->host_latch;
    }
    if (!(data & 0x04)) {
        if (!t->reset_input) t->mcu_flag = true;
        if (t->pb_output & 0x04) t->mcu_latch = old_pa;
    }
    t->pb_output = data;
}

// port N output callback (MAME port_cb_w): A is open drain
static void port_cb(taito68705 *t, int n)
{
    if (n == 0) t->pa_output = (uint8_t)(t->latch[0] | ~t->ddr[0]);
    else if (n == 1) portb_w(t, t->latch[1]);
}

static uint8_t T68_HOT mcu_read(void *u, uint16_t a)
{
    taito68705 *t = (taito68705 *)u;
    if (a >= 0x80) return t->rom[a];
    if (a >= 0x10) return t->ram[a - 0x10];
    switch (a) {
    case 0x00: case 0x01: case 0x02: {
        const int n = a;
        if (n == 2)     // port C: the flags
            t->input[2] = (uint8_t)(((t->host_flag ? 0x01 : 0x00) | (t->mcu_flag ? 0x00 : 0x02) | 0xfc) & ~port_mask[2]);
        return (uint8_t)(port_mask[n] | (t->latch[n] & t->ddr[n]) | (t->input[n] & ~t->ddr[n]));
    }
    case 0x08: return m6805_tdr_r(&t->cpu);
    case 0x09: return m6805_tcr_r(&t->cpu);
    case 0x0b: return t->pcr;
    default: return 0xff;
    }
}

static void T68_HOT mcu_write(void *u, uint16_t a, uint8_t v)
{
    taito68705 *t = (taito68705 *)u;
    if (a >= 0x80) return;
    if (a >= 0x10) { t->ram[a - 0x10] = v; return; }
    switch (a) {
    case 0x00: case 0x01: case 0x02: {
        const int n = a;
        v &= (uint8_t)~port_mask[n];
        const uint8_t diff = t->latch[n] ^ v;
        t->latch[n] = v;
        if (diff & t->ddr[n]) port_cb(t, n);
        break;
    }
    case 0x04: case 0x05: case 0x06: {
        const int n = a - 4;
        v &= (uint8_t)~port_mask[n];
        if (v != t->ddr[n]) { t->ddr[n] = v; port_cb(t, n); }
        break;
    }
    case 0x08: m6805_tdr_w(&t->cpu, v); break;
    case 0x09: m6805_tcr_w(&t->cpu, v); break;
    default: break;     // PCR (EPROM programming) and unmapped
    }
}

// m68705 device reset (DDRs to inputs, timer, CPU)
static void cpu_reset(taito68705 *t)
{
    for (int n = 0; n < 3; n++)
        if (t->ddr[n]) { t->ddr[n] = 0; port_cb(t, n); }
    t->pcr |= 0xfb;
    t->cpu.int_line = false;
    m6805_reset(&t->cpu);
}

void taito68705_init(taito68705 *t, const uint8_t *rom)
{
    memset(t, 0, sizeof *t);
    t->rom = rom;
    m6805_init(&t->cpu);
    t->cpu.read = mcu_read;
    t->cpu.write = mcu_write;
    t->cpu.user = t;
    taito68705_reset(t);
}

void taito68705_reset(taito68705 *t)
{
    memset(t->input, 0xff, sizeof t->input);
    t->pcr = 0xff;
    t->latch_driven = false;
    t->reset_input = false;
    t->host_latch = t->mcu_latch = t->pa_output = t->pb_output = 0xff;
    t->host_flag = t->mcu_flag = false;
    t->in_reset = false;
    t->debt = 0;
    cpu_reset(t);
    set_irq(t);
}

uint8_t taito68705_data_r(taito68705 *t)
{
    t->mcu_flag = false;
    return t->mcu_latch;
}

void taito68705_data_w(taito68705 *t, uint8_t data)
{
    if (!t->reset_input) t->host_flag = true;
    t->host_latch = data;
    if (t->latch_driven) t->input[0] = data;
    set_irq(t);
}

void taito68705_reset_w(taito68705 *t, bool asserted)
{
    t->reset_input = asserted;
    if (asserted) {
        t->host_flag = false;
        t->mcu_flag = false;
        set_irq(t);
        t->in_reset = true;            // halted
    } else if (t->in_reset) {
        t->in_reset = false;           // released: the CPU resets
        cpu_reset(t);
        set_irq(t);
        t->debt = 0;
    }
}

void T68_HOT taito68705_run(taito68705 *t, int cycles)
{
    if (t->in_reset) return;
    const int want = cycles - t->debt;
    if (want <= 0) { t->debt = -want; return; }
    t->debt = m6805_run(&t->cpu, want) - want;
}
