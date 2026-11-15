// atari8_core.c - Atari 400/800, 800XL and 130XE: a C11 migration of
// dsp-cpp's drivers/computers/atari8 (see atari8_core.h).
#include "atari8_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "antic.h"
#include "gtia.h"
#include "m65xx.h"
#include "pokey.h"

#define SIOV 0xe459

struct atari8 {
    m65xx cpu;
    gtia gt;
    antic an;
    pokey pk;
    int model;
    int rate;
    uint8_t *ram;                   // 64 KB
    const uint8_t *os, *basic;
    bool has_basic;

    uint8_t porta, portb, pactl, pbctl, porta_dir, portb_dir;
    bool os_enabled, basic_enabled, selftest_enabled;
    uint8_t stick[2], trig[2], console;
    int boot_option_frames;

    // the next frame's input (dsp-cpp's set_inputs)
    int key_code;
    bool key_shift, key_ctrl, break_req;
    unsigned joy_bits[2], console_bits;

    zx_pbuf disk;
    int disk_sector_size, disk_sectors;
    bool disk_loaded;
    const char *error;

    uint8_t *frame;                 // A8_W x A8_H colour bytes
    uint8_t offscreen[A8_W];
    int16_t audio[A8_AUDIO_MAX];
    int naudio;
    int64_t audio_acc;
    bool irq_line, turbo;
    uint16_t pal565[256];
};

// dsp-cpp's kKeyTable order (then the keys it has no host key for)
static const int8_t kKeyOrder[] = {
    0x00, 0x01, 0x02, 0x05, 0x06, 0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x12, 0x15, 0x16, 0x17,
    0x18, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x25, 0x26, 0x28, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x32, 0x33, 0x34, 0x35, 0x38, 0x39, 0x3a, 0x3c, 0x3d, 0x3e, 0x3f,
    0x07, 0x27, 0x36, 0x37, -1,
};

int atari8_key_order(int i)
{
    return i >= 0 && i < (int)sizeof kKeyOrder ? kKeyOrder[i] : -1;
}

const char *atari8_title(const atari8 *m)
{
    switch (m->model) {
    case A8_MODEL_XL: return "Atari 800XL";
    case A8_MODEL_XE: return "Atari 800XE";
    default: return "Atari 800";
    }
}

int atari8_model(const atari8 *m) { return m->model; }

// --- memory ---------------------------------------------------------------------
static void map_pages(atari8 *m)
{
    m65xx *c = &m->cpu;
    for (int p = 0; p < 0xc0; p++) {
        c->page_r[p] = m->ram + (p << 8);
        c->page_w[p] = m->ram + (p << 8);
    }
    if (m->model != A8_MODEL_800 && m->basic_enabled)
        for (int p = 0xa0; p < 0xc0; p++) c->page_r[p] = m->basic + ((p - 0xa0) << 8);
    if (m->selftest_enabled)
        for (int p = 0x50; p < 0x58; p++) c->page_r[p] = NULL;
    for (int p = 0xc0; p < 0x100; p++) c->page_r[p] = c->page_w[p] = NULL;
    for (int p = 0xc0; p < 0x100; p++) {
        if (p >= 0xd0 && p < 0xd8) continue;
        if (p == (SIOV >> 8)) continue;             // the SIO patch's page: through the handler
        if (m->model == A8_MODEL_800) {
            if (p >= 0xd8) c->page_r[p] = m->os + ((p - 0xd8) << 8);
            else c->page_r[p] = m->ram + (p << 8);
        } else if (m->os_enabled) {
            c->page_r[p] = m->os + ((p - 0xc0) << 8);
        } else {
            c->page_r[p] = c->page_w[p] = m->ram + (p << 8);
        }
    }
}

static void update_banking(atari8 *m)
{
    if (m->model == A8_MODEL_800) return;
    m->os_enabled = (m->portb & 0x01) != 0;
    m->basic_enabled = m->has_basic && (m->portb & 0x02) == 0;
    m->selftest_enabled = (m->portb & 0x80) == 0;
    map_pages(m);
}

static uint8_t antic_mem(void *user, uint16_t a)
{
    atari8 *m = user;
    if (a >= 0xd000 && a < 0xd800) return m->ram[a];
    if (m->model == A8_MODEL_800) {
        if (a >= 0xd800) return m->os[a - 0xd800];
    } else {
        if (m->os_enabled && a >= 0xc000 && a < 0xd000) return m->os[a - 0xc000];
        if (m->os_enabled && a >= 0xd800) return m->os[a - 0xc000];
        if (m->basic_enabled && a >= 0xa000 && a < 0xc000) return m->basic[a - 0xa000];
    }
    return m->ram[a];
}

static void service_sio(atari8 *m);

static uint8_t pia_read(atari8 *m, uint16_t off)
{
    switch (off & 3) {
    case 0:
        if (!(m->pactl & 0x04)) return m->porta_dir;
        return (uint8_t)((m->stick[0] & 0x0f) | ((m->stick[1] & 0x0f) << 4));
    case 1:
        if (!(m->pbctl & 0x04)) return m->portb_dir;
        if (m->model == A8_MODEL_800) return 0xff;
        return m->portb;
    case 2: return m->pactl;
    default: return m->pbctl;
    }
}

static void pia_write(atari8 *m, uint16_t off, uint8_t v)
{
    switch (off & 3) {
    case 0:
        if (m->pactl & 0x04) m->porta = v;
        else m->porta_dir = v;
        break;
    case 1:
        if (m->pbctl & 0x04) {
            m->portb = v;
            update_banking(m);
        } else {
            m->portb_dir = v;
        }
        break;
    case 2: m->pactl = v; break;
    default: m->pbctl = v; break;
    }
}

static uint8_t cpu_read(void *user, uint16_t a)
{
    atari8 *m = user;
    if (a == SIOV && m->cpu.pc == (uint16_t)(SIOV + 1) && m->os_enabled) {
        service_sio(m);
        return 0x60;                                // RTS
    }
    if (a < 0xc000) {
        if (m->model != A8_MODEL_800 && m->basic_enabled && a >= 0xa000) return m->basic[a - 0xa000];
        if (m->selftest_enabled && a >= 0x5000 && a < 0x5800) return m->os[0x1000 + (a - 0x5000)];
        return m->ram[a];
    }
    if (m->model == A8_MODEL_800) {
        if (a >= 0xd800) return m->os[a - 0xd800];
    } else {
        if (a < 0xd000) return m->os_enabled ? m->os[a - 0xc000] : m->ram[a];
        if (a >= 0xd800) return m->os_enabled ? m->os[a - 0xc000] : m->ram[a];
    }
    switch (a & 0xff00) {
    case 0xd000: return gtia_read(&m->gt, a & 0x1f);
    case 0xd200: return pokey_read(&m->pk, a & 0x0f);
    case 0xd300: return pia_read(m, a & 3);
    case 0xd400: return antic_read(&m->an, a & 0x0f);
    default: return 0xff;
    }
}

static void cpu_write(void *user, uint16_t a, uint8_t v)
{
    atari8 *m = user;
    if (a < 0xc000) {
        m->ram[a] = v;
        return;
    }
    if (a >= 0xd800 || (m->model != A8_MODEL_800 && a < 0xd000)) {
        if (m->model != A8_MODEL_800 && !m->os_enabled) m->ram[a] = v;
        return;
    }
    switch (a & 0xff00) {
    case 0xd000: gtia_write(&m->gt, a & 0x1f, v); break;
    case 0xd200: pokey_write(&m->pk, a & 0x0f, v); break;
    case 0xd300: pia_write(m, a & 3, v); break;
    case 0xd400: antic_write(&m->an, a & 0x0f, v); break;
    default: break;
    }
}

// --- the disk: SIO --------------------------------------------------------------------
static bool sector_at(const atari8 *m, int sector, uint32_t *off, uint32_t *len)
{
    if (!m->disk_loaded || sector < 1) return false;
    if (m->disk_sector_size == 128 || sector <= 3) {
        *off = 16 + (uint32_t)(sector - 1) * 128;
        *len = 128;
    } else {
        *off = 16 + 384 + (uint32_t)(sector - 4) * 256;
        *len = 256;
    }
    return *off + *len <= m->disk.size;
}

static bool disk_write(atari8 *m, uint32_t off, uint8_t v)
{
    zx_pbuf *pb = &m->disk;
    if (pb->flat) {
        pb->flat[off] = v;
        return true;
    }
    if (pb->lazy) return false;                     // (left on the card: read only)
    uint8_t **pg = &pb->pages[off >> ZX_PAGE_BITS];
    if (!*pg) {
        uint8_t *p = malloc(ZX_PAGE);
        if (!p) return false;
        memset(p, pb->fill[off >> ZX_PAGE_BITS], ZX_PAGE);
        *pg = p;
    }
    (*pg)[off & (ZX_PAGE - 1)] = v;
    return true;
}

static void service_sio(atari8 *m)
{
    uint8_t *ram = m->ram;
    const uint8_t device = ram[0x0300];
    const uint8_t command = ram[0x0302];
    const uint16_t buffer = (uint16_t)(ram[0x0304] | (ram[0x0305] << 8));
    const uint16_t count = (uint16_t)(ram[0x0308] | (ram[0x0309] << 8));
    const int sector = ram[0x030a] | (ram[0x030b] << 8);
    uint8_t status = 0x8a;                          // device does not respond
    if (device == 0x31 && m->disk_loaded) {
        uint32_t off, len;
        switch (command) {
        case 0x52:                                  // read sector
            if (sector_at(m, sector, &off, &len)) {
                const uint32_t n = count && count < len ? count : len;
                for (uint32_t i = 0; i < n; ++i) ram[(uint16_t)(buffer + i)] = zx_pb_byte(&m->disk, off + i);
                status = 0x01;
            } else {
                status = 0x90;
            }
            break;
        case 0x50:                                  // put sector
        case 0x57: {                                // write sector
            const uint32_t want = count ? count : (uint32_t)m->disk_sector_size;
            status = 0x90;
            if (sector_at(m, sector, &off, &len)) {
                const uint32_t n = want < len ? want : len;
                bool ok = true;
                for (uint32_t i = 0; i < n && ok; ++i) ok = disk_write(m, off + i, ram[(uint16_t)(buffer + i)]);
                if (ok) status = 0x01;
            }
            break;
        }
        case 0x53: {                                // status
            const uint8_t dd = m->disk_sector_size == 256 ? 0x20 : 0x00;
            ram[(uint16_t)(buffer + 0)] = (uint8_t)(0x10 | dd);
            ram[(uint16_t)(buffer + 1)] = 0xff;
            ram[(uint16_t)(buffer + 2)] = 0xe0;
            ram[(uint16_t)(buffer + 3)] = 0x00;
            status = 0x01;
            break;
        }
        case 0x4e:                                  // read PERCOM block
            for (int i = 0; i < 12; ++i) ram[(uint16_t)(buffer + i)] = 0;
            ram[(uint16_t)(buffer + 0)] = 40;
            ram[(uint16_t)(buffer + 2)] = 0;
            ram[(uint16_t)(buffer + 3)] = 18;
            ram[(uint16_t)(buffer + 6)] = (uint8_t)(m->disk_sector_size >> 8);
            ram[(uint16_t)(buffer + 7)] = (uint8_t)(m->disk_sector_size & 0xff);
            status = 0x01;
            break;
        default: status = 0x8b; break;
        }
    }
    ram[0x0303] = status;
    m->cpu.y = status;
    m->cpu.p.n = (status & 0x80) != 0;
    m->cpu.p.z = status == 0;
}

// --- chips' callbacks -------------------------------------------------------------------
static void on_nmi(void *user) { m65xx_set_nmi(&((atari8 *)user)->cpu, M65_PULSE); }

static void on_irq(void *user, uint8_t mask)
{
    (void)mask;
    atari8 *m = user;
    m->irq_line = true;
    m65xx_set_irq(&m->cpu, M65_ASSERT);
}

static uint8_t on_console(void *user)
{
    atari8 *m = user;
    uint8_t c = m->console;
    if (m->disk_loaded && m->boot_option_frames > 0) c = (uint8_t)(c & ~0x04);
    return c;
}

static uint8_t on_trigger(void *user, int n)
{
    atari8 *m = user;
    return n < 2 ? m->trig[n] : 1;
}

// --- the machine ------------------------------------------------------------------------
static uint16_t rgb565_default(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void atari8_set_rgb565(atari8 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b))
{
    const uint32_t *pal = antic_palette();
    for (int i = 0; i < 256; i++) m->pal565[i] = fn((uint8_t)(pal[i] >> 16), (uint8_t)(pal[i] >> 8), (uint8_t)pal[i]);
}

atari8 *atari8_create(int model, int audio_rate, const uint8_t *os, size_t os_size, const uint8_t *basic)
{
    const size_t need = model == A8_MODEL_800 ? 0x2800 : 0x4000;
    if (!os || os_size < need) return NULL;
    atari8 *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->ram = malloc(0x10000);
    m->frame = calloc(1, A8_W * A8_H);
    if (!m->ram || !m->frame) {
        atari8_destroy(m);
        return NULL;
    }
    m->model = model;
    m->rate = audio_rate > 0 ? audio_rate : A8_AUDIO_RATE;
    m->os = os;
    m->basic = model != A8_MODEL_800 ? basic : NULL;
    m->has_basic = m->basic != NULL;
    zx_pb_init(&m->disk);
    m->disk_sector_size = 128;

    m->cpu.read = cpu_read;
    m->cpu.write = cpu_write;
    m->cpu.user = m;
    m->gt.console = on_console;
    m->gt.trigger = on_trigger;
    m->gt.user = m;
    m->an.gtia = &m->gt;
    m->an.mem = antic_mem;
    m->an.nmi = on_nmi;
    m->an.user = m;
    pokey_init(&m->pk);
    m->pk.irq = on_irq;
    m->pk.irq_user = m;
    m->key_code = -1;
    atari8_set_rgb565(m, rgb565_default);
    atari8_reset(m);
    return m;
}

void atari8_destroy(atari8 *m)
{
    if (!m) return;
    zx_pb_free(&m->disk);
    free(m->pk.plan_a);
    free(m->pk.plan_b);
    free(m->ram);
    free(m->frame);
    free(m);
}

void atari8_reset(atari8 *m)
{
    memset(m->ram, 0, 0x10000);
    m->porta = m->portb = 0xff;
    m->pactl = m->pbctl = 0;
    m->porta_dir = m->portb_dir = 0;
    m->os_enabled = true;
    m->basic_enabled = false;
    m->selftest_enabled = false;
    m->stick[0] = m->stick[1] = 0x0f;
    m->trig[0] = m->trig[1] = 1;
    m->console = 0x07;
    m->boot_option_frames = 120;
    m->irq_line = false;
    m->naudio = 0;
    m->audio_acc = 0;
    memset(m->frame, 0, A8_W * A8_H);
    gtia_reset(&m->gt);
    antic_reset(&m->an);
    pokey_reset(&m->pk);
    map_pages(m);
    m65xx_reset(&m->cpu);
}

void atari8_set_key(atari8 *m, int code, bool shift, bool ctrl)
{
    m->key_code = code;
    m->key_shift = shift;
    m->key_ctrl = ctrl;
}

void atari8_break(atari8 *m) { m->break_req = true; }
void atari8_joy(atari8 *m, int port, unsigned bits) { if (port >= 0 && port < 2) m->joy_bits[port] = bits; }
void atari8_console(atari8 *m, unsigned bits) { m->console_bits = bits; }
void atari8_set_turbo(atari8 *m, bool on) { m->turbo = on; }

// dsp-cpp's apply_keys, before each frame
static void apply_inputs(atari8 *m)
{
    for (int p = 0; p < 2; p++) {
        const unsigned b = m->joy_bits[p];
        uint8_t s = 0x0f;
        if (b & A8_UP) s &= (uint8_t)~0x01;
        if (b & A8_DOWN) s &= (uint8_t)~0x02;
        if (b & A8_LEFT) s &= (uint8_t)~0x04;
        if (b & A8_RIGHT) s &= (uint8_t)~0x08;
        m->stick[p] = s;
        m->trig[p] = (b & A8_FIRE) ? 0 : 1;
    }
    m->console = (uint8_t)(0x07 & ~m->console_bits);
    if (m->key_code >= 0) {
        uint8_t code = (uint8_t)(m->key_code & 0x3f);
        if (m->key_shift) code |= 0x40;
        if (m->key_ctrl) code |= 0x80;
        pokey_set_key(&m->pk, code, true, m->key_shift);
    } else {
        pokey_set_key(&m->pk, 0, false, m->key_shift);
    }
    if (m->break_req) {
        m->break_req = false;
        pokey_press_break(&m->pk);
    }
}

static void on_cycles(atari8 *m, int cycles)
{
    pokey_run(&m->pk, cycles);
    m->audio_acc += (int64_t)cycles * m->rate;
    while (m->audio_acc >= A8_CPU_CLOCK) {
        m->audio_acc -= A8_CPU_CLOCK;
        int32_t s = pokey_update(&m->pk);
        s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
        if (m->naudio < A8_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)s;
    }
}

int atari8_run_frame(atari8 *m, const int16_t **audio)
{
    apply_inputs(m);
    m->naudio = 0;
    if (m->boot_option_frames > 0) --m->boot_option_frames;
    antic_begin_frame(&m->an);
    for (int line = 0; line < ANTIC_LINES_PER_FRAME; ++line) {
        const int fb_line = line - ANTIC_FIRST_VISIBLE_LINE;
        uint8_t *dst = fb_line >= 0 && fb_line < A8_H ? m->frame + fb_line * A8_W : m->offscreen;
        const int stolen = antic_scanline(&m->an, line, dst);
        m->an.wsync = false;
        const int budget = ANTIC_CYCLES_PER_LINE - stolen;
        int done = 0;
        while (done < budget) {
            if (m->an.wsync) {
                m->an.wsync = false;
                break;
            }
            const int ran = m65xx_run(&m->cpu, 1);
            if (ran <= 0) break;
            done += ran;
        }
        on_cycles(m, ANTIC_CYCLES_PER_LINE);
        if (m->irq_line) {
            m->irq_line = false;
            m65xx_set_irq(&m->cpu, M65_CLEAR);
        }
    }
    if (audio) *audio = m->audio;
    return m->turbo ? 0 : m->naudio;
}

const uint8_t *atari8_frame(const atari8 *m) { return m->frame; }

void atari8_render_row(const atari8 *m, int y, uint32_t *out)
{
    const uint32_t *pal = antic_palette();
    const uint8_t *s = m->frame + y * A8_W;
    for (int x = 0; x < A8_W; x++) out[x] = pal[s[x]];
}

void atari8_render_row565(const atari8 *m, int y, int x0, int n, uint16_t *out)
{
    const uint8_t *s = m->frame + y * A8_W + x0;
    for (int x = 0; x < n; x++) out[x] = m->pal565[s[x]];
}

// --- media ------------------------------------------------------------------------------
const char *atari8_last_error(const atari8 *m) { return m->error ? m->error : ""; }
bool atari8_has_disk(const atari8 *m) { return m->disk_loaded; }

void atari8_eject(atari8 *m)
{
    zx_pb_free(&m->disk);
    zx_pb_init(&m->disk);
    m->disk_loaded = false;
}

bool atari8_load_pbuf(atari8 *m, zx_pbuf *pb, const char *name)
{
    (void)name;
    m->error = NULL;
    if (pb->size < 16 || zx_pb_byte(pb, 0) != 0x96 || zx_pb_byte(pb, 1) != 0x02) {
        m->error = "not an Atari .ATR disk image";
        zx_pb_free(pb);
        return false;
    }
    const int ss = zx_pb_byte(pb, 4) | (zx_pb_byte(pb, 5) << 8);
    if (ss != 128 && ss != 256) {
        m->error = "unsupported ATR sector size";
        zx_pb_free(pb);
        return false;
    }
    const uint32_t paragraphs = zx_pb_byte(pb, 2) | (zx_pb_byte(pb, 3) << 8) | ((uint32_t)zx_pb_byte(pb, 6) << 16);
    const uint32_t bytes = paragraphs * 16;
    atari8_eject(m);
    m->disk_sector_size = ss;
    m->disk_sectors = ss == 128 ? (int)(bytes / 128) : (int)(3 + (bytes > 384 ? (bytes - 384) / 256 : 0));
    m->disk = *pb;
    m->disk_loaded = true;
    // (dsp-cpp: a disk is booted by a cold start, OPTION held so that BASIC stays out)
    atari8_reset(m);
    return true;
}

bool atari8_load_mem(atari8 *m, const uint8_t *data, size_t size, const char *name)
{
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) {
        m->error = "out of memory for the disk";
        return false;
    }
    memcpy(copy, data, size);
    zx_pb_take_flat(&pb, copy, size);
    return atari8_load_pbuf(m, &pb, name);
}

bool atari8_load_file(atari8 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->error = "cannot open the disk image";
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16 * 1024 * 1024) {
        fclose(f);
        m->error = "not an Atari .ATR disk image";
        return false;
    }
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        fclose(f);
        m->error = "cannot read the disk image";
        return false;
    }
    fclose(f);
    zx_pbuf pb;
    zx_pb_init(&pb);
    zx_pb_take_flat(&pb, data, (size_t)n);
    return atari8_load_pbuf(m, &pb, path);
}
