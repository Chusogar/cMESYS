// atarisy1_core.c - Atari System 1: Indiana Jones and the Temple of Doom,
// Road Runner. C port of dsp-cpp's drivers/arcade/atari_system1.cpp.
#include "atarisy1_core.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "m68k/m68k.h"

#ifndef AS1_HOT
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define AS1_HOT IRAM_ATTR   /* hot code out of the flash cache */
#else
#define AS1_HOT
#endif
#endif

static as1_core *g_core;   // Musashi has one global CPU

// An interrupt line changed inside the 68000: applied before the next
// instruction (global: the ESP32 build's Musashi tests it inline).
bool as1_ipl_dirty;

static inline void flush_via_tms(as1_core *c);

// ---------------------------------------------------------------------------
// Graphics set-up (convert_background / decode_bank of the C++ driver)
// ---------------------------------------------------------------------------

bool as1_pack_bank(const uint8_t *(*chunk)(void *u, int index), void *u, int bank, uint32_t *out)
{
    const uint32_t offset = 0x80000u * (uint32_t)(bank - 1);
    bool any = false;
    const uint8_t *planes[4][2];
    for (int k = 0; k < 4; k++)
        for (int half = 0; half < 2; half++) {
            uint32_t at = offset + (uint32_t)k * 0x10000u + (uint32_t)half * 0x4000u;
            planes[k][half] = chunk(u, (int)(at / 0x4000));
            if (planes[k][half]) any = true;
        }
    if (!any) return false;
    for (unsigned at = 0; at < AS1_TILES_PER_BANK * 8; at++) {
        uint32_t row = 0;
        for (int k = 0; k < 4; k++) {
            const uint8_t *p = planes[k][at >> 14];
            uint8_t b = p ? (uint8_t)~p[at & 0x3fff] : 0;
            for (int x = 0; x < 8; x++) row |= (uint32_t)((b >> (7 - x)) & 1) << (4 * x + k);
        }
        out[at] = row;
    }
    return true;
}

static uint8_t decode_bank(as1_gfx *g, uint8_t prom1, uint8_t prom2, int bpp)
{
    int bank_index;
    if ((prom1 & 0x10) == 0) bank_index = 1;
    else if ((prom1 & 0x20) == 0) bank_index = 2;
    else if ((prom1 & 0x40) == 0) bank_index = 3;
    else if ((prom1 & 0x80) == 0) bank_index = 4;
    else if ((prom2 & 0x40) == 0) bank_index = 5;
    else if ((prom2 & 0x80) == 0) bank_index = ((prom2 & 0x08) == 0) ? 7 : 6;
    else return 0;

    if (g->bank_gfx[bpp - 4][bank_index] != 0) return g->bank_gfx[bpp - 4][bank_index];
    // Indy has 2 MB of graphics image (banks 1-4), Road Runner 3 MB (1-6).
    if (0x80000u * (uint32_t)(bank_index - 1) >= as1_gfx_bytes(g->roms->game)) return 0;
    uint8_t idx = g->next_gfx_index++;
    if (idx >= 16) return 0;
    g->gfx_bank[idx] = (uint8_t)bank_index;
    g->gfx_bpp[idx] = (uint8_t)bpp;
    g->bank_gfx[bpp - 4][bank_index] = idx;
    g->bank_color_shift[idx] = (uint8_t)(bpp - 3);
    return idx;
}

static void convert_background(as1_gfx *g, const uint8_t *proms)
{
    memset(g->bank_gfx, 0, sizeof g->bank_gfx);
    g->next_gfx_index = 1;
    memset(g->bank_color_shift, 1, sizeof g->bank_color_shift);
    memset(g->gfx_bank, 0, sizeof g->gfx_bank);
    memset(g->gfx_bpp, 0, sizeof g->gfx_bpp);
    for (int obj = 0; obj < 2; obj++)
        for (int i = 0; i < 256; i++) {
            uint8_t prom1 = proms[i + 0x100 * obj];
            uint8_t prom2 = proms[0x200 + i + 0x100 * obj];
            int bpp = 4;
            if (prom2 & 0x10) {
                bpp = 5;
                if (prom2 & 0x20) bpp = 6;
            }
            uint8_t offset = prom1 & 0x0f;
            uint8_t bank = decode_bank(g, prom1, prom2, bpp);
            if (obj == 0) {
                uint8_t color = (uint8_t)(((uint8_t)~prom2 & 0x0f) >> (bpp - 4));
                uint8_t used_offset = offset;
                if (bank == 0) {
                    bank = 1;
                    used_offset = 0;
                    color = 0;
                }
                g->playfield_lookup[i] = (uint16_t)(used_offset | ((uint16_t)bank << 8) | ((uint16_t)color << 12));
            } else {
                uint8_t color = (uint8_t)(((uint8_t)~prom2 & 0x07) >> (bpp - 4));
                g->motable[i] = (uint16_t)(offset | ((uint16_t)bank << 8) | ((uint16_t)color << 12));
            }
        }
}

static inline bool gfx_valid(const as1_gfx *g, int gfx)
{
    return gfx == 0 || (gfx < g->next_gfx_index && g->gfx_bank[gfx] != 0);
}

// 2bpp alphanumerics: one ROM byte -> its 4 pens (pixel 0 in the low byte).
static uint32_t char_pens[256];

static void make_char_pens(void)
{
    for (unsigned b = 0; b < 256; ++b) {
        uint32_t v = 0;
        for (int xx = 0; xx < 4; ++xx)
            v |= (uint32_t)((((b >> (7 - xx)) & 1) << 1) | ((b >> (3 - xx)) & 1)) << (8 * xx);
        char_pens[b] = v;
    }
}

// The 4bpp row of a tile, pixel x in bits 4x..4x+3.
static inline uint32_t tile_row4(const as1_gfx *g, int gfx, int code, int row)
{
    const uint32_t *t = g->roms->tiles[g->gfx_bank[gfx] & 7];
    return t ? t[(unsigned)(code & 0xfff) * 8 + (unsigned)row] : 0;
}

// ---------------------------------------------------------------------------
// Interrupts of the 68010
// ---------------------------------------------------------------------------

static void apply_ipl(as1_core *c)
{
    int level = 0;
    for (int l = 7; l >= 1; --l)
        if (c->irq[l] != LINE_CLEAR) { level = l; break; }
    m68k_set_irq((unsigned)level);
}

static void main_irq(as1_core *c, int level, uint8_t state)
{
    if (c->irq[level] == state) return;
    c->irq[level] = state;
    // Inside the 68000 (a memory handler) Musashi would take the interrupt in
    // the middle of the instruction: apply it before the next one instead.
    if (c->in_main) as1_ipl_dirty = true;
    else apply_ipl(c);
}

int cpu_irq_ack(int level)
{
    as1_core *c = g_core;
    if (c && level >= 1 && level <= 7 && c->irq[level] == LINE_HOLD) {
        c->irq[level] = LINE_CLEAR;
        as1_ipl_dirty = true;
    }
    return M68K_INT_ACK_AUTOVECTOR;
}

#ifdef AS1_PROFILE
extern void prof_hook(unsigned pc);
#endif

// Busy loops of the program, fast-forwarded exactly. Interrupts only arrive
// between m68k_execute() calls (a line), so a whole number of iterations
// that ends inside the slice changes nothing but the loop's own state:
//   $005290  subq.l #1,D0 / bne $5290            (BIOS delay)        D0 -= n
//   $02D8A4  tst.w (A2) / bne / ... jsr $10B02   (wait for vblank,   seed at
//            stirring the random seed every pass)                      $400F8E
// The cycles of one iteration are measured from two hits in one slice.
static void AS1_HOT idle_check(as1_core *c, uint32_t pc)
{
    int k;
    if (pc == 0x2d8a4 && c->game == AS1_INDYTEMP) k = 1;
    else if (pc == 0x1482c && c->game == AS1_ROADRUNN) k = 2;
    else if (pc == 0x5290) k = 0;       // (in the BIOS: every game)
    else return;
    int rem = m68k_cycles_remaining();
    if (c->site[k].slice == c->slice && c->site[k].cost == 0) c->site[k].cost = c->site[k].rem - rem;
    c->site[k].slice = c->slice;
    c->site[k].rem = rem;
    const int cost = c->site[k].cost;
    if (cost <= 0) return;
    uint32_t n = (uint32_t)((rem - 1) / cost);
    if (n == 0) return;
    if (k == 2) {
        // Road Runner, $1482C: tst.w (A2) / bne / move.l D2,-(A7) /
        // jsr $10556 (seed at $400C5E = seed * $3619 + $5D35, D0 = a random
        // number below D2 + 1) / jsr $1CA22 (sound queue, empty: $4015CA < 0)
        // / addq.l #4,A7 / bra. A pass leaves the seed, D0, D1 and A0.
        if (m68k_get_reg(NULL, M68K_REG_A2) != 0x400c86 || c->ram[0xc86 >> 1] != 0) return;
        if (!(c->ram[0x15ca >> 1] & 0x8000) || m68k_get_reg(NULL, M68K_REG_D2) != 0xff) return;
        if (m68k_get_reg(NULL, M68K_REG_PPC) != 0x14840) return;   // after one real pass
        uint16_t seed = c->ram[0xc5e >> 1];
        for (uint32_t i = 0; i < n; i++) seed = (uint16_t)(seed * 0x3619u + 0x5d35u);
        c->ram[0xc5e >> 1] = seed;
        int32_t d0 = (int32_t)(int16_t)seed * 0x100 + 0x800000;   // muls D1 ; add.l ; swap ; ext.l
        m68k_set_reg(M68K_REG_D0, (uint32_t)(int32_t)(int16_t)((uint32_t)d0 >> 16));
        m68k_set_reg(M68K_REG_D1, 0x00800000);
        m68k_set_reg(M68K_REG_A0, 0x400c5e);
    } else if (k == 0) {
        uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
        if (d0 < 2) return;
        if (n > d0 - 1) n = d0 - 1;
        m68k_set_reg(M68K_REG_D0, d0 - n);
    } else {
        if (m68k_get_reg(NULL, M68K_REG_A2) != 0x400f88 || c->ram[0xf88 >> 1] != 0) return;
        // Only after one real pass (back from the loop's BRA): it has written
        // the stack words every pass writes again.
        if (m68k_get_reg(NULL, M68K_REG_PPC) != 0x2d8b4) return;
        uint16_t seed = c->ram[0xf8e >> 1];
        for (uint32_t i = 0; i < n; i++) seed = (uint16_t)(seed * 0x3619u + 0x5d35u);
        c->ram[0xf8e >> 1] = seed;
        m68k_set_reg(M68K_REG_D2, m68k_get_reg(NULL, M68K_REG_D2) + n);
    }
    m68k_modify_timeslice(-(int)(n * (uint32_t)cost));
    c->site[k].rem = rem - (int)(n * (uint32_t)cost);
    c->skipped += n * (uint32_t)cost;
}

// Before every instruction. (The ESP32 build's Musashi passes the PC.)
#ifdef AS1_HOOK_PC
void AS1_HOT cpu_instr_callback(unsigned int pc)
#else
void cpu_instr_callback(void)
#endif
{
    as1_core *c = g_core;
#ifndef AS1_HOOK_PC
    unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
#endif
    if (as1_ipl_dirty) {
        as1_ipl_dirty = false;
        apply_ipl(c);
        pc = m68k_get_reg(NULL, M68K_REG_PC);   // an interrupt may have been taken
    }
#ifdef AS1_PROFILE
    prof_hook(pc);
#endif
    if (c->idle_skip) idle_check(c, pc);
}

void cpu_pulse_reset(void) {}
void cpu_set_fc(int fc) { (void)fc; }

// ---------------------------------------------------------------------------
// 68000 memory map
// ---------------------------------------------------------------------------

static inline uint16_t image_word(const as1_core *c, uint32_t off)
{
    const uint8_t *p = c->main_page[off >> 10] + (off & 0x3fe);
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint16_t rom_word(const as1_core *c, uint32_t a) { return image_word(c, a & 0x7fffe); }

static inline uint16_t slap_word(const as1_core *c, uint32_t a)
{
    return image_word(c, 0x80000 + (uint32_t)(c->rom_bank & 3) * 0x2000 + (a & 0x1ffe));
}

static void set_sound_reset(as1_core *c, bool running)
{
    if (running == !c->sound_halted) return;
    if (running) {
        c->sound_halted = false;
        ym2151_clear_external_irq(&c->ym);
        dsp6502_set_irq(&c->snd, LINE_CLEAR);
        dsp6502_set_nmi(&c->snd, LINE_CLEAR);
    } else {
        c->sound_halted = true;
        flush_via_tms(c);
        via6522_reset(&c->via);
        tms5220_reset(&c->tms);
        c->main_pending = false;
        main_irq(c, 6, LINE_CLEAR);
    }
}

static void adc_start(as1_core *c, uint32_t address)
{
    int offset = (int)((address >> 1) & 0xf);
    c->adc_channel = (uint8_t)(offset & 7);
    c->adc_irq_enable = (offset & 8) == 0;
    c->adc_busy = true;
    main_irq(c, 2, LINE_CLEAR);
}

static uint8_t adc_channel_value(const as1_core *c, int ch)
{
    if (c->game == AS1_ROADRUNN) {
        if (ch == 6) return c->analog_y;
        if (ch == 7) return c->analog_x;
        return 0x80;
    }
    if (ch >= 0 && ch <= 3) return (c->joy_bits & (1 << (7 - ch))) ? 0xf0 : 0x00;
    return 0;
}

static void adc_complete(as1_core *c)
{
    if (!c->adc_busy) return;
    c->adc_busy = false;
    c->adc_value = adc_channel_value(c, c->adc_channel);
    if (c->adc_irq_enable) main_irq(c, 2, LINE_HOLD);
}

// Road Runner: the motion objects of the lines finished so far (up to `line`,
// exclusive) are taken from the RAM before it changes (MAME's update_partial)
static void mo_partial(as1_core *c, int line)
{
    if (!c->frame_mo) return;
    if (line > AS1_SCREEN_H) line = AS1_SCREEN_H;
    if (line <= c->mo_line) return;
    as1_build_mo_band(&c->gfx, &c->v, c->frame_mo, c->mo_line, line);
    c->mo_line = line;
}

// Road Runner: INT3 fires at the line of the next motion object whose second
// word is 0xffff, following the link list of the current bank (as dsp-cpp).
static void reschedule_int3(as1_core *c, int scanline)
{
    if (c->game != AS1_ROADRUNN) { c->int3_line = -1; return; }
    const uint16_t *spr = &c->v.ram3[0x1000];
    const int offset = (int)c->v.mo_bank * 256;
    int link = 0, best = scanline;
    bool found = false;
    uint64_t visited = 0;
    while (!(visited & (1ull << (link & 63)))) {
        visited |= 1ull << (link & 63);
        if (spr[offset + link + 0x40] == 0xffff) {
            const uint16_t data = spr[offset + link];
            const int vsize = (data & 15) + 1;
            const int ypos = (256 - (data >> 5) - vsize * 8 - 1) & 0x1ff;
            found = true;
            if (best <= scanline) {
                if ((ypos <= scanline && ypos < best) || ypos > scanline) best = ypos;
            } else if (ypos < best) {
                best = ypos;
            }
        }
        link = spr[offset + link + 0xc0] & 0x3f;
    }
    if (!found || best >= AS1_SCANLINES) best = -1;
    c->int3_line = best;
}

static uint16_t AS1_HOT main_read(as1_core *c, uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x7ffff) return rom_word(c, a);
    if ((a & 0xffe000) == 0x400000) return c->ram[(a & 0x1fff) >> 1];   // the busiest after the ROM
    if (a <= 0x87fff) {
        uint16_t v = slap_word(c, a);
        c->rom_bank = slapstic_tweak(&c->slap, (uint16_t)((a & 0x7fff) >> 1));
        return v;
    }
    if ((a & ~1u) == 0x2e0000) return c->int3_state ? 0x0080 : 0;  // INT3 state (Road Runner)
    if (a >= 0x400000 && a <= 0x401fff) return c->ram[(a & 0x1fff) >> 1];
    if (a >= 0x900000 && a <= 0x9fffff) {
        const uint16_t *page = c->ram2[(a & 0xfffff) >> 12];
        return page ? page[(a & 0xfff) >> 1] : 0;
    }
    if (a >= 0xa00000 && a <= 0xa03fff) return c->v.ram3[(a & 0x3fff) >> 1];
    if (a >= 0xb00000 && a <= 0xb007ff) return c->v.palette[(a & 0x7ff) >> 1];
    if (a >= 0xf00000 && a <= 0xf00fff) return c->eeprom[(a & 0xfff) >> 1];
    if (a >= 0xf20000 && a <= 0xf20007) return 0x00ff;
    if (a >= 0xf40000 && a <= 0xf4001f) {
        uint8_t value = c->adc_value;
        adc_start(c, a);
        return value;
    }
    if (a >= 0xf60000 && a <= 0xf60003) return (uint16_t)(c->in0 | c->vblank | (c->sound_pending ? 0x80 : 0));
    if ((a & ~1u) == 0xfc0000) {
        c->main_pending = false;
        main_irq(c, 6, LINE_CLEAR);
        return c->main_latch;
    }
    return 0xffff;
}

static void AS1_HOT main_write(as1_core *c, uint32_t a, uint16_t value)
{
    a &= 0xffffff;
    if ((a & 0xffe000) == 0x400000) { c->ram[(a & 0x1fff) >> 1] = value; return; }
    if (a <= 0x7ffff) return;
    if (a <= 0x87fff) {
        c->rom_bank = slapstic_tweak(&c->slap, (uint16_t)((a & 0x7fff) >> 1));
        return;
    }
    if (a >= 0x400000 && a <= 0x401fff) { c->ram[(a & 0x1fff) >> 1] = value; return; }
    switch (a & ~1u) {
    case 0x800000: c->scroll_x = value; return;
    case 0x820000:
        c->scroll_y_latch = value;
        c->scroll_y = (c->line < 240) ? (uint16_t)(value - (c->line + 1)) : value;
        return;
    case 0x840000:
        if (value != c->v.priority_pens) mo_partial(c, c->line + 1);
        c->v.priority_pens = value;
        return;
    case 0x860000: {
        uint16_t diff = (uint16_t)(c->bankselect ^ value);
        if (diff & 0x3c) mo_partial(c, c->line + 1);
        if (diff & 0x04) c->v.tile_bank = (uint8_t)((value >> 2) & 1);
        if (diff & 0x80) set_sound_reset(c, (value & 0x80) != 0);
        c->v.mo_bank = (uint8_t)((value >> 3) & 7);
        c->bankselect = value;
        if (diff & 0x38) reschedule_int3(c, c->line);
        return;
    }
    case 0x880000: return;  // watchdog
    case 0x8a0000: main_irq(c, 4, LINE_CLEAR); return;
    case 0x8c0000: c->write_eeprom = true; return;
    default: break;
    }
    if (a >= 0x900000 && a <= 0x9fffff) {
        uint16_t **page = &c->ram2[(a & 0xfffff) >> 12];
        if (!*page) *page = (uint16_t *)calloc(0x800, sizeof(uint16_t));
        if (*page) (*page)[(a & 0xfff) >> 1] = value;
        return;
    }
    if (a >= 0xa00000 && a <= 0xa03fff) {
        uint16_t *p = &c->v.ram3[(a & 0x3fff) >> 1];
        const uint16_t old = *p;
        // Road Runner: a motion object of the current bank gaining or losing
        // its INT3 mark moves the interrupt; any other change of the current
        // bank first finishes the lines drawn so far (MAME: vpos + 2)
        if (c->game == AS1_ROADRUNN && old != value && (a & 0x3000) == 0x2000) {
            const int so = (int)((a & 0xfff) >> 1);
            const uint16_t *spr = &c->v.ram3[0x1000];
            if ((so >> 8) == (int)c->v.mo_bank) {
                if (((so & 0xc0) == 0x00 && spr[so | 0x40] == 0xffff) ||
                    ((so & 0xc0) == 0x40 && (value == 0xffff || old == 0xffff))) {
                    *p = value;
                    reschedule_int3(c, c->line);
                } else {
                    mo_partial(c, c->line + 3);
                }
            }
        }
        *p = value;
        return;
    }
    if (a >= 0xb00000 && a <= 0xb007ff) {
        int index = (int)((a & 0x7ff) >> 1);
        if (c->v.palette[index] != value) {
            c->v.palette[index] = value;
            c->v.palette_set[index >> 3] |= (uint8_t)(1 << (index & 7));
        }
        return;
    }
    if (a >= 0xf00000 && a <= 0xf00fff) {
        if (c->write_eeprom) {
            uint8_t *e = &c->eeprom[(a & 0xfff) >> 1];
            if (*e != (uint8_t)value) c->eeprom_dirty = true;
            *e = (uint8_t)value;
            c->write_eeprom = false;
        }
        return;
    }
    if (a >= 0xf40000 && a <= 0xf4001f) { adc_start(c, a); return; }
    if ((a & ~1u) == 0xf80000 || (a & ~1u) == 0xfe0000) {
        c->sound_latch = (uint8_t)value;
        c->sound_pending = true;
        dsp6502_set_nmi(&c->snd, LINE_ASSERT);
        return;
    }
}

unsigned int AS1_HOT cpu_read_word(unsigned int a) { return main_read(g_core, a & ~1u); }

unsigned int AS1_HOT cpu_read_byte(unsigned int a)
{
    uint16_t w = main_read(g_core, a & ~1u);
    return (a & 1) ? (w & 0xff) : (w >> 8);
}

unsigned int AS1_HOT cpu_read_long(unsigned int a)
{
    uint32_t hi = main_read(g_core, a & ~1u);
    return (hi << 16) | main_read(g_core, (a + 2) & ~1u);
}

void AS1_HOT cpu_write_word(unsigned int a, unsigned int d) { main_write(g_core, a & ~1u, (uint16_t)d); }

void AS1_HOT cpu_write_byte(unsigned int a, unsigned int d)
{
    // As dsp-cpp's 68000 does it: read the word, merge, write it back.
    uint16_t old = main_read(g_core, a & ~1u);
    if (a & 1) main_write(g_core, a & ~1u, (uint16_t)((old & 0xff00) | (d & 0xff)));
    else main_write(g_core, a & ~1u, (uint16_t)((old & 0x00ff) | ((d & 0xff) << 8)));
}

void AS1_HOT cpu_write_long(unsigned int a, unsigned int d)
{
    main_write(g_core, a & ~1u, (uint16_t)(d >> 16));
    main_write(g_core, (a + 2) & ~1u, (uint16_t)d);
}

void cpu_write_long_pd(unsigned int a, unsigned int d) { cpu_write_long(a, d); }

// Opcode and extension word fetches: dsp-cpp's pc() is the fetch address.
unsigned int AS1_HOT m68k_read_immediate_16(unsigned int a)
{
    as1_core *c = g_core;
    if ((a & 0xffffff) <= 0x7ffff) return rom_word(c, a & 0x7fffe);      // no SLAPSTIC there
    c->fetching = true;
    c->fetch_pc = a;
    unsigned int v = main_read(c, a & ~1u);
    c->fetching = false;
    return v;
}

unsigned int AS1_HOT m68k_read_immediate_32(unsigned int a)
{
    as1_core *c = g_core;
    if ((a & 0xffffff) <= 0x7fffc) return ((uint32_t)rom_word(c, a & 0x7fffe) << 16) | rom_word(c, (a + 2) & 0x7fffe);
    c->fetching = true;
    c->fetch_pc = a;
    uint32_t hi = main_read(c, a & ~1u);
    c->fetch_pc = a + 2;
    uint32_t lo = main_read(c, (a + 2) & ~1u);
    c->fetching = false;
    return (hi << 16) | lo;
}

unsigned int m68k_read_pcrelative_8(unsigned int a) { return cpu_read_byte(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return cpu_read_word(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return cpu_read_long(a); }

static uint16_t peek_word(const as1_core *c, uint32_t a)
{
    a &= 0xfffffe;
    if (a <= 0x7ffff) return rom_word(c, a);
    if (a <= 0x87fff) return slap_word(c, a);
    if (a >= 0x400000 && a <= 0x401fff) return c->ram[(a & 0x1fff) >> 1];
    return 0xffff;
}

unsigned int cpu_read_word_dasm(unsigned int a) { return peek_word(g_core, a); }
unsigned int cpu_read_long_dasm(unsigned int a)
{
    return ((uint32_t)peek_word(g_core, a) << 16) | peek_word(g_core, a + 2);
}

// SLAPSTIC alternate-bank kludge: the 68000 as dsp-cpp's core shows it.
static uint32_t s_pc(void *u)
{
    as1_core *c = (as1_core *)u;
    return c->fetching ? c->fetch_pc : m68k_get_reg(NULL, M68K_REG_PC);
}
static uint32_t s_ppc(void *u) { (void)u; return m68k_get_reg(NULL, M68K_REG_PPC); }
static uint16_t s_peek(void *u, uint32_t a) { return peek_word((as1_core *)u, a); }
static uint32_t s_areg(void *u, int n) { (void)u; return m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + n)); }

// ---------------------------------------------------------------------------
// 6502 sound board
// ---------------------------------------------------------------------------

static inline bool via_selected(uint16_t a)
{
    uint16_t base = (uint16_t)(a & ~0x27f0);
    return base >= 0x1000 && base <= 0x100f;
}

static void sync_pokey(as1_core *c)
{
    if (c->pokey_pending > 0) {
        pokey_run(&c->pk, c->pokey_pending);
        c->pokey_pending = 0;
    }
}

static inline void flush_via_tms(as1_core *c)
{
    if (c->via_pending) {
        via6522_tick(&c->via, c->via_pending);
        c->via_pending = 0;
    }
    if (c->tms_pending) {
        tms5220_tick(&c->tms, c->tms_pending);
        c->tms_pending = 0;
    }
}

static inline void ym_recompute_due(as1_core *c)
{
    int32_t d = INT32_MAX;
    if (c->ym.timer_a_enabled && c->ym.timer_a_counter < d) d = c->ym.timer_a_counter;
    if (c->ym.timer_b_enabled && c->ym.timer_b_counter < d) d = c->ym.timer_b_counter;
    c->ym_due = d;
}

static inline void flush_ym(as1_core *c)
{
    if (c->ym_pending) {
        ym2151_run_timers(&c->ym, c->ym_pending);
        c->ym_pending = 0;
    }
    ym_recompute_due(c);
}

static inline uint8_t AS1_HOT sound_read(void *u, uint16_t a)
{
    as1_core *c = (as1_core *)u;
    if ((a & ~0x2000) <= 0x0fff) return c->sound_ram[a & 0x0fff];
    if (a >= 0x4000) return c->roms.sound[a - 0x4000];
    if (via_selected(a)) {
        flush_via_tms(c);
        return via6522_read(&c->via, (uint8_t)(a & 0x0f));
    }
    if ((a & ~0x278e) == 0x1800 || (a & ~0x278e) == 0x1801) return ym2151_status(&c->ym);
    if ((a & ~0x278f) == 0x1810) {
        c->sound_pending = false;
        dsp6502_set_nmi(&c->snd, LINE_CLEAR);
        return c->sound_latch;
    }
    if ((a & ~0x278f) == 0x1820) {
        uint8_t v = (uint8_t)(c->in2 | (c->sound_pending ? 0x08 : 0) | (c->main_pending ? 0x10 : 0));
        if ((c->in0 & 0x0040) == 0) v ^= 0x80;
        return v;
    }
    if ((a & ~0x2780) >= 0x1870 && (a & ~0x2780) <= 0x187f) {
        sync_pokey(c);
        return pokey_read(&c->pk, a & 0x0f);
    }
    return 0xff;
}

static inline void AS1_HOT sound_write(void *u, uint16_t a, uint8_t value)
{
    as1_core *c = (as1_core *)u;
    if ((a & ~0x2000) <= 0x0fff) { c->sound_ram[a & 0x0fff] = value; return; }
    if (via_selected(a)) {
        flush_via_tms(c);
        via6522_write(&c->via, (uint8_t)(a & 0x0f), value);
        return;
    }
    if ((a & ~0x278e) == 0x1800) { ym2151_select(&c->ym, value); return; }
    if ((a & ~0x278e) == 0x1801) {
        if (c->ym_write) c->ym_write(c->user, c->ym.last_register, value, c->sample_pos);
        flush_ym(c);
        ym2151_write(&c->ym, value);
        ym_recompute_due(c);
        return;
    }
    if ((a & ~0x278f) == 0x1810) {
        c->main_latch = value;
        c->main_pending = true;
        main_irq(c, 6, LINE_ASSERT);
        return;
    }
    uint16_t outlatch = (uint16_t)(a & ~0x2788);
    if (outlatch >= 0x1820 && outlatch <= 0x1827) return;
    if ((a & ~0x2780) >= 0x1870 && (a & ~0x2780) <= 0x187f) {
        sync_pokey(c);
        pokey_write(&c->pk, a & 0x0f, value);
    }
}

static inline void AS1_HOT on_sound_cycles(void *u, int cycles)
{
    as1_core *c = (as1_core *)u;
    c->ym_pending += cycles * 2;
    if (c->ym_pending >= c->ym_due) flush_ym(c);     // a timer expires on this instruction
    c->pokey_pending += cycles;
    c->via_pending += cycles;
    if (c->tms.clock != c->tms_table_clock) {
        c->tms_table_clock = c->tms.clock;
        for (int n = 0; n < 16; ++n)
            c->tms_clocks[n] = (uint16_t)(((uint64_t)n * c->tms_table_clock + (AS1_SOUND_CLOCK / 2)) / AS1_SOUND_CLOCK);
    }
    c->tms_pending += cycles < 16 ? c->tms_clocks[cycles]
        : (int)(((int64_t)cycles * (int64_t)c->tms.clock + (AS1_SOUND_CLOCK / 2)) / AS1_SOUND_CLOCK);
    // While /READY is held after a strobe the chip is run instruction by instruction.
    if (c->tms.io_ready_delay > 0) flush_via_tms(c);
    if (!c->audio_enabled) return;
    c->audio_acc += cycles * (int32_t)c->sample_rate;
    if (c->audio_acc < (int32_t)AS1_SOUND_CLOCK) return;
    sync_pokey(c);
    flush_via_tms(c);
    while (c->audio_acc >= (int32_t)AS1_SOUND_CLOCK) {
        c->audio_acc -= AS1_SOUND_CLOCK;
        int32_t s = pokey_update(&c->pk) + tms5220_last_sample(&c->tms);
        if (!c->ym_external) s += ym2151_update(&c->ym);
        if (s < -32768) s = -32768; else if (s > 32767) s = 32767;
        if (c->audio && c->audio_n < c->audio_cap) c->audio[c->audio_n++] = (int16_t)s;
        c->sample_pos++;
    }
}

// The sound program's main loop polls for work 17 instructions at a time
// (Indy at $40F1, 54 cycles; Road Runner the same code at $80EC, 53 cycles,
// counter in $28):
//   40F1 LDA $02 / AND #$FE / STA $02 / SEI / INC $1C / BNE 4103 / (INC, JSR)
//   4103 CLI / LDA #$10 / BIT $1820 / BNE (main CPU wrote) / LDY $224 / CPY $225
//   / BEQ 4123 / ... 4123 LDX $210 / CPX $211 / BEQ 413B / ... 413B JMP $40F1
// With nothing to do and no interrupt able to arrive (the YM2151 timers are the
// only source while the 6502 runs), a pass changes only the counter; the
// registers and flags come out as they went in. Passes are replayed here: the
// counter and the chips' per-instruction work (same cycles, same order)
// without decoding them.
typedef struct {
    uint16_t pc, jmp;               // loop start, its closing JMP
    uint8_t counter;                // zero page counter it increments
    uint8_t pass;                   // cycles of a pass
    uint8_t cycles[17];
} sound_poll;
static const sound_poll poll_indy = {0x40f1, 0x413b, 0x1c, 54, {3, 2, 3, 2, 5, 4, 2, 2, 4, 2, 4, 4, 3, 4, 4, 3, 3}};
static const sound_poll poll_rr = {0x80ec, 0x8136, 0x28, 53, {3, 2, 3, 2, 5, 3, 2, 2, 4, 2, 4, 4, 3, 4, 4, 3, 3}};

static int AS1_HOT sound_idle(void *u, int budget)
{
    as1_core *c = (as1_core *)u;
    const sound_poll *sp = c->game == AS1_ROADRUNN ? &poll_rr : &poll_indy;
    const uint8_t *ram = c->sound_ram;
    if (!c->idle_skip || c->snd.ppc != sp->jmp) return 0;   // after a real pass
    if (c->snd.irq_request != LINE_CLEAR || c->snd.nmi_request != LINE_CLEAR || c->snd.fi) return 0;
    if (c->main_pending || (ram[0x02] & 1)) return 0;
    if (ram[0x224] != ram[0x225] || ram[0x210] != ram[0x211]) return 0;
    uint8_t count = ram[sp->counter];
    int done = 0;
    while (count != 0xff && done + sp->pass < budget && c->ym_pending + 2 * sp->pass < c->ym_due) {
        for (int i = 0; i < 17; i++) on_sound_cycles(c, sp->cycles[i]);
        count++;
        done += sp->pass;
    }
    c->sound_ram[sp->counter] = count;
    c->snd_skipped += (uint32_t)done;
    return done;
}

// The 6502 core, compiled here with the sound board's handlers inlined.
#define DSP6502_READ(u, a) sound_read(u, a)
#define DSP6502_WRITE(u, a, v) sound_write(u, a, v)
#define DSP6502_CYCLES(u, n) on_sound_cycles(u, n)
#include "dsp6502.c"

static void ym_irq(void *u, bool on)
{
    as1_core *c = (as1_core *)u;
    dsp6502_set_irq(&c->snd, on ? LINE_HOLD : LINE_CLEAR);
}

static uint8_t via_pa_in(void *u) { return tms5220_status(&((as1_core *)u)->tms); }
static void via_pa_out(void *u, uint8_t v)
{
    as1_core *c = (as1_core *)u;
    tms5220_set_data_latch(&c->tms, v);
    tms5220_write_data(&c->tms, v);
}
static uint8_t via_pb_in(void *u)
{
    as1_core *c = (as1_core *)u;
    uint8_t v = 0;
    if (tms5220_readyq(&c->tms)) v |= 0x04;
    if (tms5220_intq(&c->tms)) v |= 0x08;
    return v;
}
static void via_pb_out(void *u, uint8_t v)
{
    as1_core *c = (as1_core *)u;
    tms5220_set_wsq(&c->tms, (v & 0x01) != 0);
    tms5220_set_rsq(&c->tms, (v & 0x02) != 0);
    int div = 5 | ((v >> 3) & 2);
    tms5220_set_clock(&c->tms, AS1_ATARI_CLOCK / 2 / (uint32_t)(16 - div));
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

void as1_init(as1_core *c, const as1_roms *roms, uint32_t sample_rate)
{
    memset(c, 0, sizeof *c);
    g_core = c;
    c->roms = *roms;
    c->game = roms->game;
    c->gfx_bytes = as1_gfx_bytes(roms->game);
    if (c->game == AS1_ROADRUNN) {
        c->frame_mo = (as1_mo_list *)calloc(1, sizeof *c->frame_mo);
        c->v.pens_per_line = true;
    }
    for (int i = 0; i < AS1_ROM_PAGES; i++) c->main_page[i] = roms->main + (size_t)i * 1024;
    c->sample_rate = sample_rate ? sample_rate : 44100;
    c->audio_enabled = true;
    make_char_pens();

    c->gfx.roms = &c->roms;
    convert_background(&c->gfx, roms->proms);
    for (int i = 0; i < 256; i++) {
        as1_gfx *g = &c->gfx;
        const uint16_t lookup = g->playfield_lookup[i];
        int gfx = (lookup >> 8) & 0xf;
        const int shift = g->bank_color_shift[gfx];
        const int color = 0x20 + ((((lookup >> 12) & 0xf) << shift));
        if (!gfx_valid(g, gfx) || gfx == 0) gfx = 1;
        g->pf[i].tiles = c->roms.tiles[g->gfx_bank[gfx] & 7];
        g->pf[i].base = (uint16_t)(0x100 + (color << 3));
        g->pf[i].codehi = (uint16_t)((lookup & 0xff) << 8);
    }

    dsp6502_init(&c->snd);
    c->snd.read = sound_read;
    c->snd.write = sound_write;
    c->snd.cycles = on_sound_cycles;
    c->snd.user = c;
    c->snd.idle = sound_idle;                // the polling loop of the sound program
    c->snd.idle_pc = c->game == AS1_ROADRUNN ? poll_rr.pc : poll_indy.pc;
    for (int page = 0x40; page < 0x100; ++page) dsp6502_set_page(&c->snd, (uint8_t)page, roms->sound + (page - 0x40) * 0x100, NULL);
    for (int page = 0; page < 0x10; ++page) {
        dsp6502_set_page(&c->snd, (uint8_t)page, c->sound_ram + page * 0x100, c->sound_ram + page * 0x100);
        dsp6502_set_page(&c->snd, (uint8_t)(0x20 + page), c->sound_ram + page * 0x100, c->sound_ram + page * 0x100);
    }

    c->ym.irq_handler = ym_irq;
    c->ym.user = c;
    ym2151_init(&c->ym, AS1_YM_CLOCK, c->sample_rate);
    pokey_init(&c->pk);
    c->via.pa_in = via_pa_in;
    c->via.pa_out = via_pa_out;
    c->via.pb_in = via_pb_in;
    c->via.pb_out = via_pb_out;
    c->via.user = c;
    via6522_reset(&c->via);
    tms5220_init(&c->tms, AS1_ATARI_CLOCK / 2 / 11, false);

    slapstic_cpu sc = {s_pc, s_ppc, s_peek, s_areg, c};
    slapstic_init(&c->slap, c->game == AS1_ROADRUNN ? 108 : 105, &sc);

    memset(c->eeprom, 0xff, sizeof c->eeprom);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68010);
    as1_reset(c);
}

void as1_reset(as1_core *c)
{
    g_core = c;
    slapstic_reset(&c->slap);
    c->rom_bank = c->slap.current_bank;
    memset(c->irq, 0, sizeof c->irq);
    as1_ipl_dirty = false;
    c->in_main = false;
    m68k_pulse_reset();
    m68k_set_irq(0);
    dsp6502_reset(&c->snd);
    ym2151_reset(&c->ym);
    pokey_reset(&c->pk);
    c->pokey_pending = 0;
    c->via_pending = c->tms_pending = 0;
    c->ym_pending = 0;
    ym_recompute_due(c);
    via6522_reset(&c->via);
    tms5220_reset(&c->tms);
    c->in0 = 0xff6f;
    c->in2 = 0x87;
    c->joy_bits = 0;
    c->analog_x = c->analog_y = 0x80;
    c->int3_line = c->int3_off_line = -1;
    c->int3_state = false;
    c->adc_channel = c->adc_value = 0;
    c->adc_irq_enable = true;
    c->adc_busy = false;
    c->scroll_x = c->scroll_y = c->scroll_y_latch = 0;
    memset(c->v.scroll_x, 0, sizeof c->v.scroll_x);
    memset(c->v.scroll_y, 0, sizeof c->v.scroll_y);
    c->vblank = 0x10;
    c->bankselect = 0;
    c->v.tile_bank = 0;
    c->v.priority_pens = 0;
    c->v.mo_bank = 0;
    c->write_eeprom = false;
    c->sound_pending = c->main_pending = false;
    c->main_latch = c->sound_latch = 0;
    c->sound_halted = false;
    c->line = 0;
    c->audio_acc = 0;
    c->sample_pos = 0;
}

void as1_set_inputs(as1_core *c, const as1_inputs *in)
{
    c->in0 = 0xff6f;
    c->in2 = 0x87;
    if (in->button1) c->in0 &= (uint16_t)~0x0001;
    if (in->start) c->in0 &= (uint16_t)~0x0002;
    if (in->button2) c->in0 &= (uint16_t)~0x0004;
    if (c->game == AS1_ROADRUNN && in->button2) c->in0 &= (uint16_t)~0x0002;
    if (in->coin1) c->in2 &= (uint8_t)~0x01;
    if (in->coin2) c->in2 &= (uint8_t)~0x02;
    c->joy_bits = 0;
    if (in->up) c->joy_bits |= 0x10;
    if (in->down) c->joy_bits |= 0x20;
    if (in->left) c->joy_bits |= 0x40;
    if (in->right) c->joy_bits |= 0x80;
    if (in->analog_x || in->analog_y) {
        c->analog_x = in->analog_x;
        c->analog_y = in->analog_y;
    } else {
        c->analog_y = in->up == in->down ? 0x80 : in->up ? 0x10 : 0xf0;
        c->analog_x = in->left == in->right ? 0x80 : in->left ? 0xf0 : 0x10;   // PORT_REVERSE
    }
}

void as1_run_frame(as1_core *c)
{
    const int main_cycles = (int)((double)AS1_MAIN_CLOCK / AS1_FPS / AS1_SCANLINES + 0.5);
    const int sound_cycles = (int)((double)AS1_SOUND_CLOCK / AS1_FPS / AS1_SCANLINES + 0.5);
    g_core = c;
    c->audio_n = 0;
    reschedule_int3(c, -1);
    if (c->frame_mo) { c->frame_mo->count = 0; c->mo_line = 0; }
    for (c->line = 0; c->line < AS1_SCANLINES; c->line++) {
        c->in_main = true;
        c->slice++;
        m68k_execute(main_cycles);
        c->in_main = false;
#ifdef AS1_TRACE_SLICES
        { extern void trace_slice(as1_core *c); trace_slice(c); }
#endif
        if (as1_ipl_dirty) {
            as1_ipl_dirty = false;
            apply_ipl(c);
        }
        if (!c->sound_halted) dsp6502_run(&c->snd, sound_cycles);
        adc_complete(c);
        c->v.scroll_x[c->line] = c->scroll_x;
        c->v.scroll_y[c->line] = c->scroll_y;
        if (c->line < AS1_SCREEN_H) c->v.pens_line[c->line] = c->v.priority_pens;
        if (c->int3_off_line == c->line) {
            c->int3_state = false;
            c->int3_off_line = -1;
            main_irq(c, 3, LINE_CLEAR);
        }
        if (c->int3_line == c->line) {
            c->int3_state = true;
            main_irq(c, 3, LINE_ASSERT);
            c->int3_off_line = (c->line + 1) % AS1_SCANLINES;
            reschedule_int3(c, c->line);
        }
        if (c->line == 239) {
            mo_partial(c, AS1_SCREEN_H);
            if (c->on_frame) c->on_frame(c->user, c);
            c->vblank = 0;
            main_irq(c, 4, LINE_ASSERT);
        }
        if (c->line == 261) c->vblank = 0x10;
    }
    c->scroll_y = c->scroll_y_latch;
    c->line = 0;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

typedef struct {
    const as1_gfx *g;
    as1_mo_list *list;
    int y0, y1;
} mo_ctx;

static int mo_code(void *u, int raw)
{
    const as1_gfx *g = ((mo_ctx *)u)->g;
    return (raw & 0xff) | ((g->motable[(raw >> 8) & 0xff] & 0xff) << 8);
}
static int mo_color(void *u, int index)
{
    const as1_gfx *g = ((mo_ctx *)u)->g;
    return index < 256 ? (g->motable[index] >> 12) & 0xf : index;
}
static int mo_gfx(void *u, int hi) { return (((mo_ctx *)u)->g->motable[hi & 0xff] >> 8) & 0xf; }

static void mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx, int priority)
{
    mo_ctx *m = (mo_ctx *)u;
    as1_mo_list *l = m->list;
    if (l->count >= AS1_MO_MAX_TILES) return;
    if (y + 8 <= m->y0 || y >= m->y1) return;
    as1_mo_tile *t = &l->t[l->count++];
    t->x = (int16_t)x;
    t->y = (int16_t)y;
    t->code = (uint16_t)code;
    t->color = (uint16_t)color;
    t->gfx = (uint8_t)gfx;
    t->hflip = hflip;
    t->vflip = vflip;
    t->priority = (uint8_t)priority;
    t->ymin = (int16_t)m->y0;
    t->ymax = (int16_t)m->y1;
}

static const atari_mo_config mo_config = {
    .tile_width = 8, .tile_height = 8, .bankcount = 8, .linked = true, .split = true,
    .maxperline = 0x38, .palettebase = 0x100,
    .link_entry = {0, 0, 0, 0x003f},
    .code_entry = {{0, 0xffff, 0, 0}, {0, 0, 0, 0}},
    .color_entry = {{0, 0xff00, 0, 0}, {0, 0, 0, 0}},
    .xpos_entry = {0, 0, 0x3fe0, 0},
    .ypos_entry = {0x3fe0, 0, 0, 0},
    .height_entry = {0x000f, 0, 0, 0},
    .hflip_entry = {0x8000, 0, 0, 0},
    .priority_entry = {0, 0, 0x8000, 0},
    .special_entry = {0, 0xffff, 0, 0},
    .specialvalue = 0xffff,
};

void as1_build_mo_band(const as1_gfx *g, const as1_video *v, as1_mo_list *list, int y0, int y1)
{
    atari_mo *mo = &list->mo;
    if (!list->mo_ready) {
        atari_mo_init(mo, &mo_config, AS1_SCREEN_W + 8, AS1_SCREEN_H + 8);
        list->mo_ready = true;
    }
    mo->bank = v->mo_bank;
    mo_ctx ctx = {g, list, y0, y1};
    const atari_mo_lookup lk = {mo_code, mo_color, mo_gfx};
    atari_mo_draw(mo, &v->ram3[0x2000 >> 1], 0, 256, -1, &lk, mo_tile, &ctx);
}

void as1_build_mo_list(const as1_gfx *g, const as1_video *v, as1_mo_list *list)
{
    atari_mo *mo = &list->mo;
    if (!list->mo_ready) {
        atari_mo_init(mo, &mo_config, AS1_SCREEN_W + 8, AS1_SCREEN_H + 8);
        list->mo_ready = true;
    }
    mo->bank = v->mo_bank;
    list->count = 0;
    mo_ctx ctx = {g, list, -1000, 1000};
    const atari_mo_lookup lk = {mo_code, mo_color, mo_gfx};
    atari_mo_draw(mo, &v->ram3[0x2000 >> 1], 0, 256, -1, &lk, mo_tile, &ctx);
}

static inline void mix_mo_pixel(uint16_t pens, uint16_t *pf, uint16_t mo)
{
    if (mo & 0xf000) {
        if ((mo & 0x0f) != 1) *pf = (uint16_t)(0x300 + ((*pf & 0x0f) << 4) + (mo & 0x0f));
    } else if ((*pf & 0xf8) != 0 || (pens & (1u << (*pf & 7))) == 0) {
        *pf = (uint16_t)(mo & 0x0fff);
    }
}

void AS1_HOT as1_compose(const as1_gfx *g, const as1_video *v, const as1_mo_list *mo,
                           int y0, int y1, uint16_t *out)
{
    const int W = AS1_SCREEN_W;
    // Playfield: 64x64 tiles of 8x8 on a 512x512 map, scrolled per line.
    const as1_pf_info *pfi = g->pf + ((unsigned)v->tile_bank << 7);
    for (int y = y0; y < y1; y++) {
        uint16_t *dst = out + (size_t)(y - y0) * W;
        const int sy = (int)v->scroll_y[y] & 511;
        const int py = (y + sy) & 511;
        const uint16_t *maprow = v->ram3 + (py >> 3) * 64;
        int px = (int)v->scroll_x[y] & 511;
        int x = 0;
        while (x < W) {
            const uint16_t atrib = maprow[(px >> 3) & 63];
            const as1_pf_info *pi = &pfi[(atrib >> 8) & 0x7f];
            uint32_t row = pi->tiles ? pi->tiles[(unsigned)((pi->codehi | (atrib & 0xff)) & 0xfff) * 8 + (unsigned)(py & 7)] : 0;
            if (atrib & 0x8000) {   // hflip: reverse the 8 nibbles
                row = ((row >> 4) & 0x0f0f0f0fu) | ((row & 0x0f0f0f0fu) << 4);
                row = ((row >> 8) & 0x00ff00ffu) | ((row & 0x00ff00ffu) << 8);
                row = (row >> 16) | (row << 16);
            }
            const unsigned base = pi->base;
            const int first = px & 7;
            if (first == 0 && x + 8 <= W) {
                dst[0] = (uint16_t)((base + (row & 15)) & 0x3ff);
                dst[1] = (uint16_t)((base + ((row >> 4) & 15)) & 0x3ff);
                dst[2] = (uint16_t)((base + ((row >> 8) & 15)) & 0x3ff);
                dst[3] = (uint16_t)((base + ((row >> 12) & 15)) & 0x3ff);
                dst[4] = (uint16_t)((base + ((row >> 16) & 15)) & 0x3ff);
                dst[5] = (uint16_t)((base + ((row >> 20) & 15)) & 0x3ff);
                dst[6] = (uint16_t)((base + ((row >> 24) & 15)) & 0x3ff);
                dst[7] = (uint16_t)((base + (row >> 28)) & 0x3ff);
                dst += 8; x += 8; px += 8;
                continue;
            }
            int count = 8 - first;
            if (count > W - x) count = W - x;
            row >>= 4 * first;
            for (int col = 0; col < count; col++, row >>= 4) dst[col] = (uint16_t)((base + (row & 15)) & 0x3ff);
            dst += count; x += count; px += count;
        }
    }

    // Motion objects, in drawing order, the rows of this band.
    for (int i = 0; i < mo->count; i++) {
        const as1_mo_tile *t = &mo->t[i];
        if (t->y + 8 <= y0 || t->y >= y1) continue;
        int set = (gfx_valid(g, t->gfx) && t->gfx > 0) ? t->gfx : (gfx_valid(g, 1) ? 1 : 0);
        for (int r = 0; r < 8; r++) {
            const int ty = t->y + r;
            if (ty < y0 || ty >= y1 || ty < 0 || ty >= AS1_SCREEN_H || ty < t->ymin || ty >= t->ymax) continue;
            const uint16_t pens_mask = v->pens_per_line ? v->pens_line[ty] : v->priority_pens;
            uint32_t row = set ? tile_row4(g, set, t->code, t->vflip ? 7 - r : r) : 0;
            if (!row) continue;
            uint16_t *line = out + (size_t)(ty - y0) * W;
            for (int col = 0; col < 8; col++) {
                const int tx = t->x + col;
                const int pen = (row >> (4 * (t->hflip ? 7 - col : col))) & 15;
                if (tx < 0 || tx >= W || pen == 0) continue;
                const uint16_t m = (uint16_t)(((t->color + pen) & 0x0fff) | ((t->priority ? 1 : 0) << 12));
                mix_mo_pixel(pens_mask, &line[tx], m);
            }
        }
    }

    // Alphanumerics over the top.
    const uint8_t *chars = g->roms->chars;
    for (int y = y0; y < y1; y++) {
        uint16_t *line = out + (size_t)(y - y0) * W;
        for (int cx = 0; cx < W / 8; cx++) {
            const uint16_t atrib = v->ram3[(0x3000 >> 1) + (y >> 3) * 64 + cx];
            const bool opaque = (atrib & 0x2000) != 0;
            const uint8_t *r = chars + (size_t)(atrib & 0x1ff) * 16 + (size_t)(y & 7) * 2;
            if (!opaque && r[0] == 0 && r[1] == 0) continue;
            const int base = ((atrib >> 10) & 7) << 2;
            uint32_t lo = char_pens[r[0]], hi = char_pens[r[1]];
            uint16_t *cell = line + cx * 8;
            for (int col = 0; col < 8; col++) {
                uint8_t pen = (uint8_t)((col < 4 ? lo >> (8 * col) : hi >> (8 * (col - 4))) & 0xff);
                if (opaque || pen != 0) cell[col] = (uint16_t)((base + pen) & 0x3ff);
            }
        }
    }
}

uint32_t as1_palette_rgb(const as1_video *v, int index)
{
    // IRGB 4444 as MAME's palette_device::IRGB_4444 (standard_irgb_decoder):
    // each 4-bit value expanded to 8 bits, colour = intensity * value / 256.
    // (dsp-cpp used the old intensity table 0, 3, 4 .. 0x11, too bright at
    // low intensities: Road Runner's dark tunnel interiors showed grey.)
    index &= 0x3ff;
    if (!(v->palette_set[index >> 3] & (1 << (index & 7)))) return 0;
    uint16_t val = v->palette[index];
    uint32_t i = (uint32_t)(val >> 12) * 17;
    uint32_t r = (i * ((val >> 8) & 15) * 17) >> 8, g = (i * ((val >> 4) & 15) * 17) >> 8, b = (i * (val & 15) * 17) >> 8;
    return (r << 16) | (g << 8) | b;
}
