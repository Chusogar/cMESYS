// msx_core.c - MSX1 / MSX2, see msx_core.h
#include "msx_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay8910.h"
#include "msx_cart.h"
#include "msx_disk.h"
#include "tms9918.h"
#include "v9938.h"
#include "z80dsp/z80dsp.h"

#define CYCLES_PER_LINE 228
#define LINES_PAL 313
#define LINES_NTSC 262
#define ROWS 212                        // the picture's lines (192-line modes in the middle)
#define MAX_SEGS 16

struct msx {
    zd_z80 cpu;
    int model;
    tms9918 tms;
    v9938 v99;
    ay8910 ay;
    msx_cart cart;
    msx_disk disk;
    msx_fdc fdc;

    const uint8_t *bios, *sub, *diskrom;
    uint8_t *seg[MAX_SEGS];             // RAM: 16 KB pieces (the MSX1: 4, at slot 3)
    int nsegs;
    uint8_t mapper[4];                  // MSX2: the segment of each page (FC-FF)
    uint8_t subslot3;                   // MSX2: slot 3's FFFF register

    // where each page reads and writes now (NULL: through the handlers)
    const uint8_t *rd[4];
    uint8_t *wr[4];
    bool page_cart[4], page_disk[4];

    // 8255
    uint8_t port_a, port_c;
    uint8_t keys[16];
    // AY ports
    uint8_t joy[2], joy_select;

    // the disk ROM's FDC latch
    uint8_t fdc_control;

    // RTC (MSX2): 4-bit registers, three banks of 13
    uint8_t rtc_addr, rtc_mode, rtc_bank[3][13];

    // the cassette: CAS bytes, played through the BIOS
    zx_pbuf cas;
    uint32_t cas_pos;
    bool cas_loaded;

    // typing into the keyboard buffer
    char type_buf[256];
    int type_pos;
    bool type_pending;

    bool japan;                         // MSX2: the BIOS says 60 Hz Japanese
    bool irq_line;
    int lines;                          // per frame
    int audio_rate;
    int64_t audio_acc;
    int16_t audio[MSX_AUDIO_MAX];
    int naudio;
    bool turbo;
    const char *error;

    // the picture
    uint8_t row_done[ROWS];
#ifdef MSX_SMALL
    uint8_t (*pen)[256];                // the picture's pens (none with row_out)
    uint8_t row_g7[ROWS];               // the row's pens are GRB332
    uint8_t border_pen;
    bool border_g7;
    uint16_t pal565[16], g7_565[256];
    uint16_t pal_seen[16];              // (the V9938 palette pal565 was made from)
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
    uint16_t *(*row_out)(int y);        // rows written straight to the board's picture
#else
    uint32_t frame[ROWS][512];
#endif
};

static const uint8_t kCasHeader[8] = { 0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74 };

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
static inline int primary(const msx *m, int page) { return (m->port_a >> (page * 2)) & 3; }
static inline int secondary(const msx *m, int page) { return (m->subslot3 >> (page * 2)) & 3; }

static void map_pages(msx *m)
{
    for (int p = 0; p < 4; p++) {
        const int prim = primary(m, p);
        const uint8_t *rd = NULL;
        uint8_t *wr = NULL;
        bool cart = false, disk = false;
        if (prim == 0) {
            if (p <= 1 && !(p == 0 && m->japan)) rd = m->bios + p * 0x4000;
        } else if (prim == 1) {
            cart = true;
        } else if (m->model == MSX_1) {
            // the disk ROM (slot 2) only with a disk in: tape games want the memory it takes
            if (prim == 2) disk = p == 1 && m->diskrom && msxd_present(&m->disk);
            else rd = wr = m->seg[p];               // slot 3: 64 KB of RAM
        } else {    // MSX2: slot 3 expanded: 3-0 sub ROM, 3-1 the mapper, 3-2 the disk ROM
            const int sub = secondary(m, p);
            if (prim == 3) {
                if (sub == 0 && p == 0) rd = m->sub;
                else if (sub == 1) rd = wr = m->seg[m->mapper[p] % m->nsegs];
                else if (sub == 2) disk = p == 1 && m->diskrom;
            }
        }
        m->rd[p] = rd;
        m->wr[p] = wr;
        m->page_cart[p] = cart;
        m->page_disk[p] = disk;
    }
}

static uint8_t fdc_read(msx *m, uint16_t a)
{
    a &= 0x3fff;
    if (a >= 0x3ff8 && a <= 0x3ffb) return msxf_read(&m->fdc, a - 0x3ff8);
    if (a == 0x3ffc) return (uint8_t)(0xfe | (m->fdc_control & 1));
    if (a == 0x3ffd) return m->fdc_control;
    if (a == 0x3fff || a == 0x3fbc) {
        uint8_t v = 0x3f;
        if (!m->fdc.intrq) v |= 0x40;
        if (!m->fdc.drq) v |= 0x80;
        return v;
    }
    if (a >= 0x3fb8 && a <= 0x3fbb) return msxf_read(&m->fdc, a - 0x3fb8);
    return m->diskrom[a];
}

static void fdc_write(msx *m, uint16_t a, uint8_t v)
{
    a &= 0x3fff;
    if (a >= 0x3ff8 && a <= 0x3ffb) msxf_write(&m->fdc, a - 0x3ff8, v);
    else if (a == 0x3ffc) {
        m->fdc.side = v & 1;
        m->fdc_control = (uint8_t)((m->fdc_control & 0xfe) | (v & 1));
    } else if (a == 0x3ffd) {
        m->fdc_control = v;
        m->fdc.drive = v & 3;
        m->fdc.motor = v & 0x80;
    } else if (a >= 0x3fb8 && a <= 0x3fbb) msxf_write(&m->fdc, a - 0x3fb8, v);
    else if (a == 0x3fbc) {
        m->fdc_control = v;
        m->fdc.drive = v & 3;
        m->fdc.side = (v >> 2) & 1;
        m->fdc.motor = v & 8;
    }
}

static uint8_t read_byte(void *user, uint16_t a)
{
    msx *m = (msx *)user;
    const int p = a >> 14;
    if (a == 0xffff && m->model == MSX_2 && primary(m, 3) == 3) return (uint8_t)~m->subslot3;
    if (m->rd[p]) return m->rd[p][a & 0x3fff];
    if (m->page_cart[p]) return msxc_read(&m->cart, a);
    if (m->page_disk[p]) return fdc_read(m, a);
    if (p == 0 && primary(m, 0) == 0) {
        // a Japanese MSX2 cartridge: the BIOS says 60 Hz, Japanese charset and keyboard
        // (Metal Gear and others reset on a European one, as on the real thing)
        if (a == 0x2b) return 0x00;
        if (a == 0x2c) return m->bios[0x2c] & 0x30;
        return m->bios[a];
    }
    return 0xff;
}

static void write_byte(void *user, uint16_t a, uint8_t v)
{
    msx *m = (msx *)user;
    const int p = a >> 14;
    if (a == 0xffff && m->model == MSX_2 && primary(m, 3) == 3) {
        m->subslot3 = v;
        map_pages(m);
        return;
    }
    if (m->wr[p]) m->wr[p][a & 0x3fff] = v;
    else if (m->page_cart[p]) msxc_write(&m->cart, a, v);
    else if (m->page_disk[p]) {
        const uint16_t o = a & 0x3fff;
        if ((o >= 0x3ff8) || (o >= 0x3fb8 && o <= 0x3fbc)) fdc_write(m, a, v);
    }
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------
static uint8_t rtc_read(const msx *m)
{
    const int a = m->rtc_addr & 15;
    if (a == 13) return (uint8_t)(0xf0 | m->rtc_mode);
    if (a >= 13) return 0xff;
    const int bank = m->rtc_mode & 3;
    return (uint8_t)(0xf0 | (bank < 3 ? m->rtc_bank[bank][a] : 0));
}

static void rtc_write(msx *m, uint8_t v)
{
    const int a = m->rtc_addr & 15;
    if (a == 13) m->rtc_mode = v & 0x0f;
    else if (a < 13 && (m->rtc_mode & 3) < 3) m->rtc_bank[m->rtc_mode & 3][a] = v & 0x0f;
}

static uint8_t in_port(void *user, uint16_t port)
{
    msx *m = (msx *)user;
    switch (port & 0xff) {
    case 0x98: return m->model == MSX_2 ? v99_vram_read(&m->v99) : tms_vram_read(&m->tms);
    case 0x99: return m->model == MSX_2 ? v99_status_read(&m->v99) : tms_status_read(&m->tms);
    case 0xa2:
        if (m->ay.latch == 14) return m->joy[m->joy_select];
        return ay8910_data_r(&m->ay);
    case 0xa8: return m->port_a;
    case 0xa9: return (m->port_c & 0x0f) < 11 ? m->keys[m->port_c & 0x0f] : 0xff;
    case 0xaa: return m->port_c;
    case 0xb5: return m->model == MSX_2 ? rtc_read(m) : 0xff;
    case 0xfc: case 0xfd: case 0xfe: case 0xff:
        if (m->model != MSX_2) return 0xff;
        return (uint8_t)(m->mapper[port & 3] | (uint8_t)~(m->nsegs - 1));
    default: return 0xff;
    }
}

static void out_port(void *user, uint16_t port, uint8_t v)
{
    msx *m = (msx *)user;
    switch (port & 0xff) {
    case 0x98:
        if (m->model == MSX_2) v99_vram_write(&m->v99, v);
        else tms_vram_write(&m->tms, v);
        break;
    case 0x99:
        if (m->model == MSX_2) v99_register_write(&m->v99, v);
        else tms_register_write(&m->tms, v);
        break;
    case 0x9a: if (m->model == MSX_2) v99_palette_write(&m->v99, v); break;
    case 0x9b: if (m->model == MSX_2) v99_indirect_write(&m->v99, v); break;
    case 0xa0: ay8910_address_w(&m->ay, v); break;
    case 0xa1:
        if (m->ay.latch == 15) m->joy_select = (v >> 6) & 1;
        ay8910_data_w(&m->ay, v);
        break;
    case 0xa8: m->port_a = v; map_pages(m); break;
    case 0xaa: m->port_c = v; break;
    case 0xab:
        if (!(v & 0x80)) {              // port C bit set / reset
            const uint8_t bit = (uint8_t)(1 << ((v >> 1) & 7));
            m->port_c = (v & 1) ? (uint8_t)(m->port_c | bit) : (uint8_t)(m->port_c & ~bit);
        }
        break;
    case 0xb4: if (m->model == MSX_2) m->rtc_addr = v & 0x0f; break;
    case 0xb5: if (m->model == MSX_2) rtc_write(m, v); break;
    case 0xfc: case 0xfd: case 0xfe: case 0xff:
        if (m->model == MSX_2) {
            m->mapper[port & 3] = (uint8_t)(v & (m->nsegs - 1));
            map_pages(m);
        }
        break;
    default: break;
    }
}

static void vdp_irq(void *user, bool on)
{
    msx *m = (msx *)user;
    if (on != m->irq_line) zd_set_irq(&m->cpu, on ? ZD_ASSERT : ZD_CLEAR, 0xff);
    m->irq_line = on;
}

// ---------------------------------------------------------------------------
// the cassette through the BIOS, and typing
// ---------------------------------------------------------------------------
static void ret(msx *m)
{
    const uint16_t lo = read_byte(m, m->cpu.sp), hi = read_byte(m, (uint16_t)(m->cpu.sp + 1));
    m->cpu.sp = (uint16_t)(m->cpu.sp + 2);
    m->cpu.pc = (uint16_t)(lo | (hi << 8));
}

static bool cas_header_at(const msx *m, uint32_t p)
{
    for (int i = 0; i < 8; i++)
        if (zx_pb_byte(&m->cas, p + (uint32_t)i) != kCasHeader[i]) return false;
    return p + 8 <= m->cas.size;
}

static void kbuf_put(msx *m)
{
    // the BIOS's keyboard buffer: KEYBUF $FBF0 (40 bytes), PUTPNT $F3F8,
    // GETPNT $F3FA; a piece when it is empty
    uint16_t put = (uint16_t)(read_byte(m, 0xf3f8) | (read_byte(m, 0xf3f9) << 8));
    const uint16_t get = (uint16_t)(read_byte(m, 0xf3fa) | (read_byte(m, 0xf3fb) << 8));
    if (put != get) return;
    for (int n = 0; n < 32 && m->type_buf[m->type_pos]; n++) {
        write_byte(m, put, (uint8_t)m->type_buf[m->type_pos++]);
        put = (uint16_t)(put + 1);
        if (put >= 0xfbf0 + 40) put = 0xfbf0;
    }
    write_byte(m, 0xf3f8, (uint8_t)put);
    write_byte(m, 0xf3f9, (uint8_t)(put >> 8));
    if (!m->type_buf[m->type_pos]) m->type_pending = false;
}

static void bios_hook(void *user, uint16_t pc)
{
    msx *m = (msx *)user;
    // CHGET (or its hook H.CHGE): BASIC, or someone, waits for a key
    if ((pc == 0xfdc2 || pc == 0x9f) && m->type_pending) {
        kbuf_put(m);
        return;
    }
    if (pc > 0xf3 || primary(m, 0) != 0) return;
    if (!m->cas_loaded) return;
    zd_z80 *z = &m->cpu;
    switch (pc) {
    case 0xe1: {        // TAPION: the next block's header
        uint32_t p = (m->cas_pos + 7) & ~7u;
        while (p + 8 <= m->cas.size && !cas_header_at(m, p)) p += 8;
        if (p + 8 <= m->cas.size) {
            m->cas_pos = p + 8;
            z->f &= (uint8_t)~ZD_CF;
        } else {
            z->f |= ZD_CF;
        }
        ret(m);
        break;
    }
    case 0xe4:          // TAPIN: a byte
        if (m->cas_pos < m->cas.size) {
            z->a = zx_pb_byte(&m->cas, m->cas_pos++);
            z->f &= (uint8_t)~ZD_CF;
        } else {
            z->f |= ZD_CF;
        }
        ret(m);
        break;
    case 0xe7:          // TAPIOF
        z->f &= (uint8_t)~ZD_CF;
        z->iff1 = z->iff2 = true;
        ret(m);
        break;
    case 0xea: case 0xed:   // TAPOON / TAPOUT: no saving to tape here
        z->f |= ZD_CF;
        ret(m);
        break;
    case 0xf0:          // TAPOOF
        z->f &= (uint8_t)~ZD_CF;
        z->iff1 = z->iff2 = true;
        ret(m);
        break;
    default: break;
    }
}

void msx_type(msx *m, const char *text)
{
    snprintf(m->type_buf, sizeof m->type_buf, "%s", text);
    m->type_pos = 0;
    m->type_pending = m->type_buf[0] != 0;
}

// .tsx / .tzx: the MSX blocks (0x4B) into CAS bytes
static bool tzx_to_cas(msx *m, const zx_pbuf *t)
{
    zx_pbuf out;
    zx_pb_init(&out);
    uint32_t pos = 10, n = 0;
    static const uint8_t zero[8] = { 0 };
    bool any = false;
    while (pos < t->size) {
        const uint8_t id = zx_pb_byte(t, pos++);
        uint32_t len = 0;
        switch (id) {
        case 0x10: len = 4 + zx_pb_rd16(t, pos + 2); break;
        case 0x11: len = 18 + zx_pb_rd24(t, pos + 15); break;
        case 0x12: len = 4; break;
        case 0x13: len = 1 + 2u * zx_pb_byte(t, pos); break;
        case 0x14: len = 10 + zx_pb_rd24(t, pos + 7); break;
        case 0x15: len = 8 + zx_pb_rd24(t, pos + 5); break;
        case 0x18: case 0x19: len = 4 + zx_pb_rd32(t, pos); break;
        case 0x20: case 0x23: case 0x24: len = 2; break;
        case 0x21: case 0x30: len = 1 + zx_pb_byte(t, pos); break;
        case 0x22: case 0x25: case 0x27: len = 0; break;
        case 0x26: len = 2 + 2u * zx_pb_rd16(t, pos); break;
        case 0x28: case 0x32: len = 2 + zx_pb_rd16(t, pos); break;
        case 0x2a: len = 4; break;
        case 0x2b: len = 5; break;
        case 0x31: len = 2 + zx_pb_byte(t, pos + 1); break;
        case 0x33: len = 1 + 3u * zx_pb_byte(t, pos); break;
        case 0x35: len = 20 + zx_pb_rd32(t, pos + 16); break;
        case 0x4b: {
            const uint32_t blen = zx_pb_rd32(t, pos);
            len = 4 + blen;
            if (blen > 12) {                // header (aligned to 8) + the block's bytes
                while (n & 7) { zx_pb_append(&out, zero, 1); n++; }
                zx_pb_append(&out, kCasHeader, 8);
                n += 8;
                for (uint32_t i = 16; i < 4 + blen; i++) {
                    const uint8_t b = zx_pb_byte(t, pos + i);
                    zx_pb_append(&out, &b, 1);
                    n++;
                }
                any = true;
            }
            break;
        }
        case 0x5a: len = 9; break;
        default: pos = t->size; break;
        }
        pos += len;
    }
    if (!any || !zx_pb_finish(&out)) {
        zx_pb_free(&out);
        m->error = any ? "out of memory" : "no MSX blocks in the tape";
        return false;
    }
    zx_pb_free(&m->cas);
    m->cas = out;
    return true;
}

static void cas_autorun(msx *m)
{
    // the first block's 10-byte file type: D3 tokenized BASIC, EA ASCII, D0 binary
    uint32_t p = 0;
    while (p + 8 <= m->cas.size && !cas_header_at(m, p)) p += 8;
    const uint8_t type = zx_pb_byte(&m->cas, p + 8);
    if (type == 0xd3) msx_type(m, "CLOAD\rRUN\r");
    else if (type == 0xd0) msx_type(m, "BLOAD\"CAS:\",R\r");
    else msx_type(m, "RUN\"CAS:\"\r");
}

// ---------------------------------------------------------------------------
// the machine
// ---------------------------------------------------------------------------
static void on_cycles(void *user, int t)
{
    msx *m = (msx *)user;
    if (m->turbo) return;
    m->audio_acc += (int64_t)t * m->audio_rate;
    while (m->audio_acc >= MSX_CLOCK) {
        m->audio_acc -= MSX_CLOCK;
        int32_t s = ay8910_render(&m->ay) + msxc_sample(&m->cart, MSX_CLOCK, m->audio_rate) * 2;
        if (m->port_c & 0x80) s += 3000;            // the key click
        if (m->naudio < MSX_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

msx *msx_create(int model, int ram_kb, int vram_kb, int audio_rate)
{
    msx *m = (msx *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->model = model == MSX_2 ? MSX_2 : MSX_1;
    m->audio_rate = audio_rate > 0 ? audio_rate : MSX_AUDIO_RATE;
    m->nsegs = m->model == MSX_1 ? 4 : ram_kb >= 256 ? 16 : ram_kb >= 128 ? 8 : 4;
    for (int i = 0; i < m->nsegs; i++)
        if (!(m->seg[i] = (uint8_t *)calloc(1, 0x4000))) {
            msx_destroy(m);
            return NULL;
        }
    if (m->model == MSX_2) {
        if (!v99_init(&m->v99, vram_kb)) {
            msx_destroy(m);
            return NULL;
        }
        m->v99.irq = vdp_irq;
        m->v99.user = m;
    } else {
        m->tms.irq = vdp_irq;
        m->tms.user = m;
    }
    msxc_init(&m->cart);
    msxd_init(&m->disk);
    m->fdc.disk = &m->disk;
    zx_pb_init(&m->cas);
    ay8910_init(&m->ay, MSX_CLOCK / 2, m->audio_rate);
    ay8910_set_amplitude(&m->ay, 0.8f);
    m->cpu.read = read_byte;
    m->cpu.write = write_byte;
    m->cpu.in = in_port;
    m->cpu.out = out_port;
    m->cpu.cycles = on_cycles;
    m->cpu.hook = bios_hook;
    m->cpu.user = m;
    zd_init(&m->cpu);
#ifdef MSX_SMALL
    msx_set_row_out(m, NULL);
    if (!m->pen) {
        msx_destroy(m);
        return NULL;
    }
#endif
    msx_set_rgb565(m, NULL);
    return m;
}

void msx_destroy(msx *m)
{
    if (!m) return;
    for (int i = 0; i < MAX_SEGS; i++) free(m->seg[i]);
    v99_free(&m->v99);
    msxc_free(&m->cart);
    msxd_eject(&m->disk);
    zx_pb_free(&m->cas);
#ifdef MSX_SMALL
    free(m->pen);
#endif
    free(m);
}

void msx_set_roms(msx *m, const uint8_t *bios, const uint8_t *sub, const uint8_t *disk)
{
    m->bios = bios;
    m->sub = sub;
    m->diskrom = disk;
    msx_reset(m);
}

bool msx_has_disk_rom(const msx *m) { return m->diskrom != NULL; }
int msx_model(const msx *m) { return m->model; }

static void update_lines(msx *m)
{
    // the BIOS's ID byte $2B, bit 7: 50 Hz (else 60); a Japanese MSX2 cartridge: 60
    const bool pal = m->bios && (m->bios[0x2b] & 0x80) && !m->japan;
    m->lines = pal ? LINES_PAL : LINES_NTSC;
}

double msx_fps(const msx *m) { return (double)MSX_CLOCK / CYCLES_PER_LINE / m->lines; }

void msx_reset(msx *m)
{
    zd_reset(&m->cpu);
    ay8910_reset(&m->ay);
    if (m->model == MSX_2) v99_reset(&m->v99);
    else tms_reset(&m->tms);
    msxc_reset(&m->cart);
    msxf_reset(&m->fdc);
    for (int i = 0; i < m->nsegs; i++) memset(m->seg[i], 0, 0x4000);
    memset(m->keys, 0xff, sizeof m->keys);
    m->joy[0] = m->joy[1] = 0x3f;
    m->joy_select = 0;
    m->port_a = 0;
    m->port_c = 0x7f;
    m->subslot3 = 0;
    for (int p = 0; p < 4; p++) m->mapper[p] = (uint8_t)((3 - p) % m->nsegs);
    m->fdc_control = 0;
    m->irq_line = false;
    m->rtc_addr = m->rtc_mode = 0;
    memset(m->rtc_bank, 0, sizeof m->rtc_bank);
    m->audio_acc = 0;
    m->naudio = 0;
    m->cas_pos = 0;
    update_lines(m);
    if (m->bios) map_pages(m);
    memset(m->row_done, 0, sizeof m->row_done);
}

#ifdef MSX_SMALL
#define TOP ((MSX_PIC_H - ROWS) / 2)
#define LEFT ((MSX_PIC_W - 256) / 2)

static inline uint16_t to565(const msx *m, uint32_t c)
{
    return m->rgb565 ? m->rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c)
                     : (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x1f));
}

// the 16 colours in RGB565, again when the V9938's palette changed
static void update_pal565(msx *m)
{
    if (m->model == MSX_2) {
        if (!memcmp(m->pal_seen, m->v99.palette, sizeof m->pal_seen)) return;
        memcpy(m->pal_seen, m->v99.palette, sizeof m->pal_seen);
        for (int i = 0; i < 16; i++) m->pal565[i] = to565(m, v99_palette_rgb(&m->v99, i));
    }
}

static void out_row(msx *m, int row, const uint8_t *pen, bool g7)
{
    uint16_t *o = m->row_out(TOP + row);
    bool bg7;
    const uint8_t back = m->model == MSX_2 ? v99_backdrop_pen(&m->v99, &bg7) : tms_backdrop(&m->tms);
    const uint16_t border = (m->model == MSX_2 && bg7) ? m->g7_565[back] : m->pal565[back & 15];
    for (int x = 0; x < LEFT; x++) o[x] = o[MSX_PIC_W - 1 - x] = border;
    if (g7) for (int x = 0; x < 256; x++) o[LEFT + x] = m->g7_565[pen[x]];
    else for (int x = 0; x < 256; x++) o[LEFT + x] = m->pal565[pen[x] & 15];
}
#endif

int msx_run_frame(msx *m, const int16_t **audio)
{
    m->naudio = 0;
    uint8_t pen[512];
    memset(m->row_done, 0, sizeof m->row_done);
    for (int line = 0; line < m->lines; line++) {
        zd_run(&m->cpu, CYCLES_PER_LINE);
        int width = 0, row = -1;
        bool g7 = false;
        if (m->model == MSX_2) {
            width = v99_line(&m->v99, line, pen, &g7);
            row = line + (v99_height(&m->v99) == 192 ? 10 : 0);
        } else if (tms_line(&m->tms, line, pen)) {
            width = 256;
            row = line + 10;
        }
        if (!width || row < 0 || row >= ROWS) continue;
        m->row_done[row] = 1;
#ifdef MSX_SMALL
        if (width == 512) {     // the 512-dot modes at half width: a dot of each pair, the one not backdrop
            bool bg7;
            const uint8_t back = m->model == MSX_2 ? v99_backdrop_pen(&m->v99, &bg7) : 0;
            for (int x = 0; x < 256; x++) pen[x] = pen[2 * x] != back ? pen[2 * x] : pen[2 * x + 1];
        }
        if (m->row_out) {
            update_pal565(m);
            out_row(m, row, pen, g7);
        } else {
            memcpy(m->pen[row], pen, 256);
            m->row_g7[row] = g7;
        }
#else
        uint32_t *o = m->frame[row];
        if (m->model == MSX_1) {
            for (int x = 0; x < 256; x++) o[2 * x] = o[2 * x + 1] = tms_palette[pen[x]];
        } else {
            uint32_t pal[16];
            for (int i = 0; i < 16; i++) pal[i] = v99_palette_rgb(&m->v99, i);
            if (width == 512) for (int x = 0; x < 512; x++) o[x] = pal[pen[x] & 15];
            else if (g7) for (int x = 0; x < 256; x++) o[2 * x] = o[2 * x + 1] = v99_g7_rgb(pen[x]);
            else for (int x = 0; x < 256; x++) o[2 * x] = o[2 * x + 1] = pal[pen[x] & 15];
        }
#endif
    }
    // the rows nothing drew (a 192-line mode's borders): the backdrop
    bool g7 = false;
    const uint8_t back = m->model == MSX_2 ? v99_backdrop_pen(&m->v99, &g7) : tms_backdrop(&m->tms);
    for (int row = 0; row < ROWS; row++) {
        if (m->row_done[row]) continue;
#ifdef MSX_SMALL
        memset(pen, back, 256);
        if (m->row_out) {
            update_pal565(m);
            out_row(m, row, pen, g7);
        } else {
            memcpy(m->pen[row], pen, 256);
            m->row_g7[row] = g7;
        }
#else
        const uint32_t c = m->model == MSX_1 ? tms_palette[back] : g7 ? v99_g7_rgb(back) : v99_palette_rgb(&m->v99, back);
        for (int x = 0; x < 512; x++) m->frame[row][x] = c;
#endif
    }
#ifdef MSX_SMALL
    m->border_pen = back;               // (the border: this frame's backdrop)
    m->border_g7 = g7;
    update_pal565(m);
    if (m->row_out) {                   // the border rows above and below
        const uint16_t c = g7 ? m->g7_565[back] : m->pal565[back & 15];
        for (int y = 0; y < MSX_PIC_H; y++) {
            if (y >= TOP && y < TOP + ROWS) continue;
            uint16_t *o = m->row_out(y);
            for (int x = 0; x < MSX_PIC_W; x++) o[x] = c;
        }
    }
#endif
    if (audio) *audio = m->audio;
    return m->naudio;
}

void msx_set_turbo(msx *m, bool on) { m->turbo = on; }

#ifdef MSX_SMALL
void msx_set_rgb565(msx *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b))
{
    m->rgb565 = fn;
    for (int i = 0; i < 16; i++) m->pal565[i] = to565(m, m->model == MSX_2 ? v99_palette_rgb(&m->v99, i) : tms_palette[i]);
    memset(m->pal_seen, 0xff, sizeof m->pal_seen);
    for (int i = 0; i < 256; i++) {
        const uint32_t c = v99_g7_rgb((uint8_t)i);
        m->g7_565[i] = fn ? fn((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c)
                          : (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x1f));
    }
}

void msx_render_row565(const msx *m, int y, uint16_t *out)
{
    const int row = y - TOP;
    const uint16_t border = m->border_g7 ? m->g7_565[m->border_pen] : m->pal565[m->border_pen & 15];
    if (row < 0 || row >= ROWS || !m->pen) {
        for (int x = 0; x < MSX_PIC_W; x++) out[x] = border;
        return;
    }
    for (int x = 0; x < LEFT; x++) out[x] = out[MSX_PIC_W - 1 - x] = border;
    const uint8_t *p = m->pen[row];
    if (m->row_g7[row]) for (int x = 0; x < 256; x++) out[LEFT + x] = m->g7_565[p[x]];
    else for (int x = 0; x < 256; x++) out[LEFT + x] = m->pal565[p[x] & 15];
}

void msx_set_row_out(msx *m, uint16_t *(*fn)(int y))
{
    m->row_out = fn;
    if (fn) {
        free(m->pen);
        m->pen = NULL;
    } else if (!m->pen) {
        m->pen = (uint8_t (*)[256])calloc(ROWS, 256);
    }
}

void msx_render_row(const msx *m, int y, uint32_t *out)
{
    uint16_t row[MSX_PIC_W];
    msx_render_row565(m, y, row);
    for (int x = 0; x < MSX_PIC_W; x++) {
        const uint16_t c = row[x];
        out[x] = ((uint32_t)((c >> 11) * 255 / 31) << 16) | ((uint32_t)(((c >> 5) & 63) * 255 / 63) << 8) | (uint32_t)((c & 31) * 255 / 31);
    }
}
#else
void msx_set_rgb565(msx *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b)) { (void)m; (void)fn; }
void msx_set_row_out(msx *m, uint16_t *(*fn)(int y)) { (void)m; (void)fn; }

void msx_render_row(const msx *m, int y, uint32_t *out)
{
    memcpy(out, m->frame[(y >> 1) % ROWS], 512 * sizeof *out);
}

void msx_render_row565(const msx *m, int y, uint16_t *out)
{
    const uint32_t *src = m->frame[(y >> 1) % ROWS];
    for (int x = 0; x < 512; x++) {
        const uint32_t c = src[x];
        out[x] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x1f));
    }
}
#endif

void msx_keys_clear(msx *m) { memset(m->keys, 0xff, sizeof m->keys); }

void msx_key(msx *m, int code, bool down)
{
    const int row = code >> 3, bit = code & 7;
    if (row < 0 || row > 10) return;
    if (down) m->keys[row] &= (uint8_t)~(1 << bit);
    else m->keys[row] |= (uint8_t)(1 << bit);
}

void msx_joy(msx *m, int port, uint8_t bits)
{
    uint8_t v = 0x3f;
    if (bits & MSX_JOY_UP) v &= (uint8_t)~0x01;
    if (bits & MSX_JOY_DOWN) v &= (uint8_t)~0x02;
    if (bits & MSX_JOY_LEFT) v &= (uint8_t)~0x04;
    if (bits & MSX_JOY_RIGHT) v &= (uint8_t)~0x08;
    if (bits & MSX_JOY_A) v &= (uint8_t)~0x10;
    if (bits & MSX_JOY_B) v &= (uint8_t)~0x20;
    m->joy[(port - 1) & 1] = v;
}

// ---------------------------------------------------------------------------
// media
// ---------------------------------------------------------------------------
static bool ext_is(const char *name, const char *ext)
{
    const size_t n = strlen(name), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)name[n - k + i]) != ext[i]) return false;
    return true;
}

static bool japanese_name(const char *name)
{
    char l[256];
    size_t n = 0;
    for (; name[n] && n < sizeof l - 1; n++) l[n] = (char)tolower((unsigned char)name[n]);
    l[n] = 0;
    return strstr(l, "(j)") || strstr(l, "[j]") || strstr(l, "(japan") || strstr(l, "(jp)") || strstr(l, "_j.") ||
           strstr(l, " j.") || strstr(l, "-j.");
}

bool msx_load_pbuf(msx *m, zx_pbuf *pb, const char *name)
{
    m->error = NULL;
    if (!pb->size) {
        m->error = "empty file";
        zx_pb_free(pb);
        return false;
    }
    if (ext_is(name, ".dsk")) {
        if (!m->diskrom) {
            m->error = "no disk ROM";
            zx_pb_free(pb);
            return false;
        }
        if (!msxd_insert(&m->disk, pb, &m->error)) return false;
        msx_reset(m);
        return true;
    }
    if (ext_is(name, ".cas")) {
        zx_pb_free(&m->cas);
        m->cas = *pb;
        zx_pb_init(pb);
    } else if (ext_is(name, ".tsx") || ext_is(name, ".tzx")) {
        const bool ok = tzx_to_cas(m, pb);
        zx_pb_free(pb);
        if (!ok) return false;
    } else {                                    // a cartridge
        if (!msxc_load(&m->cart, pb, name)) {
            m->error = "cannot read the cartridge";
            return false;
        }
        m->japan = m->model == MSX_2 && japanese_name(name);
        msx_reset(m);
        return true;
    }
    m->cas_loaded = true;
    m->cas_pos = 0;
    cas_autorun(m);
    return true;
}

void msx_eject_cart(msx *m)
{
    msxc_free(&m->cart);
    m->japan = false;
    msx_reset(m);
}

bool msx_load_file(msx *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->error = "cannot open the file";
        return false;
    }
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t buf[4096];
    size_t k;
    bool ok = true;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0)
        if (!(ok = zx_pb_append(&pb, buf, k))) break;
    fclose(f);
    if (!ok || !zx_pb_finish(&pb)) {
        zx_pb_free(&pb);
        m->error = "out of memory";
        return false;
    }
    return msx_load_pbuf(m, &pb, path);
}

const char *msx_last_error(const msx *m) { return m->error ? m->error : ""; }
