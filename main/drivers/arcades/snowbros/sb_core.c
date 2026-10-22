// sb_core.c - Snow Bros core (port of zxtiny/zxm/snowbros.c, no SDL).
// Same memory map, interrupt scheme, sound CPU/YM3812 coupling and sprite
// renderer as the desktop driver.
#include "sb_core.h"
#include "rom_data.h"
#include "m68k/m68k.h"
#include "z80/z80.h"
#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define HOT IRAM_ATTR
#else
#define HOT
#endif

#define SB_RAM_SIZE     0x4000
#define SB_PALRAM_SIZE  0x200
#define SB_SPR_SIZE     0x2000
#define SB_SNDRAM_SIZE  0x800
#define SB_TILES_NUM    (0x80000 / 128)

typedef struct {
    uint8_t ram[SB_RAM_SIZE];
    uint8_t palram[SB_PALRAM_SIZE];
    uint8_t spriteram[SB_SPR_SIZE];
    uint8_t sndram[SB_SNDRAM_SIZE];
    uint16_t pal555[256];
    uint8_t soundlatch;
    uint8_t sound_to_main;
    bool flip_screen;
    z80 cpu_snd;
    // YM3812 address latch + timer/status block (emulated on the sound CPU
    // side, clocked by Z80 cycles: 1 YM sample = 72 YM clocks = 144 Z80 clocks)
    uint8_t opl_addr;
    uint8_t opl_reg[5];
    uint8_t opl_status, opl_statusmask;
    uint8_t opl_st[2];         // timer running
    int32_t opl_tcnt[2];       // Z80 cycles until overflow
    int32_t opl_tper[2];       // period in Z80 cycles
    uint64_t zclock;           // total Z80 cycles executed
    int32_t zdebt;             // cycles run ahead by command handshakes
} sb_t;

static sb_t *S;

// 68000 program ROM seen through 1 KB pages: all point into flash, the hottest
// ones (measured on the host: 32 KB cover 99.8% of accesses) are copied to RAM
// on ESP32 to keep them out of the flash cache.
static const uint8_t *rom_page[256];
static const uint8_t hot_rom_pages[] = {1,0,30,2,31,43,50,5,14,15,25,39,27,29,6,4,33,26,8,7,
                                        28,46,16,77,40,9,13,45,252,11,10,64};
static const uint8_t *snd_rom = sb_snd_rom;
volatile uint16_t sb_input[3];
volatile uint32_t sb_palette_serial;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// Musashi callbacks required by m68kconf.h
void cpu_pulse_reset(void) {}
// Idle skip: "TST.W (A0) ; BEQ *-2" with (A0)==0 only spins until the next
// interrupt, which is raised after m68k_execute() returns -> end the slice.
// (Does not change program behaviour, only saves host time.)
void HOT cpu_instr_callback(unsigned int pc)
{
    if (pc < 0x3fffc && sb_prog_rom[pc] == 0x4a && sb_prog_rom[pc + 1] == 0x50 &&
        sb_prog_rom[pc + 2] == 0x67 && sb_prog_rom[pc + 3] == 0xfc) {
        unsigned a0 = m68k_get_reg(NULL, M68K_REG_A0) & 0xffffff;
        if (a0 >= 0x100000 && a0 <= 0x103ffe && rd16(&S->ram[a0 - 0x100000]) == 0)
            m68k_end_timeslice();
    }
}
void cpu_set_fc(int fc) { (void)fc; }
int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }

// ---------------------------------------------------------------------------
// Sound CPU + YM3812
//
// snowbros.c advances the Nuked OPL2 core twice per 256 Z80 cycles (discarding
// the samples) only to clock its timers; that makes the music tempo run ~1.5x
// fast and costs as much as the synthesis itself. Here the timer/status logic
// is emulated exactly on Z80 cycles (YM3812 timer rules, as in MAME fmopl) and
// every other register write is time-stamped and sent to the synthesizer.
// ---------------------------------------------------------------------------
#define Z80_PER_T1   480        // 80 us at 6 MHz
#define Z80_PER_T2   1920       // 320 us
#define Z80_PER_SMP  144        // 72 YM clocks @3 MHz = 144 Z80 clocks @6 MHz

uint32_t sb_sample_clock(void) { return (uint32_t)(S->zclock / Z80_PER_SMP); }

static void opl_irq_update(sb_t *s)
{
    if (s->opl_status & 0x80) z80_assert_irq(&s->cpu_snd, 0xff);
    else z80_clr_irq(&s->cpu_snd);
}

static void opl_status_set(sb_t *s, uint8_t flag)
{
    s->opl_status |= flag;
    if (!(s->opl_status & 0x80) && (s->opl_status & s->opl_statusmask)) s->opl_status |= 0x80;
}

static void opl_status_reset(sb_t *s, uint8_t flag)
{
    s->opl_status &= (uint8_t)~flag;
    if ((s->opl_status & 0x80) && !(s->opl_status & s->opl_statusmask)) s->opl_status &= 0x7f;
}

static void opl_timer_write(sb_t *s, uint8_t r, uint8_t v)
{
    s->opl_reg[r] = v;
    if (r == 2) { s->opl_tper[0] = (256 - v) * Z80_PER_T1; return; }
    if (r == 3) { s->opl_tper[1] = (256 - v) * Z80_PER_T2; return; }
    // r == 4
    if (v & 0x80) { opl_status_reset(s, 0x7f - 0x08); return; }
    uint8_t st1 = v & 1, st2 = (v >> 1) & 1;
    opl_status_reset(s, v & (0x78 - 0x08));
    s->opl_statusmask = (uint8_t)((~v) & 0x78);
    if (s->opl_st[1] != st2) { s->opl_st[1] = st2; s->opl_tcnt[1] = s->opl_tper[1]; }
    if (s->opl_st[0] != st1) { s->opl_st[0] = st1; s->opl_tcnt[0] = s->opl_tper[0]; }
}

static void opl_timers_advance(sb_t *s, int32_t cyc)
{
    for (int t = 0; t < 2; t++) {
        if (!s->opl_st[t]) continue;
        s->opl_tcnt[t] -= cyc;
        while (s->opl_tcnt[t] <= 0) {
            s->opl_tcnt[t] += s->opl_tper[t] > 0 ? s->opl_tper[t] : 256 * (t ? Z80_PER_T2 : Z80_PER_T1);
            opl_status_set(s, t ? 0x20 : 0x40);
        }
    }
}

static int32_t opl_cycles_to_event(sb_t *s)
{
    int32_t m = 0x7fffffff;
    for (int t = 0; t < 2; t++) if (s->opl_st[t] && s->opl_tcnt[t] < m) m = s->opl_tcnt[t];
    return m;
}

// Z80 idle loop "019C: EI ; JP 019C" waiting for interrupts (14 T per pass).
static inline bool z80_idle(sb_t *s)
{
    z80 *z = &s->cpu_snd;
    return (z->pc == 0x019c || z->pc == 0x019d) && !z->nmi_pending && !z->irq_pending &&
           sb_snd_rom[0x19c] == 0xfb && sb_snd_rom[0x19d] == 0xc3 &&
           sb_snd_rom[0x19e] == 0x9c && sb_snd_rom[0x19f] == 0x01;
}

static void HOT run_sound(sb_t *s, int32_t cycles)
{
    while (cycles > 0) {
        int32_t n = cycles > 256 ? 256 : cycles;
        int32_t ev = opl_cycles_to_event(s);
        if (ev > 0 && ev < n) n = ev;
        int32_t done;
        if (z80_idle(s)) {
            done = ((n + 13) / 14) * 14;           // whole EI+JP passes (14 T)
        } else {
            done = (int32_t)z80_step_n(&s->cpu_snd, (unsigned)n);
        }
        s->zclock += (uint64_t)done;
        opl_timers_advance(s, done);
        opl_irq_update(s);
        cycles -= done;
    }
}
// ---------------------------------------------------------------------------
// 68000 bus
// ---------------------------------------------------------------------------
static uint16_t HOT bus_read16(uint32_t a)
{
    sb_t *s = S;
    a &= 0xffffff;
    if (a <= 0x03fffe) return rd16(&rom_page[a >> 10][a & 1023]);
    if (a >= 0x100000 && a <= 0x103ffe) return rd16(&s->ram[a - 0x100000]);
    if (a == 0x300000) return s->sound_to_main;
    if (a >= 0x500000 && a <= 0x500004) return sb_input[(a - 0x500000) >> 1];
    if (a >= 0x600000 && a <= 0x6001fe) return rd16(&s->palram[a - 0x600000]);
    if (a >= 0x700000 && a <= 0x701ffe) return rd16(&s->spriteram[a - 0x700000]);
    return 0xffff;
}

static void HOT bus_write16(uint32_t a, uint16_t v)
{
    sb_t *s = S;
    a &= 0xffffff;
    if (a >= 0x100000 && a <= 0x103ffe) { wr16(&s->ram[a - 0x100000], v); return; }
    if (a == 0x200000) return;
    if (a == 0x300000) {
        s->soundlatch = (uint8_t)v;
        z80_pulse_nmi(&s->cpu_snd);
        run_sound(s, 512);          // as snowbros.c (lets the Z80 take the command)
        s->zdebt += 512;            // ...but keep 6 MHz on average
        return;
    }
    if (a == 0x400000) { s->flip_screen = (v & 0x8000) == 0; return; }
    if (a >= 0x600000 && a <= 0x6001fe) {
        unsigned o = a - 0x600000;
        wr16(&s->palram[o], v);
        s->pal555[o >> 1] = v & 0x7fff;
        sb_palette_serial++;
        return;
    }
    if (a >= 0x700000 && a <= 0x701ffe) { wr16(&s->spriteram[a - 0x700000], v); return; }
}

unsigned int HOT cpu_read_byte(unsigned int a) { uint16_t v = bus_read16(a & ~1u); return (a & 1) ? v & 255 : v >> 8; }
unsigned int HOT cpu_read_word(unsigned int a) { return bus_read16(a & ~1u); }
unsigned int HOT cpu_read_long(unsigned int a) { return ((uint32_t)bus_read16(a & ~1u) << 16) | bus_read16((a + 2) & ~1u); }
unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    uint16_t w = bus_read16(a & ~1u);
    w = (a & 1) ? (uint16_t)((w & 0xff00) | (v & 255)) : (uint16_t)((w & 255) | ((v & 255) << 8));
    bus_write16(a & ~1u, w);
}
void HOT cpu_write_word(unsigned int a, unsigned int v) { bus_write16(a & ~1u, (uint16_t)v); }
void HOT cpu_write_long(unsigned int a, unsigned int v) { bus_write16(a & ~1u, (uint16_t)(v >> 16)); bus_write16((a + 2) & ~1u, (uint16_t)v); }
void HOT cpu_write_long_pd(unsigned int a, unsigned int v) { bus_write16((a + 2) & ~1u, (uint16_t)v); bus_write16(a & ~1u, (uint16_t)(v >> 16)); }

// ---------------------------------------------------------------------------
// Z80 bus
// ---------------------------------------------------------------------------
static uint8_t HOT snd_read(void *u, uint16_t a)
{
    sb_t *s = u;
    if (a < 0x8000) return snd_rom[a];
    if (a < 0x8800) return s->sndram[a - 0x8000];
    return 0xff;
}
static void HOT snd_write(void *u, uint16_t a, uint8_t v)
{
    sb_t *s = u;
    if (a >= 0x8000 && a < 0x8800) s->sndram[a - 0x8000] = v;
}
static uint8_t HOT snd_in(z80 *z, uint16_t p)
{
    sb_t *s = z->userdata;
    switch (p & 255) {
    case 2: return (uint8_t)((s->opl_status & (s->opl_statusmask | 0x80)) | 0x06);
    case 4: return s->soundlatch;
    default: return 0xff;
    }
}
static void HOT snd_out(z80 *z, uint16_t p, uint8_t v)
{
    sb_t *s = z->userdata;
    switch (p & 255) {
    case 2: s->opl_addr = v; break;
    case 3:
        if (s->opl_addr >= 2 && s->opl_addr <= 4) {
            opl_timer_write(s, s->opl_addr, v);
            opl_irq_update(s);
        } else {
            sb_opl_write(sb_sample_clock(), s->opl_addr, v);
        }
        break;
    case 4: s->sound_to_main = v; break;
    }
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void sb_init(int audio_rate)
{
    if (!S) {
        S = calloc(1, sizeof(sb_t));
        for (int i = 0; i < 256; i++) rom_page[i] = &sb_prog_rom[i * 1024];
#ifdef ESP_PLATFORM
        uint8_t *hot = malloc(sizeof hot_rom_pages * 1024);
        if (hot)
            for (unsigned i = 0; i < sizeof hot_rom_pages; i++) {
                memcpy(hot + i * 1024, &sb_prog_rom[hot_rom_pages[i] * 1024], 1024);
                rom_page[hot_rom_pages[i]] = hot + i * 1024;
            }
        uint8_t *sr = malloc(0x8000);
        if (sr) { memcpy(sr, sb_snd_rom, 0x8000); snd_rom = sr; }
#endif
    }
    sb_t *s = S;
    memset(s, 0, sizeof(*s));
    s->sound_to_main = 3;
    sb_input[0] = sb_input[1] = 0x7fff;
    sb_input[2] = 0xffff;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    z80_init(&s->cpu_snd);
    s->cpu_snd.read_byte = snd_read;
    s->cpu_snd.write_byte = snd_write;
    s->cpu_snd.port_in = snd_in;
    s->cpu_snd.port_out = snd_out;
    s->cpu_snd.userdata = s;
    (void)audio_rate;
    sb_reset();
}

void sb_reset(void)
{
    sb_t *s = S;
    m68k_pulse_reset();
    z80_reset(&s->cpu_snd);
    s->opl_status = 0;
    s->opl_statusmask = 0x78;
    s->opl_st[0] = s->opl_st[1] = 0;
    s->opl_tper[0] = 256 * Z80_PER_T1;
    s->opl_tper[1] = 256 * Z80_PER_T2;
    s->zdebt = 0;
}

// Identical to snowbros_run_frame(): three slices, IRQ 4 / 3 / 2, sound CPU
// run after each slice.
static void run_sound_slice(sb_t *s, int32_t zc)
{
    int32_t pay = s->zdebt < zc ? s->zdebt : zc;
    s->zdebt -= pay;
    run_sound(s, zc - pay);
}

void HOT sb_run_frame(void)
{
    sb_t *s = S;
    int mc = (SB_CPU_CLOCK / SB_FPS) / 3, zc = (SB_SND_CLOCK / SB_FPS) / 3;
    m68k_execute(mc); m68k_set_irq(4); run_sound_slice(s, zc);
    m68k_execute(mc); m68k_set_irq(3); run_sound_slice(s, zc);
    m68k_execute(mc); m68k_set_irq(2); run_sound_slice(s, zc);
}

void sb_snapshot(sb_video_t *v)
{
    memcpy(v->spriteram, S->spriteram, sizeof v->spriteram);
    v->flip = S->flip_screen;
}

const uint16_t *sb_palette(void) { return S->pal555; }

// ---------------------------------------------------------------------------
// Video (same as snowbros_render(), into an 8-bit indexed buffer)
// Tile format: bit = (y/8)*512 + (y&7)*32 + (x/8)*256 + (x&7)*4, 4bpp,
// high nibble first.
// ---------------------------------------------------------------------------
static void HOT draw_tile(uint8_t *fb, bool flip, int code, int color, int sx, int sy, bool fx, bool fy)
{
    code &= SB_TILES_NUM - 1;
    const uint8_t *t = &sb_gfx_rom[code * 128];
    const uint8_t cbase = (uint8_t)(color << 4);
    for (int py = 0; py < 16; py++) {
        int y = sy + py - SB_VIS_Y0;
        int ty = fy ? 15 - py : py;
        if (!flip) { if ((unsigned)y >= SB_SCREEN_H) continue; }
        else { if ((unsigned)(223 - y) >= SB_SCREEN_H) continue; }
        // 16 pixels of this row: two 4-byte groups (x 0..7 and 8..15)
        const uint8_t *r0 = t + (ty >> 3) * 64 + (ty & 7) * 4;
        uint8_t row[16];
        for (int i = 0; i < 4; i++) {
            uint8_t b0 = r0[i], b1 = r0[32 + i];
            row[i * 2] = b0 >> 4;      row[i * 2 + 1] = b0 & 15;
            row[8 + i * 2] = b1 >> 4;  row[8 + i * 2 + 1] = b1 & 15;
        }
        for (int px = 0; px < 16; px++) {
            int pen = row[fx ? 15 - px : px];
            if (!pen) continue;
            int x = sx + px, yy = y;
            if (flip) { x = 255 - x; yy = 223 - y; }
            if ((unsigned)x < SB_SCREEN_W && (unsigned)yy < SB_SCREEN_H)
                fb[yy * SB_SCREEN_W + x] = (uint8_t)(cbase | pen);
        }
    }
}

void HOT sb_render(const sb_video_t *v, uint8_t *fb)
{
    memset(fb, 0xf0, SB_SCREEN_W * SB_SCREEN_H);
    const uint8_t *sr = v->spriteram;
    bool flip = v->flip;
    int x = 0, y = 0;
    for (int o = 0; o < 0x1e00; o += 16) {
        int tc = rd16(&sr[o + 6]), dx = rd16(&sr[o + 8]) & 255, dy = rd16(&sr[o + 10]) & 255;
        int cw = rd16(&sr[o + 12]), a = rd16(&sr[o + 14]);
        if (tc & 1) dx = -1 - (dx ^ 255);
        if (tc & 2) dy = -1 - (dy ^ 255);
        if (tc & 4) { x += dx; y += dy; } else { x = dx; y = dy; }
        if (x > 511) x &= 511;
        if (y > 511) y &= 511;
        if (x > -16 && x < 256 && y > 0 && y < 240)
            draw_tile(fb, flip, ((a & 0x3f) << 8) | (cw & 255), (tc >> 4) & 15, x, y, (a & 0x80) != 0, (a & 0x40) != 0);
    }
}
