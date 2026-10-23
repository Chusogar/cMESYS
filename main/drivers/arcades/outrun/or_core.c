// or_core.c - OutRun (Sega, 1986). See or_core.h.
#include "or_core.h"

#include <stdlib.h>
#include <string.h>

#include "m68k/m68k.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define OR_HOT IRAM_ATTR
#else
#define OR_HOT
#endif

static or_core *g_core;
bool or_ipl_dirty;

// The 68000 running now (0 main, 1 sub), for profiling hooks.
int or_running_cpu(void) { return g_core ? g_core->cur : 0; }

// Mapper kinds (sega_315_5195 map[] entries)
enum {
    K_ROM = 2,      // main CPU program (0x40000 bytes; zeros up to the 0x60000 region)
    K_SUBROM,       // sub CPU program, seen by the main CPU
    K_RAM,          // plain RAM
    K_PALRAM,       // palette RAM (marks the palette changed)
    K_SPRRAM,       // sprite RAM
    K_IO,           // misc I/O ($90000 window of region 4)
    K_ROADCTL,      // road control (reads swap the road buffers)
};

// Program ROM reads go through 1 KB pages (flash, or copies in RAM - also the
// 32-bit-only IRAM: always read as aligned 32-bit words).
static inline uint32_t rom_word32(const uint8_t *const *pages, uint32_t off)
{
    return *(const uint32_t *)(pages[(off >> 10) & 0xff] + (off & 0x3fc));
}
static inline uint16_t rom16(const uint8_t *const *pages, uint32_t off)
{
    uint32_t w = rom_word32(pages, off);
    if (off & 2) w >>= 16;
    return (uint16_t)(((w & 0xff) << 8) | ((w >> 8) & 0xff));
}
static inline uint32_t rom32(const uint8_t *const *pages, uint32_t off)     // off % 4 == 0
{
    return __builtin_bswap32(rom_word32(pages, off));
}

// ---------------------------------------------------------------------------
// Time and interrupts
// ---------------------------------------------------------------------------
// Main CPU cycles since power on, at the current instruction.
static inline int64_t now(const or_core *c)
{
    return c->slice_start + (c->cur == 0 ? m68k_cycles_run() + c->slice_skipped : 0);
}

static void update_main_irqs(or_core *c)
{
    int level = 0;
    if (c->irq2) level = 2;
    if (c->vblank) level = 4;
    if (c->irq2 && c->vblank) level = 6;
    c->irq_level[0] = (uint8_t)level;
    if (c->cur == 0) or_ipl_dirty = true;
}

int cpu_irq_ack(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

// RESET instruction: on the main CPU it pulses the sub CPU's reset line.
void cpu_pulse_reset(void)
{
    if (g_core->cur == 0) g_core->sub_reset_pending = true;
}
void cpu_set_fc(int fc) { (void)fc; }

static void idle_check(or_core *c, uint32_t pc);

void OR_HOT cpu_instr_callback(unsigned int pc)
{
    or_core *c = g_core;
    if (or_ipl_dirty) {
        or_ipl_dirty = false;
        m68k_set_irq(c->irq_level[c->cur]);
        pc = m68k_get_reg(NULL, M68K_REG_PC);
    }
    if (c->idle_skip) idle_check(c, pc);
}

// ---------------------------------------------------------------------------
// i8255 (mode 0): port A in (bank motor limits, ADC done), port B out (bank
// motor), port C out (display enable, ADC channel, sound CPU reset)
// ---------------------------------------------------------------------------
static void bankmotor_control_w(or_core *c, uint8_t data)
{
    data &= 0x0f;
    if (data == 0) return;
    c->bankmotor_delta = 8 - data;
}

// To the sound board, time-stamped. (A full queue - the board on another core
// fell far behind - would lose the event: the producer waits instead.)
static void snd_event(or_core *c, uint8_t type, uint8_t value)
{
    while (!or_sound_event(c->sound, now(c), type, value)) {
        if (!c->sound_async) or_sound_run(c->sound, c->time);
    }
}

static void video_control_w(or_core *c, uint8_t data)
{
    c->display_enable = (data & 0x20) != 0;
    c->adc_select = (data >> 2) & 7;
    const bool reset = (data & 1) == 0;
    if (reset != c->z80_reset) snd_event(c, OR_EV_RESET, reset);
    c->z80_reset = reset;
}

static void ppi_output(or_core *c, int port)
{
    const uint8_t ctl = c->ppi_control;
    if (port == 1) bankmotor_control_w(c, (ctl & 0x02) ? 0xff : c->ppi_out[1]);
    if (port == 2) {
        uint8_t mask = 0;
        if (!(ctl & 0x08)) mask |= 0xf0;
        if (!(ctl & 0x01)) mask |= 0x0f;
        video_control_w(c, (uint8_t)((c->ppi_out[2] & mask) | (0xff & ~mask)));
    }
}

static void ppi_set_mode(or_core *c, uint8_t data)
{
    c->ppi_control = data;
    c->ppi_out[0] = c->ppi_out[1] = c->ppi_out[2] = 0;
    ppi_output(c, 1);
    ppi_output(c, 2);
}

static uint8_t porta_r(or_core *c)
{
    uint8_t ret = 0xff;
    const int pos = (c->bankmotor_pos >> 8) & 0xff;
    if (pos <= 0x20 + 2) ret ^= 0x20;
    else if (pos >= 0x80 - 2 && pos <= 0x80 + 2) ret ^= 0x10;
    else if (pos >= 0xe0 - 2) ret ^= 0x08;
    if (c->adc_done_at >= 0 && now(c) >= c->adc_done_at) {
        c->adc_done_at = -1;
        c->adc_intr = true;
    }
    if (c->adc_intr) ret ^= 0x40;
    return ret;
}

static uint8_t ppi_read(or_core *c, int port)
{
    const uint8_t ctl = c->ppi_control;
    switch (port) {
    case 0: return (ctl & 0x10) ? porta_r(c) : c->ppi_out[0];
    case 1: return (ctl & 0x02) ? 0 : c->ppi_out[1];
    case 2: {
        uint8_t v = 0;
        if (!(ctl & 0x08)) v |= c->ppi_out[2] & 0xf0;
        if (!(ctl & 0x01)) v |= c->ppi_out[2] & 0x0f;
        return v;
    }
    default: return ctl;
    }
}

static void ppi_write(or_core *c, int port, uint8_t data)
{
    switch (port) {
    case 0: if (!(c->ppi_control & 0x10)) c->ppi_out[0] = data; break;
    case 1: if (!(c->ppi_control & 0x02)) { c->ppi_out[1] = data; ppi_output(c, 1); } break;
    case 2: c->ppi_out[2] = data; ppi_output(c, 2); break;
    case 3:
        if (data & 0x80) ppi_set_mode(c, data);
        else {
            const int bit = (data >> 1) & 7;
            c->ppi_out[2] = (uint8_t)((c->ppi_out[2] & ~(1 << bit)) | ((data & 1) << bit));
            ppi_output(c, 2);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// ADC0804 (steering, gas, brake, bank motor position)
// ---------------------------------------------------------------------------
static uint8_t analog_r(or_core *c)
{
    switch (c->adc_select) {
    case 0: return c->steer;
    case 1: return c->gas;
    case 2: return c->brake;
    case 3: return (uint8_t)((c->bankmotor_pos >> 8) & 0xff);
    default: return 0x10;
    }
}

static void adc_settle(or_core *c)
{
    if (c->adc_done_at >= 0 && now(c) >= c->adc_done_at) {
        c->adc_done_at = -1;
        c->adc_intr = true;
    }
}

// 73 clocks of 25.1748 MHz / 24
#define ADC_CONVERSION ((int64_t)(73.0 * 24.0 * OR_MAIN_CLOCK / 25174800.0 + 0.5))

static uint8_t adc_read(or_core *c)
{
    adc_settle(c);
    c->adc_intr = false;
    return c->adc_result;
}

static void adc_write(or_core *c)
{
    adc_settle(c);
    c->adc_intr = false;
    if (c->adc_done_at < 0) {
        c->adc_done_at = now(c) + ADC_CONVERSION;
        // the result is the input at the end of the conversion: inputs only
        // change between frames, so it can be taken now
        c->adc_result = analog_r(c);
    }
}

// ---------------------------------------------------------------------------
// Main CPU I/O ($90000 window): MAME's outrun_custom_io_r/w
// ---------------------------------------------------------------------------
static uint16_t io_read(or_core *c, uint32_t offset)
{
    offset &= 0x3f;
    switch (offset & 0x38) {
    case 0x00: return ppi_read(c, offset & 3);
    case 0x08:
        switch (offset & 3) {
        case 0: return c->in_service;
        case 1: return 0xff;
        case 2: return c->dsw_a;
        default: return c->dsw_b;
        }
    case 0x18: return adc_read(c);
    default: return 0xffff;
    }
}

static void sprites_swap(or_core *c)
{
    for (int i = 0; i < 0x800; i++) {
        const uint16_t t = c->spriteram[i];
        c->spriteram[i] = c->spritebuf[i];
        c->spritebuf[i] = t;
    }
    c->spriteram[0] = 0xffff;
}

static void io_write(or_core *c, uint32_t offset, uint16_t data, uint16_t mask)
{
    offset &= 0x3f;
    switch (offset & 0x38) {
    case 0x00: if (mask & 0x00ff) ppi_write(c, offset & 3, (uint8_t)data); return;
    case 0x18: adc_write(c); return;
    case 0x38: sprites_swap(c); return;
    default: return;    // $20 mute and lamps, $60 watchdog
    }
}

static void road_swap(or_core *c)
{
    for (int i = 0; i < 0x800; i++) {
        const uint16_t t = c->roadram[i];
        c->roadram[i] = c->roadbuf[i];
        c->roadbuf[i] = t;
    }
}

// ---------------------------------------------------------------------------
// Memory mapper (MAME's segaorun_state::memory_mapper)
// ---------------------------------------------------------------------------
static void mapper_cb(void *u, sega_315_5195 *m, int index)
{
    or_core *c = (or_core *)u;
    switch (index) {
    case 5:
        s16m_map_handler(m, 0x90000, 0x10000, 0xf00000, K_ROADCTL, K_ROADCTL);
        s16m_map_ram(m, 0x80000, 0x01000, 0xf0f000, (uint8_t *)c->roadram, K_RAM, K_RAM);
        s16m_map_ram(m, 0x60000, 0x08000, 0xf18000, (uint8_t *)c->cpu1ram, K_RAM, K_RAM);
        s16m_map_rom(m, 0x00000, 0x60000, 0xf00000, c->roms.sub, 0x60000, K_SUBROM, S16M_NONE);
        break;
    case 4:
        s16m_map_handler(m, 0x90000, 0x10000, 0xf00000, K_IO, K_IO);
        break;
    case 3:
        s16m_map_ram(m, 0x00000, 0x01000, 0xfff000, (uint8_t *)c->spriteram, K_RAM, K_RAM);
        break;
    case 2:
        s16m_map_ram(m, 0x00000, 0x02000, 0xffe000, (uint8_t *)c->paletteram, K_RAM, K_PALRAM);
        break;
    case 1:
        s16m_map_ram(m, 0x00000, 0x10000, 0xfe0000, (uint8_t *)c->tileram, K_RAM, K_RAM);
        s16m_map_ram(m, 0x10000, 0x01000, 0xfef000, (uint8_t *)c->textram, K_RAM, K_RAM);
        break;
    case 0:
        s16m_map_ram(m, 0x60000, 0x08000, 0xf98000, (uint8_t *)c->workram, K_RAM, K_RAM);
        s16m_map_rom(m, 0x00000, 0x60000, 0xf80000, c->roms.main, 0x60000, K_ROM, S16M_NONE);
        break;
    }
}

static void mapper_sound_w(void *u, uint8_t data)
{
    snd_event((or_core *)u, OR_EV_LATCH, data);
}

static uint16_t main_read16(or_core *c, uint32_t a);
static void main_write16(or_core *c, uint32_t a, uint16_t v, uint16_t mask);
static uint16_t bus_r(void *u, uint32_t a) { return main_read16((or_core *)u, a); }
static void bus_w(void *u, uint32_t a, uint16_t v) { main_write16((or_core *)u, a, v, 0xffff); }

static uint16_t OR_HOT main_read16(or_core *c, uint32_t a)
{
    a &= 0xfffffe;
    sega_315_5195 *m = &c->mapper;
    const s16m_map *e = &m->map[m->rpage[a >> S16M_PAGE_SHIFT]];
    const uint32_t off = (a & ~e->mirror) - e->start;
    switch (e->kind) {
    case K_RAM: return ((const uint16_t *)e->mem)[off >> 1];
    case K_ROM: return off < 0x40000 ? rom16(c->main_page, off) : 0;
    case K_SUBROM: return off < 0x40000 ? rom16(c->sub_page, off) : 0;
    case K_IO: return io_read(c, off >> 1);
    case K_ROADCTL: road_swap(c); return 0xffff;
    case S16M_REGS: return (uint16_t)(0xff00 | s16m_read(m, a >> 1));
    default: return 0xffff;
    }
}

static void OR_HOT main_write16(or_core *c, uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffffe;
    sega_315_5195 *m = &c->mapper;
    const s16m_map *e = &m->map[m->wpage[a >> S16M_PAGE_SHIFT]];
    const uint32_t off = (a & ~e->mirror) - e->start;
    switch (e->kind) {
    case K_PALRAM: c->palette_dirty = true; // fall through
    case K_RAM: {
        uint16_t *p = &((uint16_t *)e->mem)[off >> 1];
        *p = (uint16_t)((*p & ~mask) | (v & mask));
        return;
    }
    case K_IO: io_write(c, off >> 1, v, mask); return;
    case K_ROADCTL: if (mask & 0x00ff) c->road_control = v & 3; return;
    case S16M_REGS: if (mask & 0x00ff) s16m_write(m, a >> 1, (uint8_t)v); return;
    default: return;
    }
}

// ---------------------------------------------------------------------------
// Sub CPU map (fixed): MAME's sub_map
// ---------------------------------------------------------------------------
static uint16_t OR_HOT sub_read16(or_core *c, uint32_t a)
{
    a &= 0xffffe;
    if (a < 0x60000) return a < 0x40000 ? rom16(c->sub_page, a) : 0;
    if (a < 0x80000) return c->cpu1ram[(a & 0x7fff) >> 1];
    if (a < 0x90000) return c->roadram[(a & 0xfff) >> 1];
    if (a < 0xa0000) { road_swap(c); return 0xffff; }
    return 0xffff;
}

static void OR_HOT sub_write16(or_core *c, uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xffffe;
    uint16_t *p;
    if (a < 0x60000) return;
    if (a < 0x80000) p = &c->cpu1ram[(a & 0x7fff) >> 1];
    else if (a < 0x90000) p = &c->roadram[(a & 0xfff) >> 1];
    else if (a < 0xa0000) { if (mask & 0x00ff) c->road_control = v & 3; return; }
    else return;
    *p = (uint16_t)((*p & ~mask) | (v & mask));
}

static inline uint16_t OR_HOT bus_read16(uint32_t a)
{
    or_core *c = g_core;
    return c->cur ? sub_read16(c, a) : main_read16(c, a);
}
static inline void OR_HOT bus_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    or_core *c = g_core;
    if (c->cur) sub_write16(c, a, v, mask);
    else main_write16(c, a, v, mask);
}

unsigned int OR_HOT cpu_read_byte(unsigned int a) { uint16_t v = bus_read16(a); return (a & 1) ? v & 255 : v >> 8; }
unsigned int OR_HOT cpu_read_word(unsigned int a) { return bus_read16(a); }
// Long reads (the 68000's prefetch among them): one access for aligned program ROM
unsigned int OR_HOT cpu_read_long(unsigned int a)
{
    or_core *c = g_core;
    if (!(a & 3)) {
        if (c->cur) {
            const uint32_t s = a & 0xffffc;
            if (s < 0x40000) return rom32(c->sub_page, s);
        } else {
            const uint32_t m = a & 0xfffffc;
            const s16m_map *e = &c->mapper.map[c->mapper.rpage[m >> S16M_PAGE_SHIFT]];
            const uint32_t off = (m & ~e->mirror) - e->start;
            if (e->kind == K_ROM && off < 0x40000) return rom32(c->main_page, off);
        }
    }
    return ((uint32_t)bus_read16(a) << 16) | bus_read16(a + 2);
}
unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void OR_HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    if (a & 1) bus_write16(a, (uint16_t)(v & 255), 0x00ff);
    else bus_write16(a, (uint16_t)((v & 255) << 8), 0xff00);
}
void OR_HOT cpu_write_word(unsigned int a, unsigned int v) { bus_write16(a, (uint16_t)v, 0xffff); }
void OR_HOT cpu_write_long(unsigned int a, unsigned int v) { bus_write16(a, (uint16_t)(v >> 16), 0xffff); bus_write16(a + 2, (uint16_t)v, 0xffff); }
void OR_HOT cpu_write_long_pd(unsigned int a, unsigned int v) { bus_write16(a + 2, (uint16_t)v, 0xffff); bus_write16(a, (uint16_t)(v >> 16), 0xffff); }

// ---------------------------------------------------------------------------
// Busy loops of the 68000s, fast-forwarded exactly. They wait for a word to
// change:
//   main $7DDA  tst.w $60800 / bge $7DDA        (set by its vblank interrupt)
//   main $7964  cmpi.w #1,$60800 / beq $7964
//   sub  $1182  tst.w ($3800,A5) / bge $1182    (negative: set by the main CPU or its
//                                                own interrupt)
// Interrupt levels only change between slices, and the other CPU does not run
// during this one's slice: until the slice ends the word cannot change, so the
// remaining whole passes only take time (the flags come out the same).
// The cycles of a pass are measured from two hits in one slice.
// ---------------------------------------------------------------------------
static void OR_HOT idle_check(or_core *c, uint32_t pc)
{
    const int k = c->cur;
    uint16_t w;
    int site = k;
    if (k == 0) {
        if (pc != 0x7dda && pc != 0x7964) return;
        const s16m_map *e = &c->mapper.map[c->mapper.rpage[0x60800 >> S16M_PAGE_SHIFT]];
        if (e->kind != K_RAM) return;
        w = main_read16(c, 0x60800);
        if (pc == 0x7964) {                 // cmpi.w #1,$60800 / beq $7964
            if (w != 1) return;
            site = 2;
            w = 0;
        }
    } else {
        if (pc != 0x1182) return;
        const uint32_t a = (m68k_get_reg(NULL, M68K_REG_A5) + 0x3800) & 0xffffe;
        if (a < 0x60000 || a >= 0x90000) return;
        w = sub_read16(c, a);
    }
    if (w & 0x8000) return;                 // the loop ends on this pass
    const int rem = m68k_cycles_remaining();
    or_idle_site *s = &c->site[site];
    if (s->slice == c->slice_no && s->cost == 0) s->cost = s->rem - rem;
    s->slice = c->slice_no;
    s->rem = rem;
    if (s->cost <= 0) return;
    const uint32_t n = (uint32_t)((rem - 1) / s->cost);
    if (!n) return;
    const int32_t cyc = (int32_t)(n * (uint32_t)s->cost);
    m68k_modify_timeslice(-cyc);
    s->rem = rem - cyc;
    c->slice_skipped += cyc;
    c->skipped[k] += (uint32_t)cyc;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
static void run_cpu(or_core *c, int i, int cycles)
{
    c->cur = i;
    m68k_set_context(c->ctx[i]);
    if (i == 1 && c->sub_reset_pending) {
        c->sub_reset_pending = false;
        m68k_pulse_reset();
    }
    m68k_set_irq(c->irq_level[i]);
    or_ipl_dirty = false;
    c->slice_no++;
    c->slice_skipped = 0;
    const int run = cycles - c->debt[i];
    // (fast-forwarded cycles shrink the timeslice instead of counting as run)
    c->debt[i] = run > 0 ? m68k_execute(run) + c->slice_skipped - run : -run;
    m68k_get_context(c->ctx[i]);
}

void or_init(or_core *c, const or_roms *roms, uint32_t sample_rate)
{
    uint16_t *wr = c->workram, *c1 = c->cpu1ram, *tr = c->tileram;
    or_sound *snd = c->sound;
    memset(c, 0, sizeof *c);
    c->sound = snd;
    c->workram = wr ? wr : (uint16_t *)malloc(0x8000);
    c->cpu1ram = c1 ? c1 : (uint16_t *)malloc(0x8000);
    c->tileram = tr ? tr : (uint16_t *)malloc(0x10000);
    memset(c->workram, 0, 0x8000);  // power-on contents (NVRAM: all 0 until loaded)
    memset(c->cpu1ram, 0, 0x8000);
    memset(c->tileram, 0, 0x10000);
    g_core = c;
    c->roms = *roms;
    c->sample_rate = sample_rate ? sample_rate : 44100;
    for (int i = 0; i < 256; i++) {
        c->main_page[i] = roms->main + i * 1024;
        c->sub_page[i] = roms->sub + i * 1024;
    }

    c->mapper.user = c;
    c->mapper.mapper = mapper_cb;
    c->mapper.sound_w = mapper_sound_w;
    c->mapper.bus_r = bus_r;
    c->mapper.bus_w = bus_w;

    if (!c->sound) c->sound = (or_sound *)malloc(sizeof(or_sound));
    or_sound_init(c->sound, roms->sound, roms->pcm, c->sample_rate);
    c->sound->idle_skip = true;

    m68k_init();
    for (int i = 0; i < 2; i++) c->ctx[i] = calloc(1, m68k_context_size());
    c->in_service = 0xef;
    c->dsw_a = 0xff;
    c->dsw_b = 0xf9;            // Mini Up cabinet, demo sounds on, normal settings
    c->steer = 0x80;
    or_reset(c);
}

void or_reset(or_core *c)
{
    g_core = c;
    // (the RAM keeps its contents over a reset; workram is the NVRAM)
    s16m_reset(&c->mapper);
    c->time = c->slice_start = 0;
    c->irq2 = c->vblank = false;
    c->irq_level[0] = c->irq_level[1] = 0;
    for (int i = 0; i < 2; i++) {
        c->cur = i;
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
        m68k_get_context(c->ctx[i]);
        c->debt[i] = 0;
    }
    c->cur = 0;
    c->sub_reset_pending = false;
    or_sound_reset(c->sound);
    c->z80_reset = false;
    ppi_set_mode(c, 0x9b);      // i8255 reset: all inputs
    c->adc_done_at = -1;
    c->adc_intr = false;
    c->adc_result = 0;
    c->bankmotor_pos = 0x8000;
    c->bankmotor_next = OR_MAIN_CLOCK / 100;
    c->road_control = 0;
    memset(c->latched_page, 0, sizeof c->latched_page);
    memset(c->latched_x, 0, sizeof c->latched_x);
    memset(c->latched_y, 0, sizeof c->latched_y);
    c->line = 0;
    c->line_frac = 0;
}

void or_set_inputs(or_core *c, const or_inputs *in)
{
    uint8_t v = 0xef;
    if (in->service) v &= (uint8_t)~0x02;
    if (in->start) v &= (uint8_t)~0x08;
    if (in->coin1) v &= (uint8_t)~0x40;
    if (in->coin2) v &= (uint8_t)~0x80;
    if (in->gear_toggle && !c->gear_prev) c->gear_high = !c->gear_high;
    c->gear_prev = in->gear_toggle;
    if (c->gear_high) v |= 0x10;
    c->in_service = v;
    c->steer = in->steer ? in->steer : 0x80;
    c->gas = in->gas;
    c->brake = in->brake;
}

void or_get_video(const or_core *c, or_video *v)
{
    v->tileram = c->tileram;
    v->textram = c->textram;
    v->palette = c->paletteram;
    v->sprites = c->spritebuf;
    v->road = c->roadbuf;
    v->road_control = c->road_control;
    v->display_enable = c->display_enable;
    memcpy(v->latched_page, c->latched_page, sizeof v->latched_page);
    memcpy(v->latched_x, c->latched_x, sizeof v->latched_x);
    memcpy(v->latched_y, c->latched_y, sizeof v->latched_y);
}

// One stretch of time: both 68000s, then the Z80 over the same time.
static void run_slice(or_core *c, int cycles)
{
    c->slice_start = c->time;
    run_cpu(c, 0, cycles);
    c->cur = 1;
    run_cpu(c, 1, cycles);
    c->cur = 0;
    c->time += cycles;
    if (!c->sound_async) or_sound_run(c->sound, c->time);
}

#ifndef OR_SLICE_LINES
#define OR_SLICE_LINES 4
#endif

static inline bool event_line(int line)
{
    switch (line) {
    case 65: case 66: case 129: case 130: case 193: case 194: case 223: case 224: case 261: return true;
    default: return false;
    }
}

void or_run_frame(or_core *c)
{
    // 400 pixel clocks a line = 635.56 CPU cycles; hblank from pixel 320.
    // The CPUs run OR_SLICE_LINES lines at a time (a line where something
    // happens ends a slice; the IRQ2 lines are split at the start of hblank).
    g_core = c;
    c->sound->idle_skip = c->idle_skip;
    int pending = 0;
    for (c->line = 0; c->line < OR_LINES; c->line++) {
        const int line = c->line;
        if (event_line(line) && pending) { run_slice(c, pending); pending = 0; }
        switch (line) {
        case 66: case 130: case 194: c->irq2 = false; update_main_irqs(c); break;
        case 223: c->vblank = true; update_main_irqs(c); c->irq_level[1] = 4; break;
        case 224:
            c->vblank = false; update_main_irqs(c); c->irq_level[1] = 0;
            if (c->on_frame) c->on_frame(c->user, c);
            break;
        case 261:
            for (int i = 0; i < 4; i++) {
                c->latched_page[i] = c->textram[0xe80 / 2 + i];
                c->latched_y[i] = c->textram[0xe90 / 2 + i];
                c->latched_x[i] = c->textram[0xe98 / 2 + i];
            }
            break;
        }
        const int64_t total = (int64_t)OR_MAIN_CLOCK * 400 + c->line_frac;
        const int cyc = (int)(total / (int64_t)OR_PIXEL_CLOCK);
        c->line_frac = (int32_t)(total - (int64_t)cyc * (int64_t)OR_PIXEL_CLOCK);
        if (line == 65 || line == 129 || line == 193) {
            const int a = cyc * 320 / 400;
            run_slice(c, a);
            c->irq2 = true;
            update_main_irqs(c);
            run_slice(c, cyc - a);
        } else {
            pending += cyc;
            if (event_line(line) || (line + 1) % OR_SLICE_LINES == 0 || line == OR_LINES - 1) {
                run_slice(c, pending);
                pending = 0;
            }
        }
        // bank motor: every 10 ms
        while (c->time >= c->bankmotor_next) {
            c->bankmotor_next += OR_MAIN_CLOCK / 100;
            c->bankmotor_pos += 100 * c->bankmotor_delta;
            if (c->bankmotor_pos <= 0x2000) c->bankmotor_pos = 0x2000;
            else if (c->bankmotor_pos >= 0xe000) c->bankmotor_pos = 0xe000;
        }
    }
    c->line = 0;
}
