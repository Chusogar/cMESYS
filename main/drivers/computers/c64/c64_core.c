// c64_core.c - Commodore 64, see c64_core.h (dsp-cpp's drivers/computers/c64.cpp
// and machine/c64_tape.cpp, t64_image.cpp).
#include "c64_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m65xx.h"
#include "mos6526.h"
#include "mos6569.h"
#include "sid6581.h"
#include "c1541.h"

#define CPU_CLOCK 985248
#define SCANLINES 312
#define CYCLES_PER_LINE 63
#define PRG_INJECT_FRAMES 120           // BASIC's cold start first (READY.)

// .tap (C64-TAPE-RAW), read from its pages as it plays (c64_tape.cpp)
typedef struct {
    zx_pbuf pb;
    bool loaded, playing, paused;
    uint8_t version;
    uint32_t pos, end;                  // the next pulse in the file
    int remaining;
} c64_tape;

struct c64 {
    m65xx cpu;
    bool drive_on;                  // the 1541 runs (on the boards: from the first disk on)
    uint16_t pal565[16];
    mos6569 vic;
    sid6581 sid;
    mos6526 cia1, cia2;
    c1541 drive;
    c64_tape tape;
    uint8_t *ram;                       // 64 KB
    const uint8_t *kernal, *basic, *chargen;
    uint8_t color_ram[0x400];
    uint8_t port_bits, port_val;        // the 6510's port
    bool tape_motor, tape_play;
    bool write_ram, read_ram_a, read_ram_e;
    uint8_t read_ram_d;                 // 0 RAM, 1 CHAR, 2 I/O
    bool cia_irq, vic_irq, cia_nmi, restore;
    uint8_t keyboard[8], keys[8];       // [column]: active low rows
    uint8_t joy1, joy2;
    uint8_t *pending_prg;               // a .prg waiting for READY.
    size_t pending_size;
    int boot_frames;
    int cpu_cycle_debt;
    int audio_rate;
    int64_t audio_acc;
    int16_t audio[C64_AUDIO_MAX];
    int naudio;
    uint8_t line_pixels[VIC_W];
    uint8_t screen[(C64_PIC_W + 1) / 2 * C64_PIC_H];   // 4 bits a pixel, low nibble left
    int crop_x, crop_y;
    const char *error;
};

// ---------------------------------------------------------------------------
// .tap
// ---------------------------------------------------------------------------
static uint32_t tape_next_pulse(c64_tape *t)
{
    if (t->pos >= t->end) return 0;
    const uint8_t b = zx_pb_byte(&t->pb, t->pos++);
    uint32_t cycles;
    if (b == 0) {
        if (t->version == 0) cycles = 256u * 8u;
        else {
            if (t->pos + 3 > t->end) { t->pos = t->end; return 0; }
            cycles = zx_pb_rd24(&t->pb, t->pos);
            t->pos += 3;
        }
    } else {
        cycles = (uint32_t)b * 8u;
    }
    return cycles ? cycles : 1;
}

static void tape_play(c64_tape *t, bool restart)
{
    if (!t->loaded) return;
    if (restart || t->pos >= t->end) {
        t->pos = 20;
        t->remaining = 0;
    }
    if (t->remaining <= 0) t->remaining = (int)tape_next_pulse(t);
    t->playing = true;
    t->paused = false;
}

// an edge: CIA1's FLAG falls
static void tape_edge(c64 *m)
{
    mos6526_set_flag(&m->cia1, true);
    mos6526_set_flag(&m->cia1, false);
    const uint32_t p = tape_next_pulse(&m->tape);
    if (!p) {
        m->tape.remaining = 0;
        m->tape.playing = false;
        return;
    }
    m->tape.remaining = (int)p;
}

static void tape_advance(c64 *m, int cycles)
{
    c64_tape *t = &m->tape;
    if (!t->playing || t->paused || cycles <= 0 || !t->loaded) return;
    while (cycles > 0 && t->playing) {
        if (t->remaining <= 0) {
            tape_edge(m);
            if (!t->playing) break;
        }
        const int step = t->remaining < cycles ? t->remaining : cycles;
        t->remaining -= step;
        cycles -= step;
        if (t->remaining <= 0) tape_edge(m);
    }
}

// ---------------------------------------------------------------------------
// memory, PLA
// ---------------------------------------------------------------------------
static void update_pla(c64 *m)
{
    const uint8_t res = (uint8_t)(m->port_val | (uint8_t)~m->port_bits);
    m->tape_motor = (m->port_val & 0x20) == 0;
    m->write_ram = true;
    switch (res & 7) {
    case 0: case 4: m->read_ram_d = 0; m->read_ram_a = true; m->read_ram_e = true; break;
    case 1: m->read_ram_d = 1; m->read_ram_a = true; m->read_ram_e = true; break;
    case 2: m->read_ram_d = 1; m->read_ram_a = true; m->read_ram_e = false; break;
    case 3: m->read_ram_d = 1; m->read_ram_a = false; m->read_ram_e = false; break;
    case 5: m->read_ram_d = 2; m->read_ram_a = true; m->read_ram_e = true; break;
    case 6: m->read_ram_d = 2; m->read_ram_a = true; m->read_ram_e = false; break;
    default: m->read_ram_d = 2; m->read_ram_a = false; m->read_ram_e = false; break;
    }
    // direct pages for the plain RAM / ROM (the 6510's own port and I/O excepted)
    for (int p = 0x02; p < 0x100; p++) {
        const uint16_t a = (uint16_t)(p << 8);
        const uint8_t *r = m->ram + a;
        if (a >= 0xA000 && a <= 0xBFFF && !m->read_ram_a) r = m->basic + (a & 0x1FFF);
        else if (a >= 0xD000 && a <= 0xDFFF) r = m->read_ram_d == 0 ? m->ram + a : m->read_ram_d == 1 ? m->chargen + (a & 0x0FFF) : NULL;
        else if (a >= 0xE000 && !m->read_ram_e) r = m->kernal + (a & 0x1FFF);
        m->cpu.page_r[p] = r;
        m->cpu.page_w[p] = (a >= 0xD000 && a <= 0xDFFF && m->read_ram_d == 2) ? NULL : m->ram + a;
    }
    m->cpu.page_r[1] = m->cpu.page_w[1] = m->ram + 0x100;
}

static void update_irq(c64 *m)
{
    m65xx_set_irq(&m->cpu, (m->cia_irq || m->vic_irq) ? M65_ASSERT : M65_CLEAR);
}

static void update_nmi(c64 *m)
{
    m65xx_set_nmi(&m->cpu, (m->cia_nmi || m->restore) ? M65_ASSERT : M65_CLEAR);
}

static uint8_t read_byte(void *user, uint16_t addr)
{
    c64 *m = (c64 *)user;
    if (addr == 0) return m->port_bits;
    if (addr == 1) {
        uint8_t ext = 0xFF;
        if (m->tape.loaded && m->tape_play) ext &= (uint8_t)~0x10;   // sense low
        return (uint8_t)((m->port_val & m->port_bits) | (ext & (uint8_t)~m->port_bits));
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) return m->read_ram_a ? m->ram[addr] : m->basic[addr & 0x1FFF];
    if (addr >= 0xD000 && addr <= 0xDFFF) {
        switch (m->read_ram_d) {
        case 0: return m->ram[addr];
        case 1: return m->chargen[addr & 0x0FFF];
        default:
            switch ((addr >> 8) & 0x0F) {
            case 0: case 1: case 2: case 3: return mos6569_read(&m->vic, (uint8_t)(addr & 0x3F));
            case 4: case 5: case 6: case 7: return sid6581_read(&m->sid, (uint8_t)(addr & 0x1F));
            case 8: case 9: case 0xA: case 0xB: return (uint8_t)(m->color_ram[addr & 0x3FF] | 0xF0);
            case 0xC: return mos6526_read(&m->cia1, (uint8_t)(addr & 0x0F));
            case 0xD: return mos6526_read(&m->cia2, (uint8_t)(addr & 0x0F));
            default: return 0xFF;
            }
        }
    }
    if (addr >= 0xE000) return m->read_ram_e ? m->ram[addr] : m->kernal[addr & 0x1FFF];
    return m->ram[addr];
}

static void write_byte(void *user, uint16_t addr, uint8_t value)
{
    c64 *m = (c64 *)user;
    if (addr == 0) { m->port_bits = value; update_pla(m); return; }
    if (addr == 1) { m->port_val = value; update_pla(m); return; }
    if (addr >= 0xD000 && addr <= 0xDFFF && m->read_ram_d == 2) {
        switch ((addr >> 8) & 0x0F) {
        case 0: case 1: case 2: case 3: mos6569_write(&m->vic, (uint8_t)(addr & 0x3F), value); return;
        case 4: case 5: case 6: case 7: sid6581_write(&m->sid, (uint8_t)(addr & 0x1F), value); return;
        case 8: case 9: case 0xA: case 0xB: m->color_ram[addr & 0x3FF] = value & 0x0F; return;
        case 0xC: mos6526_write(&m->cia1, (uint8_t)(addr & 0x0F), value); return;
        case 0xD: mos6526_write(&m->cia2, (uint8_t)(addr & 0x0F), value); return;
        default: return;
        }
    }
    if (m->write_ram) m->ram[addr] = value;
}

static uint8_t vic_mem(void *user, uint16_t a)
{
    c64 *m = (c64 *)user;
    if ((a & 0x7000) == 0x1000) return m->chargen[a & 0x0FFF];     // the character ROM in banks 0 and 2
    return m->ram[a];
}

static void vic_irq(void *user, bool on) { c64 *m = (c64 *)user; m->vic_irq = on; update_irq(m); }
static void cia1_irq(void *user, bool on) { c64 *m = (c64 *)user; m->cia_irq = on; update_irq(m); }
static void cia2_irq(void *user, bool on) { c64 *m = (c64 *)user; m->cia_nmi = on; update_nmi(m); }

static uint8_t cia1_pb_in(void *user)
{
    c64 *m = (c64 *)user;
    uint8_t ret = 0xFF;
    const uint8_t pa = mos6526_pa(&m->cia1);
    for (int i = 0; i < 8; i++)
        if (!(pa & (1 << i))) ret &= m->keyboard[i];
    return ret;
}

// The boards start the 1541 with the first disk (a third of the C64's time
// goes to it): without one, "device not present"
static inline bool drive_active(const c64 *m) { return m->drive_on && c1541_present(&m->drive); }

static bool drive_insert(c64 *m, zx_pbuf *pb)
{
    const bool ok = c1541_insert(&m->drive, pb, &m->error);
    if (ok && !m->drive_on) {
        m->drive_on = true;
        c1541_reset(&m->drive);
    }
    return ok;
}

// CIA2 port A: VIC bank (PA0-1), IEC ATN / CLK / DATA out (PA3-5), CLK / DATA in (PA6-7)
static uint8_t cia2_pa_in(void *user)
{
    c64 *m = (c64 *)user;
    uint8_t v = 0x3F;
    if (drive_active(m)) {
        if (c1541_bus_clk(&m->drive)) v |= 0x40;
        if (c1541_bus_data(&m->drive)) v |= 0x80;
    } else {
        v |= 0xC0;
    }
    return v;
}

static void cia2_pa_out(void *user, uint8_t v)
{
    c64 *m = (c64 *)user;
    mos6569_changed_va(&m->vic, (uint16_t)(~v & 3));
    if (drive_active(m)) {
        c1541_set_host_atn(&m->drive, (v & 0x08) == 0);
        c1541_set_host_clk(&m->drive, (v & 0x10) == 0);
        c1541_set_host_data(&m->drive, (v & 0x20) == 0);
    }
}

static void on_cycles(void *user, int cycles)
{
    c64 *m = (c64 *)user;
    if (cycles <= 0) return;
    if (m->tape_motor && m->tape.loaded) {
        if (!m->tape_play) {
            m->tape_play = true;
            tape_play(&m->tape, true);
        } else if (!m->tape.playing) {
            tape_play(&m->tape, false);
        }
        tape_advance(m, cycles);
    }
    mos6526_tick(&m->cia1, cycles);
    mos6526_tick(&m->cia2, cycles);
    if (drive_active(m)) c1541_run(&m->drive, cycles);
    m->audio_acc += (int64_t)cycles * m->audio_rate;
    while (m->audio_acc >= CPU_CLOCK) {
        m->audio_acc -= CPU_CLOCK;
        int32_t s = sid6581_update(&m->sid);
        if (m->naudio < C64_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)s;
    }
}

// ---------------------------------------------------------------------------
// .prg into the RAM (no drive)
// ---------------------------------------------------------------------------
static void inject_prg(c64 *m, const uint8_t *data, size_t size)
{
    const uint16_t addr = (uint16_t)(data[0] | (data[1] << 8));
    const size_t n = size - 2;
    for (size_t i = 0; i < n; i++) m->ram[(uint16_t)(addr + i)] = data[i + 2];
    const uint16_t end = (uint16_t)(addr + n);
    if (addr != 0x0801) return;
    for (int p = 0x2D; p <= 0x31; p += 2) { m->ram[p] = (uint8_t)end; m->ram[p + 1] = (uint8_t)(end >> 8); }
    m->ram[0xAE] = (uint8_t)end; m->ram[0xAF] = (uint8_t)(end >> 8);
    static const uint8_t kRun[] = { 'R', 'U', 'N', 0x0D };      // typed into the keyboard buffer
    memcpy(m->ram + 0x0277, kRun, sizeof kRun);
    m->ram[0xC6] = sizeof kRun;
}

void c64_kbuf(c64 *m, const char *text)
{
    int n = m->ram[0xC6];
    for (; *text && n < 10; text++) m->ram[0x0277 + n++] = (uint8_t)(*text == '\n' ? 0x0D : *text);
    m->ram[0xC6] = (uint8_t)n;
}

static void update_pending_prg(c64 *m)
{
    if (!m->pending_prg) return;
    if (++m->boot_frames < PRG_INJECT_FRAMES) return;
    if (m->ram[0x2B] != 0x01 || m->ram[0x2C] != 0x08) return;
    inject_prg(m, m->pending_prg, m->pending_size);
    free(m->pending_prg);
    m->pending_prg = NULL;
}

static bool queue_prg(c64 *m, uint8_t *data, size_t size)      // takes `data`
{
    if (size < 3) {
        free(data);
        m->error = "PRG too small";
        return false;
    }
    free(m->pending_prg);
    m->pending_prg = data;
    m->pending_size = size;
    m->boot_frames = 0;
    return true;
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
c64 *c64_create(int audio_rate)
{
    c64 *m = (c64 *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->ram = (uint8_t *)calloc(1, 0x10000);
    if (!m->ram) { free(m); return NULL; }
    static const uint8_t kNoRoms[0x2000];
    m->kernal = m->basic = m->chargen = kNoRoms;
    memcpy(m->pal565, vic_palette565, sizeof m->pal565);
#ifndef C64_SMALL
    m->drive_on = true;
#endif
    m->audio_rate = audio_rate > 0 ? audio_rate : C64_AUDIO_RATE;
    m->crop_x = 48 - (C64_PIC_W - 320) / 2;
    if (m->crop_x < 0) m->crop_x = 0;
    if (m->crop_x > VIC_W - C64_PIC_W) m->crop_x = VIC_W - C64_PIC_W;
    m->crop_y = 35 - (C64_PIC_H - 200) / 2;
    if (m->crop_y < 0) m->crop_y = 0;
    if (m->crop_y > VIC_H - C64_PIC_H) m->crop_y = VIC_H - C64_PIC_H;
    m->cpu.read = read_byte;
    m->cpu.write = write_byte;
    m->cpu.cycles = on_cycles;
    m->cpu.user = m;
    m->vic.mem = vic_mem;
    m->vic.irq = vic_irq;
    m->vic.user = m;
    m->vic.color_ram = m->color_ram;
    sid6581_init(&m->sid, CPU_CLOCK, (uint32_t)m->audio_rate);
    m->cia1.irq = cia1_irq;
    m->cia1.pb_in = cia1_pb_in;
    m->cia1.user = m;
    m->cia2.irq = cia2_irq;
    m->cia2.pa_in = cia2_pa_in;
    m->cia2.pa_out = cia2_pa_out;
    m->cia2.user = m;
    c1541_init(&m->drive, NULL, 0);
    zx_pb_init(&m->tape.pb);
    memset(m->keys, 0xFF, sizeof m->keys);
    return m;
}

void c64_destroy(c64 *m)
{
    if (!m) return;
    c1541_free(&m->drive);
    zx_pb_free(&m->tape.pb);
    free(m->pending_prg);
    free(m->ram);
    free(m);
}

void c64_set_roms(c64 *m, const uint8_t *kernal, const uint8_t *basic, const uint8_t *chargen,
                  const uint8_t *dos1541)
{
    if (kernal) m->kernal = kernal;
    if (basic) m->basic = basic;
    if (chargen) m->chargen = chargen;
    c1541_free(&m->drive);
    c1541_init(&m->drive, dos1541, 0x4000);
    c64_reset(m);
}

void c64_reset(c64 *m)
{
    m->port_bits = 0x2F;
    m->port_val = 0x37;
    update_pla(m);
    mos6569_reset(&m->vic);
    sid6581_reset(&m->sid);
    mos6526_reset(&m->cia1);
    mos6526_reset(&m->cia2);
    m->cia_irq = m->vic_irq = m->cia_nmi = m->restore = false;
    m65xx_set_irq(&m->cpu, M65_CLEAR);
    m65xx_set_nmi(&m->cpu, M65_CLEAR);
    m->cpu_cycle_debt = 0;
    m->boot_frames = 0;
    m65xx_reset(&m->cpu);
    if (c1541_present(&m->drive)) c1541_reset(&m->drive);
    m->tape_motor = false;
    memset(m->keyboard, 0xFF, sizeof m->keyboard);
    memset(m->color_ram, 0, sizeof m->color_ram);
    m->naudio = 0;
    m->audio_acc = 0;
    memset(m->screen, 0, sizeof m->screen);
}

int c64_run_frame(c64 *m, const int16_t **audio)
{
    memcpy(m->keyboard, m->keys, sizeof m->keys);
    m->cia1.joystick1 = (uint8_t)~m->joy1;
    m->cia1.joystick2 = (uint8_t)~m->joy2;
    m->naudio = 0;
    update_pending_prg(m);
    for (int line = 0; line < SCANLINES; line++) {
        int cpu_cycles = CYCLES_PER_LINE;
        const int vis_y = line - 16;
        const bool shown = vis_y >= 0 && vis_y < VIC_H;
        const int stolen = mos6569_update_line(&m->vic, line, shown ? m->line_pixels : NULL);
        const int y = vis_y - m->crop_y;
        if (shown && y >= 0 && y < C64_PIC_H) {
            uint8_t *dst = m->screen + (size_t)y * ((C64_PIC_W + 1) / 2);
            const uint8_t *src = m->line_pixels + m->crop_x;
            for (int x = 0; x + 1 < C64_PIC_W; x += 2) *dst++ = (uint8_t)(src[x] | (src[x + 1] << 4));
        }
        cpu_cycles -= stolen;
        if (cpu_cycles < 0) cpu_cycles = 0;
        if (stolen > 0) on_cycles(m, stolen);
        m->cpu_cycle_debt += cpu_cycles;
        if (m->cpu_cycle_debt > 0) m->cpu_cycle_debt -= m65xx_run(&m->cpu, m->cpu_cycle_debt);
    }
    if (audio) *audio = m->audio;
    return m->naudio;
}

void c64_keys_clear(c64 *m) { memset(m->keys, 0xFF, sizeof m->keys); }

void c64_key(c64 *m, int code, bool down)
{
    if (code < 0 || code >= 64) return;
    if (down) m->keys[code >> 3] &= (uint8_t)~(1u << (code & 7));
    else m->keys[code >> 3] |= (uint8_t)(1u << (code & 7));
}

void c64_joy(c64 *m, int port, uint8_t bits)
{
    if (port == 1) m->joy1 = bits & 0x1F;
    else m->joy2 = bits & 0x1F;
}

void c64_restore(c64 *m, bool down)
{
    if (down == m->restore) return;
    m->restore = down;
    update_nmi(m);
}

void c64_render_row(const c64 *m, int y, uint32_t *out)
{
    const uint8_t *src = m->screen + (size_t)y * ((C64_PIC_W + 1) / 2);
    for (int x = 0; x < C64_PIC_W; x++) out[x] = vic_palette[(src[x >> 1] >> ((x & 1) * 4)) & 15];
}

void c64_render_row565(const c64 *m, int y, uint16_t *out)
{
    const uint8_t *src = m->screen + (size_t)y * ((C64_PIC_W + 1) / 2);
    for (int x = 0; x < C64_PIC_W; x += 2) {
        out[x] = m->pal565[src[x >> 1] & 15];
        out[x + 1] = m->pal565[src[x >> 1] >> 4];
    }
}

void c64_set_rgb565(c64 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b))
{
    for (int i = 0; i < 16; i++) {
        const uint32_t c = vic_palette[i];
        m->pal565[i] = fn ? fn((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c) : vic_palette565[i];
    }
}

// ---------------------------------------------------------------------------
// media
// ---------------------------------------------------------------------------
static bool ends_with_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)s[n - k + i]) != ext[i]) return false;
    return true;
}

static void trim_name(char *dst, const uint8_t *p, size_t n)
{
    size_t i = 0;
    for (; i < n; i++) dst[i] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : ' ';
    while (i > 0 && dst[i - 1] == ' ') i--;
    dst[i] = 0;
}

// .t64: a disk for the drive, or the first file into the RAM
static bool load_t64(c64 *m, const uint8_t *d, size_t size)
{
    char sig[33];
    trim_name(sig, d, 32);
    if (size < 0x40 || strncmp(sig, "C64", 3) != 0 || !strstr(sig, "tape")) {
        m->error = "not a T64 image";
        return false;
    }
    char tape_name[25];
    trim_name(tape_name, d + 0x28, 24);
    const uint16_t max = (uint16_t)(d[0x22] | (d[0x23] << 8));
    uint16_t used = (uint16_t)(d[0x24] | (d[0x25] << 8));
    if (used == 0 || used > max) used = max;
    d64_file files[64];
    char names[64][17];
    uint8_t *prgs[64];
    int n = 0;
    for (uint16_t i = 0; i < used && n < 64; i++) {
        const size_t base = 0x40 + (size_t)i * 32;
        if (base + 32 > size) break;
        const uint8_t *e = d + base;
        if (e[0] == 0) continue;
        const uint16_t start = (uint16_t)(e[2] | (e[3] << 8)), end = (uint16_t)(e[4] | (e[5] << 8));
        const uint32_t off = (uint32_t)(e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24));
        if (off >= size) continue;
        size_t len = end > start ? (size_t)(end - start) : 0;
        if (len == 0 || len > size - off) len = size - off;
        uint8_t *prg = (uint8_t *)malloc(len + 2);
        if (!prg) break;
        prg[0] = (uint8_t)start;
        prg[1] = (uint8_t)(start >> 8);
        memcpy(prg + 2, d + off, len);
        trim_name(names[n], e + 0x10, 16);
        prgs[n] = prg;
        files[n] = (d64_file){ names[n], prg, len + 2 };
        n++;
    }
    if (!n) {
        m->error = "T64 has no files";
        return false;
    }
    bool ok = false;
    if (c1541_present(&m->drive)) {
        uint8_t *img = d64_build(files, n, tape_name);
        if (img) {
            zx_pbuf pb;
            zx_pb_take_flat(&pb, img, 174848);
            ok = drive_insert(m, &pb);
        }
    }
    if (!ok) {
        ok = queue_prg(m, prgs[0], files[0].size);
        prgs[0] = NULL;
    }
    for (int i = 0; i < n; i++) free(prgs[i]);
    return ok;
}

// .d64 without the drive: its first PRG into the RAM
static bool d64_first_prg(c64 *m, const zx_pbuf *pb)
{
    if (pb->size < 174848) { m->error = "D64 too small"; return false; }
    int t = 18, s = 1;
    for (int guard = 0; guard < 50 && t; guard++) {
        const long off = d64_offset(t, s);
        if (off < 0) break;
        uint8_t sec[256];
        zx_pb_copy(pb, (uint32_t)off, sec, 256);
        for (int i = 0; i < 8; i++) {
            const uint8_t *e = sec + i * 32;
            if ((e[2] & 0x0F) != 0x02) continue;
            uint8_t *prg = NULL;
            size_t n = 0;
            int ft = e[3], fs = e[4];
            for (int g = 0; g < 1024 && ft; g++) {
                const long fo = d64_offset(ft, fs);
                if (fo < 0) break;
                uint8_t b[256];
                zx_pb_copy(pb, (uint32_t)fo, b, 256);
                const int k = b[0] == 0 ? (b[1] > 1 ? b[1] - 1 : 0) : 254;
                uint8_t *np = (uint8_t *)realloc(prg, n + (size_t)k + 1);
                if (!np) { free(prg); m->error = "out of memory"; return false; }
                prg = np;
                memcpy(prg + n, b + 2, (size_t)k);
                n += (size_t)k;
                ft = b[0];
                fs = b[1];
            }
            return queue_prg(m, prg, n);
        }
        t = sec[0];
        s = sec[1];
    }
    m->error = "no PRG files on disk";
    return false;
}

bool c64_load_pbuf(c64 *m, zx_pbuf *pb, const char *name)
{
    m->error = NULL;
    bool ok = false;
    if (!name) name = "";
    if (ends_with_ci(name, ".g64") || ends_with_ci(name, ".g41") || zx_pb_eq(pb, 0, "GCR-1541", 8)) {
        if (!c1541_present(&m->drive)) m->error = "G64 needs the 1541's DOS ROM";
        else ok = drive_insert(m, pb);
    } else if (ends_with_ci(name, ".d64")) {
        if (c1541_present(&m->drive)) ok = drive_insert(m, pb);
        else ok = d64_first_prg(m, pb);
    } else if (ends_with_ci(name, ".tap") || zx_pb_eq(pb, 0, "C64-TAPE-RAW", 12)) {
        if (pb->size < 20 || !zx_pb_eq(pb, 0, "C64-TAPE-RAW", 12)) m->error = "not a C64-TAPE-RAW image";
        else if (zx_pb_byte(pb, 0x0C) > 2) m->error = "unsupported TAP version";
        else {
            zx_pb_free(&m->tape.pb);
            m->tape.pb = *pb;
            zx_pb_init(pb);
            c64_tape *t = &m->tape;
            t->version = zx_pb_byte(&t->pb, 0x0C);
            const uint32_t declared = zx_pb_rd32(&t->pb, 0x10);
            t->end = declared > 0 && 20 + declared <= t->pb.size ? 20 + declared : t->pb.size;
            t->pos = 20;
            t->remaining = 0;
            t->loaded = true;
            m->tape_play = true;
            tape_play(t, true);
            ok = true;
        }
    } else if (ends_with_ci(name, ".t64") || zx_pb_eq(pb, 0, "C64", 3)) {
        uint8_t *flat = zx_pb_flatten(pb, 0, pb->size);
        if (!flat) m->error = "out of memory";
        else {
            ok = load_t64(m, flat, pb->size);
            free(flat);
        }
    } else {                                    // .prg
        uint8_t *flat = zx_pb_flatten(pb, 0, pb->size);
        if (!flat) m->error = "out of memory";
        else ok = queue_prg(m, flat, pb->size);
    }
    zx_pb_free(pb);
    if (!ok && !m->error) m->error = "cannot load the file";
    return ok;
}

bool c64_load_file(c64 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { m->error = "cannot open the file"; return false; }
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t buf[4096];
    size_t k;
    bool ok = true;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0)
        if (!(ok = zx_pb_append(&pb, buf, k))) break;
    fclose(f);
    if (!ok || !zx_pb_finish(&pb) || pb.size == 0) {
        zx_pb_free(&pb);
        m->error = ok ? "empty file" : "out of memory";
        return false;
    }
    return c64_load_pbuf(m, &pb, path);
}

const char *c64_last_error(const c64 *m) { return m->error ? m->error : ""; }
bool c64_has_drive(const c64 *m) { return c1541_present(&m->drive); }
bool c64_drive_led(const c64 *m) { return m->drive.led_on; }

void c64_tape_toggle(c64 *m)
{
    if (!m->tape.loaded) return;
    if (m->tape.playing && !m->tape.paused) m->tape.paused = true;
    else {
        m->tape_play = true;
        tape_play(&m->tape, false);
    }
}

bool c64_tape_loaded(const c64 *m) { return m->tape.loaded; }
bool c64_tape_playing(const c64 *m) { return m->tape.loaded && m->tape.playing && !m->tape.paused; }
