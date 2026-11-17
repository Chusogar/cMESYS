// ws_core.c - Bandai WonderSwan (1999) / WonderSwan Color (2000): a C11
// migration of dsp-cpp's drivers/consoles/wonderswan. The V30MZ (with the
// NEC V30 core, cpu/necv30), the ASWAN / SPHINX SoC: 16 KB (64 KB) of
// internal RAM, the two tile layers, sprites and windows, the mono pools /
// the Color's palettes, timers, IRQs, the Color's DMA and the EEPROMs; the
// sound (sound/wswan_apu, at 24 kHz). Cartridges banked through $C0-$C3;
// no boot ROM: the I/O as it leaves it and the cartridge's footer at
// FFFF:0000 (as dsp-cpp does with a cartridge). Color: a .wsc file or the
// footer's flag. X1-X4 on the pad, Y1-Y4 on player 2's pad (or W A S D).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/necv30/nec_v30.h"
#include "../../../sound/wswan_apu.h"

#define CLOCK 3072000
#define W 224
#define H 144
#define LINE_CYCLES 256
#define DEFAULT_VTOTAL 158
#define MAX_CART (16 * 1024 * 1024)

static const char *const kExts[] = { ".ws", ".wsc", ".pc2", NULL };
const cons_info cons_machine = {
    "wonderswan", "Bandai WonderSwan / Color", "Bandai, 1999", kExts, "WS WSC", W, H,
    (double)CLOCK / LINE_CYCLES / (DEFAULT_VTOTAL + 1), WSWAN_APU_RATE, { "A", "B", NULL, NULL }, "START", NULL,
    CONS_KEYPAD, 2, "W A S D: Y1-Y4",
};

static const uint8_t kStartIo[0xc8] = {
    0x00, 0x00, 0x9d, 0xbb, 0x00, 0x00, 0x00, 0x26, 0xfe, 0xde, 0xf9, 0xfb, 0xdb, 0xd7, 0x7f, 0xf5,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x9e, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x99, 0xfd, 0xb7, 0xdf,
    0x30, 0x57, 0x75, 0x76, 0x15, 0x73, 0x77, 0x77, 0x20, 0x75, 0x50, 0x36, 0x70, 0x67, 0x50, 0x77,
    0x57, 0x54, 0x75, 0x77, 0x75, 0x17, 0x37, 0x73, 0x50, 0x57, 0x60, 0x77, 0x70, 0x77, 0x10, 0x73,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xdb, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x00, 0x83, 0x00,
    0x2f, 0x3f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};

enum { IRQ_SERIAL_SEND, IRQ_INPUT, IRQ_CART, IRQ_SERIAL_RECV, IRQ_LINE, IRQ_VTIMER, IRQ_VBLANK, IRQ_HTIMER };

typedef struct { bool enable, repeat; uint16_t frequency, counter; } ws_timer;
typedef struct { bool enable; uint8_t map_base, hscroll, vscroll; } ws_layer;
typedef struct { bool enable, invert; uint8_t x0, y0, x1, y1; } ws_window;
typedef struct { bool enable; uint8_t oam_base, first, count; ws_window window; } ws_sprites;

static struct {
    nec_v30 cpu;
    wswan_apu apu;
    bool color;
    uint8_t *iram;               // 64 KB
    uint8_t *rom;
    uint32_t rom_size;
    uint8_t *cart_ram;
    uint32_t cart_ram_size;
    uint8_t internal_eeprom[2048];
    uint32_t internal_eeprom_size;
    uint8_t *cart_eeprom;
    uint32_t cart_eeprom_size;
    uint8_t rom_bank2, sram_bank, rom_bank0, rom_bank1;
    bool cart_enable, rom_width, rom_wait;
    uint8_t disp_mode, irq_base, irq_enable, irq_status, nmi_control, keypad_matrix;
    uint16_t buttons;
    ws_layer screen1, screen2;
    ws_sprites sprite;
    ws_window screen2_window;
    bool lcd_enable, lcd_contrast;
    uint8_t backdrop, lcd_icons, vtotal, vsync_line, vcompare;
    int vcounter;
    bool field;
    uint8_t mono_pool[8], mono_pal[16][4];
    uint32_t oam_cache[128];
    int oam_count;
    ws_timer htimer, vtimer;
    uint32_t dma_source;
    uint16_t dma_dest, dma_length;
    uint8_t dma_control;
    uint16_t eep_data[2], eep_cmd[2];
    uint8_t eep_ctrl[2];
    bool eep_protect;
    uint16_t *fb;
    int debt;
    uint64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    unsigned pads[2];
    bool keys_held[4];
    bool loaded;
} S;

static inline uint8_t display_mode(void) { return (uint8_t)((S.disp_mode >> 5) & 7); }
static inline bool planar(void) { return display_mode() != 7; }
static inline bool packed(void) { return display_mode() == 7; }
static inline int depth(void) { return ((display_mode() >> 1) & 3) != 3 ? 2 : 4; }
static inline bool grayscale(void) { return !(display_mode() & 4); }

static bool timer_step(ws_timer *t)
{
    const uint16_t next = (uint16_t)(t->counter - 1);
    if (t->enable && t->counter) {
        t->counter = next;
        if (t->repeat && t->counter == 0) t->counter = t->frequency;
    }
    return next == 0;
}

static inline bool win_inside(const ws_window *w, uint8_t x, uint8_t y)
{
    const bool xin = (x >= w->x0 && x <= w->x1) || (x >= w->x1 && x <= w->x0);
    const bool yin = (y >= w->y0 && y <= w->y1) || (y >= w->y1 && y <= w->y0);
    return xin && yin;
}

static inline bool win_outside(const ws_window *w, uint8_t x, uint8_t y) { return x < w->x0 || x > w->x1 || y < w->y0 || y > w->y1; }

// ---- memory ----
static inline uint8_t read_iram(uint16_t a)
{
    const uint32_t size = S.color ? 0x10000u : 0x4000u;
    if (a >= size) return S.color ? 0x00 : 0x90;
    return S.iram[a];
}

static inline uint16_t read_iram16(uint16_t a) { return (uint16_t)(read_iram(a) | (read_iram((uint16_t)(a + 1)) << 8)); }
static inline uint32_t read_iram32(uint16_t a) { return read_iram16(a) | ((uint32_t)read_iram16((uint16_t)(a + 2)) << 16); }

static uint8_t read_cart_rom(uint32_t a)
{
    if (!S.rom) return 0xff;
    const uint32_t bank = (a >> 16) & 0xf, off = a & 0xffff, mask = (S.rom_size >> 16) - 1;
    uint32_t linear;
    if (bank == 2) linear = ((uint32_t)S.rom_bank0 & mask) << 16 | off;
    else if (bank == 3) linear = ((uint32_t)S.rom_bank1 & mask) << 16 | off;
    else linear = (((((uint32_t)(S.rom_bank2 & 0x0f) << 4) | bank) & mask) << 16) | off;
    return S.rom[linear & (S.rom_size - 1)];
}

static uint8_t rd(void *u, uint32_t a)
{
    (void)u;
    a &= 0xfffff;
    const uint32_t bank = a >> 16;
    if (bank == 0) {
        if (!S.color && (a & 0xc000)) return 0x90;
        return read_iram((uint16_t)a);
    }
    if (bank == 1) {
        if (!S.cart_ram) return 0xff;
        return S.cart_ram[(((uint32_t)S.sram_bank << 16) | (a & 0xffff)) & (S.cart_ram_size - 1)];
    }
    return read_cart_rom(a);
}

static void wr(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0xfffff;
    const uint32_t bank = a >> 16;
    if (bank == 0) {
        if (!S.color && (a & 0xc000)) return;
        if (a < (S.color ? 0x10000u : 0x4000u)) S.iram[a] = v;
        return;
    }
    if (bank == 1 && S.cart_ram) S.cart_ram[(((uint32_t)S.sram_bank << 16) | (a & 0xffff)) & (S.cart_ram_size - 1)] = v;
}

// ---- IRQs ----
static void poll_irq(void)
{
    const uint8_t pending = S.irq_status & S.irq_enable;
    if (!pending) {
        nec_v30_set_irq(&S.cpu, 0, 0xff);
        return;
    }
    for (int i = 0; i < 8; i++)
        if (pending & (1 << i)) {
            nec_v30_set_irq(&S.cpu, 2, (uint8_t)((S.irq_base & ~7) | i));
            return;
        }
}

static void raise_irq(int irq)
{
    if (!(S.irq_enable & (1u << irq))) return;
    S.irq_status |= (uint8_t)(1u << irq);
    poll_irq();
}

static uint8_t read_keypad(void)
{
    uint8_t l = 0;
    if (S.keypad_matrix & 0x4) l |= (uint8_t)(((S.buttons >> 8) << 1) & 0x0f);
    if (S.keypad_matrix & 0x2) l |= (uint8_t)(S.buttons & 0x0f);
    if (S.keypad_matrix & 0x1) l |= (uint8_t)((S.buttons >> 4) & 0x0f);
    return l;
}

static void run_gdma(void)
{
    if (!S.color) return;
    while (S.dma_length) {
        const uint8_t lo = rd(NULL, S.dma_source), hi = rd(NULL, S.dma_source + 1);
        wr(NULL, S.dma_dest, lo);
        wr(NULL, (uint32_t)S.dma_dest + 1, hi);
        if (S.dma_control & 0x40) {
            S.dma_source -= 2;
            S.dma_dest -= 2;
        } else {
            S.dma_source += 2;
            S.dma_dest += 2;
        }
        S.dma_source &= 0xfffff;
        S.dma_length -= 2;
    }
    S.dma_control &= 0x7f;
}

// ---- EEPROMs (a minimal M93LCx6) ----
static uint8_t eeprom_status(bool internal)
{
    const int i = internal ? 0 : 1;
    uint8_t d = 0x02 | (uint8_t)((S.eep_ctrl[i] & 0xf) << 4);
    if (internal && S.eep_protect) d |= 0x80;
    return d;
}

static void eeprom_ctrl(bool internal, uint8_t v)
{
    const int i = internal ? 0 : 1;
    uint8_t *store = internal ? S.internal_eeprom : S.cart_eeprom;
    const uint32_t size = internal ? S.internal_eeprom_size : S.cart_eeprom_size;
    if (!store || !size) return;
    const uint8_t ctrl = (v >> 4) & 0xf;
    S.eep_ctrl[i] = ctrl;
    const uint32_t addr = S.eep_cmd[i] & (size / 2 - 1);
    if (ctrl == 1) {
        if (internal && S.eep_protect && addr >= 0x30) S.eep_data[i] = 0xffff;
        else S.eep_data[i] = (uint16_t)(store[addr * 2] | (store[addr * 2 + 1] << 8));
    } else if (ctrl == 2) {
        if (!(internal && S.eep_protect && addr >= 0x30)) {
            store[addr * 2] = (uint8_t)S.eep_data[i];
            store[addr * 2 + 1] = (uint8_t)(S.eep_data[i] >> 8);
        }
    } else if (ctrl == 8 && internal) {
        S.eep_protect = true;
    }
    S.eep_ctrl[i] = 0;
}

static inline bool apu_port(uint16_t p)
{
    return (p >= 0x80 && p <= 0x9f) || p == 0x4a || p == 0x4b || p == 0x4c || p == 0x4e || p == 0x4f || p == 0x50 ||
           p == 0x52 || p == 0x6a || p == 0x6b;
}

static bool decode_port(uint16_t *port)
{
    const uint16_t p = *port;
    if (!(p & 0x100) && (p < 0x100 || (p & 0xff) < 0xb8)) *port = p & 0xff;
    else if (p >= 0xc0 && p <= 0xff) *port = p & 0xff;
    else return false;
    return true;
}

static uint8_t io_in(void *u, uint16_t port)
{
    (void)u;
    if (!decode_port(&port)) return S.color ? 0x00 : 0x90;
    const uint8_t cm = S.color ? 0xf : 0x7;
    switch (port) {
    case 0x00:
        return (uint8_t)(S.screen1.enable | (S.screen2.enable << 1) | (S.sprite.enable << 2) | (S.sprite.window.enable << 3) |
                         (S.screen2_window.invert << 4) | (S.screen2_window.enable << 5));
    case 0x01: return S.backdrop;
    case 0x02: return (uint8_t)S.vcounter;
    case 0x03: return S.vcompare;
    case 0x04: return S.sprite.oam_base & (S.color ? 0x3f : 0x1f);
    case 0x05: return S.sprite.first;
    case 0x06: return S.sprite.count;
    case 0x07: return (uint8_t)((S.screen1.map_base & cm) | ((S.screen2.map_base & cm) << 4));
    case 0x08: return S.screen2_window.x0;
    case 0x09: return S.screen2_window.y0;
    case 0x0a: return S.screen2_window.x1;
    case 0x0b: return S.screen2_window.y1;
    case 0x0c: return S.sprite.window.x0;
    case 0x0d: return S.sprite.window.y0;
    case 0x0e: return S.sprite.window.x1;
    case 0x0f: return S.sprite.window.y1;
    case 0x10: return S.screen1.hscroll;
    case 0x11: return S.screen1.vscroll;
    case 0x12: return S.screen2.hscroll;
    case 0x13: return S.screen2.vscroll;
    case 0x14: return (uint8_t)(S.lcd_enable | (S.lcd_contrast << 1));
    case 0x15: return S.lcd_icons;
    case 0x16: return S.vtotal;
    case 0x17: return S.vsync_line;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
        const int i = (port - 0x1c) * 2;
        return (uint8_t)((S.mono_pool[i] & 0xf) | ((S.mono_pool[i + 1] & 0xf) << 4));
    }
    case 0x60: return S.disp_mode & 0xe0;
    case 0xa0: return (uint8_t)(S.cart_enable | (S.color << 1) | (S.rom_width << 2) | (S.rom_wait << 3) | 0x80);
    case 0xa2: return (uint8_t)(S.htimer.enable | (S.htimer.repeat << 1) | (S.vtimer.enable << 2) | (S.vtimer.repeat << 3));
    case 0xa4: return (uint8_t)S.htimer.frequency;
    case 0xa5: return (uint8_t)(S.htimer.frequency >> 8);
    case 0xa6: return (uint8_t)S.vtimer.frequency;
    case 0xa7: return (uint8_t)(S.vtimer.frequency >> 8);
    case 0xa8: return (uint8_t)S.htimer.counter;
    case 0xa9: return (uint8_t)(S.htimer.counter >> 8);
    case 0xaa: return (uint8_t)S.vtimer.counter;
    case 0xab: return (uint8_t)(S.vtimer.counter >> 8);
    default: break;
    }
    if (port >= 0x20 && port <= 0x3f) {
        const int pal = (port - 0x20) >> 1, pair = (port & 1) << 1;
        return (uint8_t)((S.mono_pal[pal][pair] & 7) | ((S.mono_pal[pal][pair + 1] & 7) << 4));
    }
    if (port <= 0x3f || (port >= 0xa0 && port <= 0xab)) goto done;
    if (S.color && port >= 0x40 && port <= 0x48) {
        switch (port) {
        case 0x40: return (uint8_t)S.dma_source;
        case 0x41: return (uint8_t)(S.dma_source >> 8);
        case 0x42: return (uint8_t)(S.dma_source >> 16);
        case 0x44: return (uint8_t)S.dma_dest;
        case 0x45: return (uint8_t)(S.dma_dest >> 8);
        case 0x46: return (uint8_t)S.dma_length;
        case 0x47: return (uint8_t)(S.dma_length >> 8);
        case 0x48: return S.dma_control;
        default: break;
        }
    }
    if (apu_port(port)) return wswan_apu_read(&S.apu, port);
    switch (port) {
    case 0xb0: return S.irq_base;
    case 0xb2: return S.irq_enable;
    case 0xb4: return S.irq_status;
    case 0xb5: return (uint8_t)((S.keypad_matrix << 4) | read_keypad());
    case 0xb6: {
        const uint8_t pending = S.irq_status & S.irq_enable;
        for (int i = 0; i < 8; i++)
            if (pending & (1 << i)) return (uint8_t)(1 << i);
        return 0;
    }
    case 0xb7: return S.nmi_control;
    case 0xba: return (uint8_t)S.eep_data[0];
    case 0xbb: return (uint8_t)(S.eep_data[0] >> 8);
    case 0xbc: return (uint8_t)S.eep_cmd[0];
    case 0xbd: return (uint8_t)(S.eep_cmd[0] >> 8);
    case 0xbe: return eeprom_status(true);
    case 0xc0: return (uint8_t)(S.rom_bank2 | 0x20);
    case 0xc1: return S.sram_bank;
    case 0xc2: return S.rom_bank0;
    case 0xc3: return S.rom_bank1;
    case 0xc4: return (uint8_t)S.eep_data[1];
    case 0xc5: return (uint8_t)(S.eep_data[1] >> 8);
    case 0xc6: return (uint8_t)S.eep_cmd[1];
    case 0xc7: return (uint8_t)(S.eep_cmd[1] >> 8);
    case 0xc8: return eeprom_status(false);
    default: break;
    }
done:
    return S.color ? 0x00 : 0x90;
}

static void io_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    if (!decode_port(&port)) return;
    switch (port) {
    case 0x00:
        S.screen1.enable = v & 1;
        S.screen2.enable = (v & 2) != 0;
        S.sprite.enable = (v & 4) != 0;
        S.sprite.window.enable = (v & 8) != 0;
        S.screen2_window.invert = (v & 0x10) != 0;
        S.screen2_window.enable = (v & 0x20) != 0;
        return;
    case 0x01: S.backdrop = v; return;
    case 0x03: S.vcompare = v; return;
    case 0x04: S.sprite.oam_base = v & 0x3f; return;
    case 0x05: S.sprite.first = v & 0x7f; return;
    case 0x06: S.sprite.count = v; return;
    case 0x07: S.screen1.map_base = v & 0x0f; S.screen2.map_base = (v >> 4) & 0x0f; return;
    case 0x08: S.screen2_window.x0 = v; return;
    case 0x09: S.screen2_window.y0 = v; return;
    case 0x0a: S.screen2_window.x1 = v; return;
    case 0x0b: S.screen2_window.y1 = v; return;
    case 0x0c: S.sprite.window.x0 = v; return;
    case 0x0d: S.sprite.window.y0 = v; return;
    case 0x0e: S.sprite.window.x1 = v; return;
    case 0x0f: S.sprite.window.y1 = v; return;
    case 0x10: S.screen1.hscroll = v; return;
    case 0x11: S.screen1.vscroll = v; return;
    case 0x12: S.screen2.hscroll = v; return;
    case 0x13: S.screen2.vscroll = v; return;
    case 0x14: S.lcd_enable = v & 1; S.lcd_contrast = (v & 2) != 0; return;
    case 0x15: S.lcd_icons = v; return;
    case 0x16: S.vtotal = v; return;
    case 0x17: S.vsync_line = v; return;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
        const int i = (port - 0x1c) * 2;
        S.mono_pool[i] = v & 0xf;
        S.mono_pool[i + 1] = (v >> 4) & 0xf;
        return;
    }
    case 0x60: S.disp_mode = v & 0xe0; return;
    case 0xa0:
        S.cart_enable = S.cart_enable || (v & 1);
        S.rom_width = (v & 4) != 0;
        S.rom_wait = (v & 8) != 0;
        return;
    case 0xa2:
        S.htimer.enable = v & 1;
        S.htimer.repeat = (v & 2) != 0;
        S.vtimer.enable = (v & 4) != 0;
        S.vtimer.repeat = (v & 8) != 0;
        return;
    case 0xa4: S.htimer.frequency = (uint16_t)((S.htimer.frequency & 0xff00) | v); S.htimer.counter = S.htimer.frequency; return;
    case 0xa5: S.htimer.frequency = (uint16_t)((S.htimer.frequency & 0x00ff) | (v << 8)); S.htimer.counter = S.htimer.frequency; return;
    case 0xa6: S.vtimer.frequency = (uint16_t)((S.vtimer.frequency & 0xff00) | v); S.vtimer.counter = S.vtimer.frequency; return;
    case 0xa7: S.vtimer.frequency = (uint16_t)((S.vtimer.frequency & 0x00ff) | (v << 8)); S.vtimer.counter = S.vtimer.frequency; return;
    default: break;
    }
    if (port >= 0x20 && port <= 0x3f) {
        const int pal = (port - 0x20) >> 1, pair = (port & 1) << 1;
        if ((port & 0x9) != 0x8) S.mono_pal[pal][pair] = v & 7;
        S.mono_pal[pal][pair + 1] = (v >> 4) & 7;
        return;
    }
    if (port <= 0x3f || (port >= 0xa0 && port <= 0xab)) return;
    if (S.color && port >= 0x40 && port <= 0x48) {
        switch (port) {
        case 0x40: S.dma_source = (S.dma_source & 0xffff00) | (v & ~1u); return;
        case 0x41: S.dma_source = (S.dma_source & 0xff00ff) | ((uint32_t)v << 8); return;
        case 0x42: S.dma_source = (S.dma_source & 0x00ffff) | (((uint32_t)v & 0x0f) << 16); return;
        case 0x44: S.dma_dest = (uint16_t)((S.dma_dest & 0xff00) | (v & ~1u)); return;
        case 0x45: S.dma_dest = (uint16_t)((S.dma_dest & 0x00ff) | (v << 8)); return;
        case 0x46: S.dma_length = (uint16_t)((S.dma_length & 0xff00) | (v & ~1u)); return;
        case 0x47: S.dma_length = (uint16_t)((S.dma_length & 0x00ff) | (v << 8)); return;
        case 0x48:
            S.dma_control = v & ~0x3f;
            if (S.dma_control & 0x80) run_gdma();
            return;
        default: break;
        }
    }
    if (apu_port(port)) {
        wswan_apu_write(&S.apu, port, v);
        return;
    }
    switch (port) {
    case 0xb0: S.irq_base = v & ~7; poll_irq(); return;
    case 0xb2: S.irq_enable = v; poll_irq(); return;
    case 0xb5: S.keypad_matrix = (v >> 4) & 7; return;
    case 0xb6: S.irq_status &= (uint8_t)~v; poll_irq(); return;
    case 0xb7: S.nmi_control = v; return;
    case 0xba: S.eep_data[0] = (uint16_t)((S.eep_data[0] & 0xff00) | v); return;
    case 0xbb: S.eep_data[0] = (uint16_t)((S.eep_data[0] & 0x00ff) | (v << 8)); return;
    case 0xbc: S.eep_cmd[0] = (uint16_t)((S.eep_cmd[0] & 0xff00) | v); return;
    case 0xbd: S.eep_cmd[0] = (uint16_t)((S.eep_cmd[0] & 0x00ff) | (v << 8)); return;
    case 0xbe: eeprom_ctrl(true, v); return;
    case 0xc0: S.rom_bank2 = v; return;
    case 0xc1: S.sram_bank = v; return;
    case 0xc2: S.rom_bank0 = v; return;
    case 0xc3: S.rom_bank1 = v; return;
    case 0xc4: S.eep_data[1] = (uint16_t)((S.eep_data[1] & 0xff00) | v); return;
    case 0xc5: S.eep_data[1] = (uint16_t)((S.eep_data[1] & 0x00ff) | (v << 8)); return;
    case 0xc6: S.eep_cmd[1] = (uint16_t)((S.eep_cmd[1] & 0xff00) | v); return;
    case 0xc7: S.eep_cmd[1] = (uint16_t)((S.eep_cmd[1] & 0x00ff) | (v << 8)); return;
    case 0xc8: eeprom_ctrl(false, v); return;
    default: break;
    }
}

// ---- the picture ----
static uint8_t fetch_tile(uint16_t tile, uint8_t x, uint8_t y)
{
    tile &= grayscale() ? 0x1ff : 0x3ff;
    x &= 7;
    y &= 7;
    uint8_t c = 0;
    if (planar() && depth() == 2) {
        const uint16_t d = read_iram16((uint16_t)(0x2000 + (tile << 4) + (y << 1)));
        if (d & (0x80 >> x)) c |= 1;
        if (d & (0x8000 >> x)) c |= 2;
    } else if (planar() && depth() == 4) {
        const uint32_t d = read_iram32((uint16_t)(0x4000 + (tile << 5) + (y << 2)));
        if (d & (0x80u >> x)) c |= 1;
        if (d & (0x8000u >> x)) c |= 2;
        if (d & (0x800000u >> x)) c |= 4;
        if (d & (0x80000000u >> x)) c |= 8;
    } else if (packed() && depth() == 2) {
        const uint8_t d = read_iram((uint16_t)(0x2000 + (tile << 4) + (y << 1) + (x >> 2)));
        c = (uint8_t)((d >> (6 - ((x & 3) << 1))) & 3);
    } else if (packed() && depth() == 4) {
        const uint8_t d = read_iram((uint16_t)(0x4000 + (tile << 5) + (y << 2) + (x >> 1)));
        c = (uint8_t)((d >> (4 - ((x & 1) << 2))) & 0xf);
    }
    return c;
}

static inline bool opaque(uint8_t palette, uint8_t c) { return c || (depth() == 2 && !(palette & 4)); }

static uint16_t backdrop_color(uint8_t c)
{
    if (grayscale()) {
        const uint8_t l = (uint8_t)(15 - S.mono_pool[c & 7]);
        return (uint16_t)(l | (l << 4) | (l << 8));
    }
    return read_iram16((uint16_t)(0xfe00 + (c << 1))) & 0x0fff;
}

static uint16_t palette_color(uint8_t palette, uint8_t c)
{
    if (grayscale()) {
        const uint8_t pool = S.mono_pal[palette & 0xf][c & 3] & 7;
        const uint8_t l = (uint8_t)(15 - S.mono_pool[pool]);
        return (uint16_t)(l | (l << 4) | (l << 8));
    }
    return read_iram16((uint16_t)(0xfe00 + (palette << 5) + (c << 1))) & 0x0fff;
}

static uint16_t rgb12(uint16_t c)
{
    uint32_t b = c & 0xf, g = (c >> 4) & 0xf, r = (c >> 8) & 0xf;
    if (S.color && !grayscale()) {
        uint32_t R = r * 26 + g * 4 + b * 2, G = g * 24 + b * 8, B = r * 6 + g * 4 + b * 22;
        if (R > 480) R = 480;
        if (G > 480) G = 480;
        if (B > 480) B = 480;
        r = R * 255 / 480;
        g = G * 255 / 480;
        b = B * 255 / 480;
    } else {
        r = r * 255 / 15;
        g = g * 255 / 15;
        b = b * 255 / 15;
    }
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static bool layer_pixel(const ws_layer *l, uint8_t x, uint8_t y, uint16_t *out)
{
    if (!l->enable) return false;
    const uint8_t lx = (uint8_t)(x + l->hscroll), ly = (uint8_t)(y + l->vscroll);
    const uint16_t a = (uint16_t)(((lx >> 3) << 1) | ((ly >> 3) << 6) | (l->map_base << 11));
    const uint16_t at = read_iram16(a);
    const uint16_t tile = (uint16_t)((at & 0x1ff) | ((at >> 13) & 1) << 9);
    const uint8_t pal = (at >> 9) & 0xf, hf = (at & 0x4000) ? 7 : 0, vf = (at & 0x8000) ? 7 : 0;
    const uint8_t c = fetch_tile(tile, (uint8_t)((lx & 7) ^ hf), (uint8_t)((ly & 7) ^ vf));
    if (!opaque(pal, c)) return false;
    *out = palette_color(pal, c);
    return true;
}

static void render_scanline(int y)
{
    const ws_layer s1 = S.screen1, s2 = S.screen2;
    const ws_sprites sp = S.sprite;
    const ws_window s2w = S.screen2_window;
    const bool lcd_on = S.lcd_enable, contrast = S.lcd_contrast;
    const uint8_t backdrop = S.backdrop;
    uint32_t objects[32];
    int nobj = 0;
    if (sp.enable)
        for (int i = 0; i < S.oam_count && nobj < 32; i++) {
            const uint32_t attr = S.oam_cache[i];
            if ((uint8_t)(y - (uint8_t)(attr >> 16)) > 7) continue;
            objects[nobj++] = attr;
        }
    uint16_t *out = S.fb + y * W;
    for (int x = 0; x < W; x++) {
        uint16_t color = backdrop_color(backdrop);
        if (lcd_on) {
            uint16_t c1 = 0, c2 = 0, cs = 0;
            const bool v1 = layer_pixel(&s1, (uint8_t)x, (uint8_t)y, &c1);
            bool v2 = layer_pixel(&s2, (uint8_t)x, (uint8_t)y, &c2), vs = false;
            if (s2w.enable && (s2w.invert ? win_inside(&s2w, (uint8_t)x, (uint8_t)y) : win_outside(&s2w, (uint8_t)x, (uint8_t)y)))
                v2 = false;
            if (sp.enable) {
                const bool outside = win_outside(&sp.window, (uint8_t)x, (uint8_t)y);
                for (int i = 0; i < nobj; i++) {
                    const uint32_t attr = objects[i];
                    const uint16_t tile = attr & 0x1ff;
                    const uint8_t pal = (uint8_t)(((attr >> 9) & 7) | 8);
                    const bool region = (attr >> 12) & 1, prio = (attr >> 13) & 1;
                    const uint8_t hf = (attr & 0x4000) ? 7 : 0, vf = (attr & 0x8000) ? 7 : 0;
                    const uint8_t vo = (uint8_t)(attr >> 16), ho = (uint8_t)(attr >> 24);
                    if (sp.window.enable && region != outside) continue;
                    if ((uint8_t)(x - ho) > 7) continue;
                    const uint8_t px = fetch_tile(tile, (uint8_t)((x - ho) ^ hf), (uint8_t)((y - vo) ^ vf));
                    if (!opaque(pal, px)) continue;
                    if (!prio && v2) continue;
                    vs = true;
                    cs = palette_color(pal, px);
                    break;
                }
            }
            if (v1) color = c1;
            if (v2) color = c2;
            if (vs) color = cs;
            if (S.color && contrast) {
                uint32_t b = (color & 0xf) * 3 / 2, g = ((color >> 4) & 0xf) * 3 / 2, r = ((color >> 8) & 0xf) * 3 / 2;
                if (b > 15) b = 15;
                if (g > 15) g = 15;
                if (r > 15) r = 15;
                color = (uint16_t)(b | (g << 4) | (r << 8));
            }
        }
        out[x] = rgb12(color);
    }
}

// ---- the console ----
static uint8_t apu_mem(void *u, uint32_t a) { return rd(u, a); }

bool cons_init(void)
{
    S.iram = (uint8_t *)calloc(1, 0x10000);
    S.fb = (uint16_t *)calloc(W * H, sizeof(uint16_t));
    if (!S.iram || !S.fb) return false;
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.in8 = io_in;
    S.cpu.out8 = io_out;
    nec_v30_init(&S.cpu, NEC_V30);
    S.apu.read_mem = apu_mem;
    S.color = true;
    S.internal_eeprom_size = 2048;
    cons_reset();
    return true;
}

void cons_reset(void)
{
    nec_v30_reset(&S.cpu);
    memset(S.iram, 0, 0x10000);
    static const uint8_t sig[8] = { 0x41, 0x5f, 0x43, 0x31, 0x6e, 0x5f, 0x63, 0x31 };
    memcpy(S.iram + 0x75ac, sig, 8);
    S.rom_bank2 = S.sram_bank = S.rom_bank0 = S.rom_bank1 = 0xff;
    S.cart_enable = false;
    S.rom_width = true;
    S.rom_wait = false;
    S.disp_mode = S.irq_base = S.irq_enable = S.irq_status = S.nmi_control = S.keypad_matrix = 0;
    memset(&S.screen1, 0, sizeof S.screen1);
    memset(&S.screen2, 0, sizeof S.screen2);
    memset(&S.sprite, 0, sizeof S.sprite);
    memset(&S.screen2_window, 0, sizeof S.screen2_window);
    S.lcd_enable = true;
    S.lcd_contrast = false;
    S.backdrop = S.lcd_icons = 0;
    S.vtotal = DEFAULT_VTOTAL;
    S.vsync_line = 155;
    S.vcompare = 0xbb;
    S.vcounter = 0;
    S.field = false;
    memset(S.mono_pool, 0, sizeof S.mono_pool);
    memset(S.mono_pal, 0, sizeof S.mono_pal);
    memset(&S.htimer, 0, sizeof S.htimer);
    memset(&S.vtimer, 0, sizeof S.vtimer);
    S.dma_source = S.dma_dest = S.dma_length = 0;
    S.dma_control = 0;
    memset(S.eep_data, 0, sizeof S.eep_data);
    memset(S.eep_cmd, 0, sizeof S.eep_cmd);
    memset(S.eep_ctrl, 0, sizeof S.eep_ctrl);
    S.eep_protect = false;
    wswan_apu_reset(&S.apu, S.color);
    memset(S.fb, 0, W * H * sizeof(uint16_t));
    S.audio_acc = 0;
    S.debt = 0;
    nec_v30_set_irq(&S.cpu, 0, 0xff);
    S.cart_enable = true;            // (no boot ROM: the I/O it leaves, the footer at FFFF:0000)
    for (uint16_t p = 0; p < 0xc8; p++) {
        if (p == 0xc4 || p == 0xc5 || p == 0xba || p == 0xbb) continue;
        io_out(NULL, p, kStartIo[p]);
    }
}

static bool ends_with(const char *s, const char *ext)
{
    if (!s) return false;
    const size_t n = strlen(s), m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    if (!data || size < 16 || size > MAX_CART) {
        if (why) *why = "WonderSwan cartridge too small (or too big)";
        free(data);
        return false;
    }
    uint32_t real = (uint32_t)((size + 0xffff) & ~(size_t)0xffff);
    uint32_t rom_size = 1;
    while (rom_size < real) rom_size <<= 1;
    uint8_t *rom = (uint8_t *)malloc(rom_size);
    if (!rom) {
        if (why) *why = "out of memory";
        free(data);
        return false;
    }
    memset(rom, 0xff, rom_size);
    memcpy(rom + (rom_size - size), data, size);
    free(data);
    const uint8_t *footer = rom + rom_size - 10;
    uint32_t ram = 0, eep = 0;
    switch (footer[5]) {
    case 0x01: ram = 8 * 1024; break;
    case 0x02: ram = 32 * 1024; break;
    case 0x03: ram = 128 * 1024; break;
    case 0x04: ram = 256 * 1024; break;
    case 0x05: ram = 512 * 1024; break;
    case 0x10: eep = 128; break;
    case 0x20: eep = 2048; break;
    case 0x50: eep = 1024; break;
    default: break;
    }
    free(S.rom);
    free(S.cart_ram);
    free(S.cart_eeprom);
    S.rom = rom;
    S.rom_size = rom_size;
    S.cart_ram = ram ? (uint8_t *)calloc(1, ram) : NULL;
    S.cart_ram_size = S.cart_ram ? ram : 0;
    S.cart_eeprom = eep ? (uint8_t *)malloc(eep) : NULL;
    S.cart_eeprom_size = S.cart_eeprom ? eep : 0;
    if (S.cart_eeprom) memset(S.cart_eeprom, 0xff, eep);
    S.color = ends_with(name, ".wsc") || (footer[1] & 1);
    S.internal_eeprom_size = S.color ? 2048 : 128;
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(S.rom);
    free(S.cart_ram);
    free(S.cart_eeprom);
    S.rom = S.cart_ram = S.cart_eeprom = NULL;
    S.rom_size = S.cart_ram_size = S.cart_eeprom_size = 0;
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

static void run_line(void)
{
    const int want = LINE_CYCLES - S.debt;
    S.debt = want > 0 ? nec_v30_run(&S.cpu, want) - want : -want;
}

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    const int lines = (int)S.vtotal + 1 > 144 ? (int)S.vtotal + 1 : 144;
    S.audio_acc = 0;
    for (S.vcounter = 0; S.vcounter < lines; S.vcounter++) {
        if (S.vcounter == S.vcompare) raise_irq(IRQ_LINE);
        if (timer_step(&S.htimer)) raise_irq(IRQ_HTIMER);
        if (S.vcounter < H) {
            render_scanline(S.vcounter);
            if (S.loaded) run_line();
        } else if (S.vcounter == H) {
            raise_irq(IRQ_VBLANK);
            if (timer_step(&S.vtimer)) raise_irq(IRQ_VTIMER);
            S.oam_count = 0;
            if (S.sprite.enable) {
                const uint16_t base = (uint16_t)((S.sprite.oam_base & (grayscale() ? 0x1f : 0x3f)) << 9);
                uint8_t idx = S.sprite.first;
                const int count = S.sprite.count < 128 ? S.sprite.count : 128;
                for (int i = 0; i < count; i++) {
                    const uint16_t a = (uint16_t)(base + idx * 4);
                    S.oam_cache[S.oam_count++] = read_iram16(a) | ((uint32_t)read_iram16((uint16_t)(a + 2)) << 16);
                    idx = (idx + 1) & 0x7f;
                }
            }
            S.field = !S.field;
            if (S.loaded) run_line();
        } else if (S.loaded) {
            run_line();
        }
        S.audio_acc += (uint64_t)WSWAN_APU_RATE * LINE_CYCLES;
        while (S.audio_acc >= CLOCK) {
            S.audio_acc -= CLOCK;
            const int16_t s = wswan_apu_update(&S.apu);
            if (S.naudio < 1024) S.audio[S.naudio++] = s;
        }
    }
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = W;
    *h = H;
    *stride = W;
    return S.fb;
}

static void update_buttons(void)
{
    const unsigned p = S.pads[0], q = S.pads[1];
    uint16_t b = 0;
    if (p & CP_RIGHT) b |= 0x1;
    if (p & CP_LEFT) b |= 0x2;
    if (p & CP_UP) b |= 0x4;
    if (p & CP_DOWN) b |= 0x8;
    if ((q & CP_RIGHT) || S.keys_held[3]) b |= 0x10;
    if ((q & CP_LEFT) || S.keys_held[1]) b |= 0x20;
    if ((q & CP_UP) || S.keys_held[0]) b |= 0x40;
    if ((q & CP_DOWN) || S.keys_held[2]) b |= 0x80;
    if (p & CP_B1) b |= 0x200;
    if (p & CP_B2) b |= 0x400;
    if (p & CP_START) b |= 0x100;
    S.buttons = b;
}

void cons_pad(int player, unsigned b)
{
    S.pads[player & 1] = b;
    update_buttons();
}

void cons_key(int key, bool down)
{
    const int k = key == CK_W ? 0 : key == CK_A ? 1 : key == CK_S ? 2 : key == CK_D ? 3 : -1;
    if (k < 0) return;
    S.keys_held[k] = down;
    update_buttons();
}

const char *cons_status(void) { return S.loaded ? (S.color ? "WonderSwan Color" : "WonderSwan") : NULL; }
