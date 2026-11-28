// atari8_core.c - Atari 400/800 and 800XL/XE: C port of dsp-cpp's
// drivers/computers/atari8 (dspc.h). The 6502B (m65xx) at 1.79 MHz, ANTIC and
// GTIA (video/antic_gtia), POKEY (sound/pokey: sound, keyboard, IRQs), the
// 6520 PIA (joysticks, XL/XE banking) and the SIO patch that answers the OS
// disk calls straight from a mounted .ATR image.
// DSPC_MODEL: 0 = 800, 1 = 800XL, 2 = 800XE.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../video/antic_gtia.h"
#include "../../../sound/pokey.h"

#ifndef DSPC_MODEL
#define DSPC_MODEL 0
#endif
#define XLXE (DSPC_MODEL != 0)
#define CLOCK 1789790
#define RATE 44100
#define SIOV 0xe459

static const char *const kExts[] = { ".atr", NULL };
const dspc_info dspc_machine = {
#if DSPC_MODEL == 2
    "a800xe", "Atari 800XE",
#elif DSPC_MODEL == 1
    "a800xl", "Atari 800XL",
#else
    "a800", "Atari 800",
#endif
    ANTIC_W, ANTIC_H, 1, (double)CLOCK / (ANTIC_LINES * ANTIC_CPL), RATE, "ATR", kExts, "*.atr *.ATR", ".atr" };

static struct {
    m65xx cpu;
    gtia gt;
    antic an;
    pokey pk;
    uint8_t ram[0x10000];
    uint8_t os[0x4000];
    uint8_t basic[0x2000];
    bool has_basic;
    uint8_t porta, portb, pactl, pbctl, porta_dir, portb_dir;
    bool os_enabled, basic_enabled, selftest_enabled;
    uint8_t stick[2], trig[2], console;
    int boot_option_frames;
    uint8_t *disk;
    size_t disk_n;
    int sector_size, sectors;
    bool disk_loaded, irq_line;
    int64_t audio_acc;
    int16_t audio[2048];
    int naudio;
    uint32_t fb[ANTIC_W * ANTIC_H];
} S;

static void update_banking(void)
{
    if (!XLXE) return;
    S.os_enabled = (S.portb & 0x01) != 0;
    S.basic_enabled = S.has_basic && (S.portb & 0x02) == 0;
    S.selftest_enabled = (S.portb & 0x80) == 0;
}

static uint8_t antic_mem(void *u, uint16_t a)
{
    (void)u;
    if (a >= 0xd000 && a < 0xd800) return S.ram[a];
    if (!XLXE) {
        if (a >= 0xd800) return S.os[a - 0xd800];
    } else {
        if (S.os_enabled && a >= 0xc000 && a < 0xd000) return S.os[a - 0xc000];
        if (S.os_enabled && a >= 0xd800) return S.os[a - 0xc000];
        if (S.basic_enabled && a >= 0xa000 && a < 0xc000) return S.basic[a - 0xa000];
    }
    return S.ram[a];
}

// --- disk (the SIO patch) ----------------------------------------------------------
static bool sector_at(int sector, size_t *off, size_t *len)
{
    if (!S.disk_loaded || sector < 1) return false;
    if (S.sector_size == 128 || sector <= 3) {
        *off = 16 + (size_t)(sector - 1) * 128;
        *len = 128;
    } else {
        *off = 16 + 384 + (size_t)(sector - 4) * 256;
        *len = 256;
    }
    return *off + *len <= S.disk_n;
}

static void service_sio(void)
{
    const uint8_t device = S.ram[0x0300], command = S.ram[0x0302];
    const uint16_t buffer = (uint16_t)(S.ram[0x0304] | (S.ram[0x0305] << 8));
    const uint16_t count = (uint16_t)(S.ram[0x0308] | (S.ram[0x0309] << 8));
    const int sector = S.ram[0x030a] | (S.ram[0x030b] << 8);
    uint8_t status = 0x8a;
    size_t off, len;
    if (device == 0x31 && S.disk_loaded) {
        switch (command) {
        case 0x52:
            if (sector_at(sector, &off, &len)) {
                const size_t n = count && count < len ? count : len;
                for (size_t i = 0; i < n; ++i) S.ram[(uint16_t)(buffer + i)] = S.disk[off + i];
                status = 0x01;
            } else status = 0x90;
            break;
        case 0x50: case 0x57: {
            const size_t want = count ? count : (size_t)S.sector_size;
            if (sector_at(sector, &off, &len)) {
                const size_t n = want < len ? want : len;
                for (size_t i = 0; i < n; ++i) S.disk[off + i] = S.ram[(uint16_t)(buffer + i)];
                status = 0x01;
            } else status = 0x90;
            break;
        }
        case 0x53:
            S.ram[buffer] = (uint8_t)(0x10 | (S.sector_size == 256 ? 0x20 : 0));
            S.ram[(uint16_t)(buffer + 1)] = 0xff;
            S.ram[(uint16_t)(buffer + 2)] = 0xe0;
            S.ram[(uint16_t)(buffer + 3)] = 0x00;
            status = 0x01;
            break;
        case 0x4e:
            for (int i = 0; i < 12; ++i) S.ram[(uint16_t)(buffer + i)] = 0;
            S.ram[buffer] = 40;
            S.ram[(uint16_t)(buffer + 3)] = 18;
            S.ram[(uint16_t)(buffer + 6)] = (uint8_t)(S.sector_size >> 8);
            S.ram[(uint16_t)(buffer + 7)] = (uint8_t)S.sector_size;
            status = 0x01;
            break;
        default: status = 0x8b; break;
        }
    }
    S.ram[0x0303] = status;
    S.cpu.y = status;
    S.cpu.p.n = (status & 0x80) != 0;
    S.cpu.p.z = status == 0;
}

// --- the PIA --------------------------------------------------------------------------
static uint8_t pia_read(uint16_t o)
{
    switch (o & 3) {
    case 0:
        if (!(S.pactl & 0x04)) return S.porta_dir;
        return (uint8_t)((S.stick[0] & 0x0f) | ((S.stick[1] & 0x0f) << 4));
    case 1:
        if (!(S.pbctl & 0x04)) return S.portb_dir;
        return XLXE ? S.portb : 0xff;
    case 2: return S.pactl;
    default: return S.pbctl;
    }
}

static void pia_write(uint16_t o, uint8_t v)
{
    switch (o & 3) {
    case 0: if (S.pactl & 0x04) S.porta = v; else S.porta_dir = v; break;
    case 1:
        if (S.pbctl & 0x04) { S.portb = v; update_banking(); }
        else S.portb_dir = v;
        break;
    case 2: S.pactl = v; break;
    default: S.pbctl = v; break;
    }
}

static uint8_t c_read(void *u, uint16_t a)
{
    (void)u;
    if (a == SIOV && S.cpu.pc == (uint16_t)(SIOV + 1) && S.os_enabled) {
        service_sio();
        return 0x60;
    }
    if (a < 0xc000) {
        if (XLXE && S.basic_enabled && a >= 0xa000) return S.basic[a - 0xa000];
        if (S.selftest_enabled && a >= 0x5000 && a < 0x5800) return S.os[0x1000 + (a - 0x5000)];
        return S.ram[a];
    }
    if (!XLXE) {
        if (a >= 0xd800) return S.os[a - 0xd800];
    } else {
        if (a < 0xd000 || a >= 0xd800) return S.os_enabled ? S.os[a - 0xc000] : S.ram[a];
    }
    switch (a & 0xff00) {
    case 0xd000: return gtia_read(&S.gt, a & 0x1f);
    case 0xd200: return pokey_read(&S.pk, a & 0x0f);
    case 0xd300: return pia_read(a & 3);
    case 0xd400: return antic_read(&S.an, a & 0x0f);
    default: return 0xff;
    }
}

static void c_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0xc000) { S.ram[a] = v; return; }
    if (a >= 0xd800 || (XLXE && a < 0xd000)) {
        if (XLXE && !S.os_enabled) S.ram[a] = v;
        return;
    }
    switch (a & 0xff00) {
    case 0xd000: gtia_write(&S.gt, a & 0x1f, v); break;
    case 0xd200: pokey_write(&S.pk, a & 0x0f, v); break;
    case 0xd300: pia_write(a & 3, v); break;
    case 0xd400: antic_write(&S.an, a & 0x0f, v); break;
    default: break;
    }
}

static void an_nmi(void *u) { (void)u; m65xx_set_nmi(&S.cpu, M65_PULSE); }
static void pk_irq(void *u, uint8_t m) { (void)u; (void)m; S.irq_line = true; m65xx_set_irq(&S.cpu, M65_ASSERT); }
static uint8_t gt_console(void *u)
{
    (void)u;
    uint8_t c = S.console;
    if (S.disk_loaded && S.boot_option_frames > 0) c &= (uint8_t)~0x04;
    return c;
}
static uint8_t gt_trigger(void *u, int n) { (void)u; return n < 2 ? S.trig[n] : 1; }

static bool load_roms(void)
{
    static const char *const sets[] = {
#if DSPC_MODEL == 2
        "a800xe",
#elif DSPC_MODEL == 1
        "a800xl",
#else
        "a800",
#endif
        NULL };
    uint32_t len;
    uint8_t *p;
#if DSPC_MODEL == 0
    uint8_t *a = romfs_get(sets, "co12499b.rom", 0xd818f3e8, 0x1000, &len), *b = NULL;
    if (a) b = romfs_get(sets, "co14599b.rom", 0xc1690a9b, 0x1000, &len);
    if (!a || !b) {
        free(a);
        free(b);
        a = romfs_get(sets, "co12499a.rom", 0x29f64e17, 0x1000, &len);
        b = romfs_get(sets, "co14599a.rom", 0xbc533f0c, 0x1000, &len);
    }
    p = dspc_rom_find(sets, "co12399b.rom|co12399a.rom", (const uint32_t[]){0x6a5d766e}, 1, 0x800);
    if (!a || !b || !p) {
        free(a);
        free(b);
        free(p);
        romfs_missing(sets, "Atari 800 OS (co12399b, co12499b, co14599b)");
        return false;
    }
    memcpy(S.os, p, 0x800);
    memcpy(S.os + 0x800, a, 0x1000);
    memcpy(S.os + 0x1800, b, 0x1000);
    free(a);
    free(b);
    free(p);
#else
    const bool xe = DSPC_MODEL == 2;
    p = romfs_get(sets, xe ? "c300717.rom" : "co61598b.rom", xe ? 0x29f133f7u : 0x1f9cd270u, 0x4000, &len);
    if (!p) {
        romfs_missing(sets, xe ? "c300717.rom" : "co61598b.rom");
        return false;
    }
    memcpy(S.os, p, 0x4000);
    free(p);
    p = romfs_get(sets, xe ? "co24947a.rom" : "co60302a.rom", xe ? 0x7d684184u : 0xf0202fb3u, 0x2000, &len);
    if (p) {
        memcpy(S.basic, p, 0x2000);
        S.has_basic = true;
        free(p);
    }
#endif
    romfs_release();
    return true;
}

bool dspc_init(void)
{
    memset(&S, 0, sizeof S);
    S.cpu.read = c_read;
    S.cpu.write = c_write;
    S.an.g = &S.gt;
    S.an.mem = antic_mem;
    S.an.nmi = an_nmi;
    S.gt.console = gt_console;
    S.gt.trigger = gt_trigger;
    pokey_init(&S.pk);
    S.pk.irq = pk_irq;
    if (!load_roms()) return false;
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    S.porta = S.portb = 0xff;
    S.pactl = S.pbctl = S.porta_dir = S.portb_dir = 0;
    S.os_enabled = true;
    S.basic_enabled = S.selftest_enabled = false;
    S.stick[0] = S.stick[1] = 0x0f;
    S.trig[0] = S.trig[1] = 1;
    S.console = 0x07;
    S.boot_option_frames = 120;
    S.irq_line = false;
    S.naudio = 0;
    S.audio_acc = 0;
    for (int i = 0; i < ANTIC_W * ANTIC_H; i++) S.fb[i] = 0xff000000u;
    gtia_reset(&S.gt);
    antic_reset(&S.an);
    pokey_reset(&S.pk);
    m65xx_reset(&S.cpu);
}

void dspc_set_inputs(const dspc_inputs *in)
{
    static const struct { uint8_t key, code; } kKeys[] = {
        {DK_L, 0x00}, {DK_J, 0x01}, {DK_SEMICOLON, 0x02}, {DK_K, 0x05}, {DK_PLUS, 0x06},
        {DK_O, 0x08}, {DK_P, 0x0a}, {DK_U, 0x0b}, {DK_ENTER, 0x0c}, {DK_I, 0x0d}, {DK_MINUS, 0x0e},
        {DK_EQUALS, 0x0f}, {DK_V, 0x10}, {DK_C, 0x12}, {DK_B, 0x15}, {DK_X, 0x16}, {DK_Z, 0x17},
        {DK_4, 0x18}, {DK_3, 0x1a}, {DK_6, 0x1b}, {DK_ESCAPE, 0x1c}, {DK_5, 0x1d}, {DK_2, 0x1e},
        {DK_1, 0x1f}, {DK_COMMA, 0x20}, {DK_SPACE, 0x21}, {DK_PERIOD, 0x22}, {DK_N, 0x23},
        {DK_M, 0x25}, {DK_SLASH, 0x26}, {DK_R, 0x28}, {DK_E, 0x2a}, {DK_Y, 0x2b}, {DK_TAB, 0x2c},
        {DK_T, 0x2d}, {DK_W, 0x2e}, {DK_Q, 0x2f}, {DK_9, 0x30}, {DK_0, 0x32}, {DK_7, 0x33},
        {DK_BACKSPACE, 0x34}, {DK_8, 0x35}, {DK_F, 0x38}, {DK_H, 0x39}, {DK_D, 0x3a},
        {DK_CAPSLOCK, 0x3c}, {DK_G, 0x3d}, {DK_S, 0x3e}, {DK_A, 0x3f},
    };
    const bool *k = in->keys;
    uint8_t s = 0x0f;
    if (k[DK_UP] || in->p1.up) s &= (uint8_t)~0x01;
    if (k[DK_DOWN] || in->p1.down) s &= (uint8_t)~0x02;
    if (k[DK_LEFT] || in->p1.left) s &= (uint8_t)~0x04;
    if (k[DK_RIGHT] || in->p1.right) s &= (uint8_t)~0x08;
    S.stick[0] = s;
    S.trig[0] = (k[DK_LCTRL] || in->p1.b1) ? 0 : 1;
    s = 0x0f;
    if (in->p2.up) s &= (uint8_t)~0x01;
    if (in->p2.down) s &= (uint8_t)~0x02;
    if (in->p2.left) s &= (uint8_t)~0x04;
    if (in->p2.right) s &= (uint8_t)~0x08;
    S.stick[1] = s;
    S.trig[1] = in->p2.b1 ? 0 : 1;
    S.console = 0x07;
    if (k[DK_F2]) S.console &= (uint8_t)~0x01;
    if (k[DK_F3]) S.console &= (uint8_t)~0x02;
    if (k[DK_F4]) S.console &= (uint8_t)~0x04;
    const bool shift = k[DK_LSHIFT] || k[DK_RSHIFT], ctrl = k[DK_RCTRL];
    for (size_t i = 0; i < sizeof kKeys / sizeof kKeys[0]; i++) {
        if (!k[kKeys[i].key]) continue;
        uint8_t code = kKeys[i].code;
        if (shift) code |= 0x40;
        if (ctrl) code |= 0x80;
        pokey_set_key(&S.pk, code, true, shift);
        return;
    }
    pokey_set_key(&S.pk, 0, false, shift);
}

int dspc_run_frame(const int16_t **audio)
{
    static uint32_t offscreen[ANTIC_W];
    if (S.boot_option_frames > 0) --S.boot_option_frames;
    S.naudio = 0;
    antic_begin_frame(&S.an);
    for (int line = 0; line < ANTIC_LINES; ++line) {
        const int fb_line = line - ANTIC_FIRST_LINE;
        uint32_t *dst = fb_line >= 0 && fb_line < ANTIC_H ? S.fb + (size_t)fb_line * ANTIC_W : offscreen;
        const int stolen = antic_scanline(&S.an, line, dst);
        S.an.wsync = false;
        const int budget = ANTIC_CPL - stolen;
        int done = 0;
        while (done < budget) {
            if (S.an.wsync) {
                S.an.wsync = false;
                break;
            }
            const int ran = m65xx_run(&S.cpu, 1);
            if (ran <= 0) break;
            done += ran;
        }
        pokey_run(&S.pk, ANTIC_CPL);
        S.audio_acc += (int64_t)ANTIC_CPL * RATE;
        while (S.audio_acc >= CLOCK) {
            S.audio_acc -= CLOCK;
            int32_t v = pokey_update(&S.pk);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            if (S.naudio < 2048) S.audio[S.naudio++] = (int16_t)v;
        }
        if (S.irq_line) {
            S.irq_line = false;
            m65xx_set_irq(&S.cpu, M65_CLEAR);
        }
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint32_t *dspc_frame(void) { return S.fb; }

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    (void)name;
    if (size < 16 || data[0] != 0x96 || data[1] != 0x02) return "not an Atari .ATR disk image";
    const int ss = data[4] | (data[5] << 8);
    if (ss != 128 && ss != 256) return "unsupported ATR sector size";
    uint8_t *p = (uint8_t *)malloc(size);
    if (!p) return "out of memory";
    memcpy(p, data, size);
    free(S.disk);
    S.disk = p;
    S.disk_n = size;
    S.sector_size = ss;
    const uint32_t bytes = (uint32_t)(data[2] | (data[3] << 8) | (data[6] << 16)) * 16;
    S.sectors = ss == 128 ? (int)(bytes / 128) : (int)(3 + (bytes > 384 ? (bytes - 384) / 256 : 0));
    S.disk_loaded = true;
    dspc_reset();          // cold start with the disk in (as dsp-cpp's frontend does on media load)
    return NULL;
}
