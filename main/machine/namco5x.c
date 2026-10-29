// namco5x.c - Namco 51xx-54xx. See namco5x.h.
#include "namco5x.h"

// --- 51xx ---
static uint8_t n51_k(void *u) { namco51 *n = u; return (uint8_t)((n->rw << 3) | (n->port_o & 7)); }
static void n51_o(void *u, uint8_t d) { ((namco51 *)u)->port_o = d; }
static uint8_t n51_r(void *u, int p) { namco51 *n = u; return n->in ? (uint8_t)(n->in(n->user, p) & 15) : 0; }

void namco51_init(namco51 *n, const uint8_t *rom)
{
    mb88_init(&n->cpu, MB8843, rom);
    n->cpu.read_k = n51_k;
    n->cpu.write_o = n51_o;
    n->cpu.read_r = n51_r;
    n->cpu.user = n;
    n->port_o = n->rw = 0;
}

void namco51_reset(namco51 *n)
{
    n->port_o = n->rw = 0;
    mb88_reset(&n->cpu);
}

// --- 52xx ---
static uint8_t n52_k(void *u) { return (uint8_t)(((namco52 *)u)->latched_cmd & 15); }
static int n52_si(void *u) { (void)u; return 1; }
static void n52_p(void *u, uint8_t d) { ((namco52 *)u)->dac = (uint8_t)(d & 15); }
static uint8_t n52_rom(namco52 *n) { return n->rom_read ? n->rom_read(n->user, n->address) : 0xff; }
static uint8_t n52_r(void *u, int p)
{
    namco52 *n = u;
    if (p == 0) return (uint8_t)(n52_rom(n) & 15);
    if (p == 1) return (uint8_t)(n52_rom(n) >> 4);
    return 0;
}
static void n52_rw(void *u, int p, uint8_t d)
{
    namco52 *n = u;
    if (p == 2) n->address = (uint16_t)((n->address & 0xfff0) | (d & 15));
    if (p == 3) n->address = (uint16_t)((n->address & 0xff0f) | ((d & 15) << 4));
}
static void n52_o(void *u, uint8_t d) { namco52 *n = u; n->address = (uint16_t)((n->address & 0x00ff) | (d << 8)); }

void namco52_init(namco52 *n, const uint8_t *rom)
{
    mb88_init(&n->cpu, MB8843, rom);
    n->cpu.read_k = n52_k;
    n->cpu.write_o = n52_o;
    n->cpu.write_p = n52_p;
    n->cpu.read_si = n52_si;
    n->cpu.read_r = n52_r;
    n->cpu.write_r = n52_rw;
    n->cpu.user = n;
    n->latched_cmd = n->dac = 0;
    n->address = 0;
}

void namco52_reset(namco52 *n)
{
    n->latched_cmd = n->dac = 0;
    n->address = 0;
    mb88_reset(&n->cpu);
}

// --- 53xx ---
static uint8_t n53_k(void *u) { (void)u; return 0; }
static void n53_o(void *u, uint8_t d) { ((namco53 *)u)->port_o = d; }
static uint8_t n53_r(void *u, int p) { namco53 *n = u; return n->in ? (uint8_t)(n->in(n->user, p) & 15) : 0; }

void namco53_init(namco53 *n, const uint8_t *rom)
{
    mb88_init(&n->cpu, MB8843, rom);
    n->cpu.read_k = n53_k;
    n->cpu.write_o = n53_o;
    n->cpu.read_r = n53_r;
    n->cpu.user = n;
    n->port_o = 0;
}

void namco53_reset(namco53 *n)
{
    n->port_o = 0;
    mb88_reset(&n->cpu);
}

// --- 54xx ---
static uint8_t n54_k(void *u) { return (uint8_t)(((namco54 *)u)->latched_cmd >> 4); }
static uint8_t n54_r(void *u, int p) { return p == 0 ? (uint8_t)(((namco54 *)u)->latched_cmd & 15) : 0; }
static void n54_o(void *u, uint8_t d) { namco54 *n = u; n->chanl3 = (uint8_t)(d & 15); n->chanl2 = (uint8_t)(d >> 4); }
static void n54_rw(void *u, int p, uint8_t d) { if (p == 1) ((namco54 *)u)->chanl1 = (uint8_t)(d & 15); }

void namco54_init(namco54 *n, const uint8_t *rom)
{
    mb88_init(&n->cpu, MB8844, rom);
    n->cpu.read_k = n54_k;
    n->cpu.write_o = n54_o;
    n->cpu.read_r = n54_r;
    n->cpu.write_r = n54_rw;
    n->cpu.user = n;
    n->latched_cmd = n->chanl1 = n->chanl2 = n->chanl3 = 0;
}

void namco54_reset(namco54 *n)
{
    n->latched_cmd = n->chanl1 = n->chanl2 = n->chanl3 = 0;
    mb88_reset(&n->cpu);
}
