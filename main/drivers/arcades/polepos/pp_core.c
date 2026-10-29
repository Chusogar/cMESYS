// pp_core.c - Namco Pole Position / Pole Position II. See pp_core.h.
//
// Memory maps (MAME namco/polepos.cpp):
//   Z80   0000-2FFF ROM, 3000-37FF NVRAM, 4000-47FF sprite RAM (low bytes),
//         4800-4BFF road RAM, 4C00-4FFF alpha RAM, 5000-57FF view RAM,
//         8000-83BF sound RAM, 83C0-83FF WSG, 9000/9100 06xx data/control,
//         A000 LS259 (write) / ready bits (read), A200/A300 engine sound
//   Z8002 0000-7FFF ROM (PP2 sub1: 4000-5FFF IC25 multiplier), 6000 NVI
//         enable, 8000-8FFF sprites, 9000-97FF road, 9800-9FFF alpha,
//         A000-AFFF view, C000 view H scroll, C100 road V scroll
#include "pp_core.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PP_HOT IRAM_ATTR
#else
#define PP_HOT
#endif

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

// ---------------------------------------------------------------------------
// Inputs and the LS259
// ---------------------------------------------------------------------------
static uint8_t in0(const pp_core *c)
{
    uint8_t v = 0xff;
    if (c->gear_hi) v &= (uint8_t)~0x02;
    if (c->ls259 & 0x40) v &= (uint8_t)~0x04;         // start: program controlled
    if (!(c->in0 & 0x10)) v &= (uint8_t)~0x10;
    if (!(c->in0 & 0x20)) v &= (uint8_t)~0x20;
    if (!(c->in0 & 0x40)) v &= (uint8_t)~0x40;
    return v;
}

static void ls259_w(pp_core *c, int bit, bool value)
{
    uint8_t mask = (uint8_t)(1u << bit);
    if (value) c->ls259 |= mask;
    else c->ls259 &= (uint8_t)~mask;
    switch (bit) {
    case 0:
        c->irq_enable = value;
        if (value) z80_clr_irq(&c->cpu);
        break;
    case 1:
        namco51_set_reset(&c->n51, value);
        namco52_set_reset(&c->n52, value);
        namco53_set_reset(&c->n53, value);
        namco54_set_reset(&c->n54, value);
        break;
    case 2:
        c->wsg.enabled = value;
        pp_engine_clson(&c->engine, value);
        break;
    case 3:
        c->adc_channel = value;
        break;
    case 4:
    case 5:
        c->sub_reset[bit - 4] = !value;
        z8002_set_reset_line(&c->sub[bit - 4], !value);
        break;
    case 7:
        c->chacl = value ? 1 : 0;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Namco 06xx and the MCUs
// ---------------------------------------------------------------------------
static void n06_ctrl_w(pp_core *c, uint8_t data)
{
    c->n06_control = data;
    if ((data & 0xe0) == 0) {
        c->n06_period = 0;
        c->n06_timer_state = false;
        z80_clr_nmi(&c->cpu);
        namco51_chip_select(&c->n51, false);
        namco53_chip_select(&c->n53, false);
        namco52_chip_select(&c->n52, false);
        namco54_chip_select(&c->n54, false);
    } else {
        int divisor = 1 << ((data >> 5) & 7);
        c->n06_period = 64 * divisor;           // one NMI per divided 06xx clock
        c->n06_cycle_acc = 0;
        if (data & 0x10) {
            z80_clr_nmi(&c->cpu);
            c->n06_read_stretch = true;
        } else {
            c->n06_read_stretch = false;
        }
    }
}

static void n06_tick(pp_core *c)
{
    c->n06_timer_state = !c->n06_timer_state;
    if (c->n06_timer_state) namco51_set_rw(&c->n51, (c->n06_control & 0x10) != 0);
    if (c->n06_timer_state && !c->n06_read_stretch) z80_pulse_nmi(&c->cpu);
    else z80_clr_nmi(&c->cpu);
    c->n06_read_stretch = false;
    bool t = c->n06_timer_state;
    namco51_chip_select(&c->n51, (c->n06_control & 0x01) && t);
    namco53_chip_select(&c->n53, (c->n06_control & 0x02) && t);
    namco52_chip_select(&c->n52, (c->n06_control & 0x04) && t);
    namco54_chip_select(&c->n54, (c->n06_control & 0x08) && t);
}

static uint8_t n06_data_r(pp_core *c)
{
    if (!(c->n06_control & 0x10)) return 0;
    uint8_t r = 0xff;
    if (c->n06_control & 0x01) r &= namco51_read(&c->n51);
    if (c->n06_control & 0x02) r &= namco53_read(&c->n53);
    return r;
}

static void n06_data_w(pp_core *c, uint8_t data)
{
    if (c->n06_control & 0x10) return;
    if (c->n06_control & 0x01) namco51_write(&c->n51, data);
    if (c->n06_control & 0x04) namco52_write(&c->n52, data);
    if (c->n06_control & 0x08) namco54_write(&c->n54, data);
}

static uint8_t steering_changed_r(pp_core *c)
{
    uint8_t now = c->steer;
    c->steer_accum = (int16_t)(c->steer_accum + (int8_t)(now - c->steer_last) * 2);
    c->steer_last = now;
    if (c->steer_accum < 0) {
        c->steer_delta = 0;
        c->steer_accum++;
    } else if (c->steer_accum > 0) {
        c->steer_delta = 1;
        c->steer_accum--;
    }
    return (uint8_t)(c->steer_accum & 1);
}

static uint8_t n51_in(void *u, int port)
{
    pp_core *c = u;
    switch (port) {
    case 0: return (uint8_t)(c->dswb & 15);
    case 1: return (uint8_t)(c->dswb >> 4);
    case 2: return (uint8_t)(in0(c) & 15);
    default: return (uint8_t)(in0(c) >> 4);
    }
}

static uint8_t n53_in(void *u, int port)
{
    pp_core *c = u;
    switch (port) {
    case 0: return steering_changed_r(c);
    case 1: return c->steer_delta;
    case 2: return (uint8_t)(c->dswa & 15);
    default: return (uint8_t)(c->dswa >> 4);
    }
}

static uint8_t n52_rom(void *u, uint16_t a)
{
    pp_core *c = u;
    return a < c->roms.voice_size ? c->roms.voice[a] : 0xff;
}

// The Z80's clock drives the 06xx, the MCUs (parent clock = Z80 / 2, / 6
// inside: one MCU cycle every 12 Z80 cycles) and the sound output.
static void PP_HOT mcu_flush(pp_core *c)
{
    int n = c->mcu_cycle_acc / 12;
    if (!n) return;
    c->mcu_cycle_acc -= n * 12;
    mb88_run(&c->n51.cpu, n);
    mb88_run(&c->n53.cpu, n);
    mb88_run(&c->n52.cpu, n);
    mb88_run(&c->n54.cpu, n);
}

static void PP_HOT z80_cycles(pp_core *c, int cycles)
{
    // The MCUs catch up in batches: before each 06xx edge and at the end of
    // each 16-cycle slice (pp_run_frame).
    c->mcu_cycle_acc += cycles;
    if (c->n06_period > 0) {
        c->n06_cycle_acc += cycles;
        while (c->n06_cycle_acc >= c->n06_period) {
            c->n06_cycle_acc -= c->n06_period;
            mcu_flush(c);
            n06_tick(c);
        }
    }
    c->audio_acc += (uint32_t)cycles * c->sample_rate;
    while (c->audio_acc >= PP_CPU_CLOCK) {
        c->audio_acc -= PP_CPU_CLOCK;
        // 8 voices of +-8 x volume (left + right, up to 60): x20 keeps a
        // busy mix in range; then a DC blocker (the board's output caps).
        int32_t x = pp_wsg_update(&c->wsg, c->n54.chanl1, c->n54.chanl2, c->n54.chanl3, c->n52.dac) * 20;
        x += pp_engine_update(&c->engine);
        int32_t s = x - c->dc_x + c->dc_y * 255 / 256;   // (rounds to 0: no stuck offset)
        c->dc_x = x;
        c->dc_y = s;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (c->audio && c->audio_n < c->audio_cap) c->audio[c->audio_n++] = (int16_t)s;
    }
}

// ---------------------------------------------------------------------------
// Z80
// ---------------------------------------------------------------------------
static uint8_t PP_HOT z80_read(void *u, uint16_t a)
{
    pp_core *c = u;
    if (a < 0x3000) return a < c->roms.z80_size ? c->roms.z80[a] : 0xff;
    if (a < 0x4000) return c->nvram[a & 0x7ff];
    if (a < 0x4800) return c->sprite_ram[(a - 0x4000) * 2 + 1];
    if (a < 0x4c00) return c->road_ram[(a - 0x4800) * 2 + 1];
    if (a < 0x5000) return c->alpha_ram[(a - 0x4c00) * 2 + 1];
    if (a < 0x5800) return c->view_ram[(a - 0x5000) * 2 + 1];
    if (a >= 0x8000 && a < 0x9000) {
        uint16_t off = a & 0x3ff;
        return off >= 0x3c0 ? pp_wsg_read(&c->wsg, (uint8_t)(off & 0x3f)) : c->sound_ram[off];
    }
    if ((a & 0xf000) == 0x9000) {
        mcu_flush(c);
        return (a & 0x100) ? c->n06_control : n06_data_r(c);
    }
    if ((a & 0xf000) == 0xa000) {
        if ((a & 0x300) == 0) {                     // ready bits
            uint8_t r = 0xff;
            if (c->scanline >= 128) r ^= 0x02;
            if (c->adc_ready) r ^= 0x08;
            return r;
        }
        return 0xff;
    }
    return 0xff;
}

static void PP_HOT z80_write(void *u, uint16_t a, uint8_t v)
{
    pp_core *c = u;
    if (a >= 0x3000 && a < 0x4000) { c->nvram[a & 0x7ff] = v; return; }
    if (a >= 0x4000 && a < 0x4800) { c->sprite_ram[(a - 0x4000) * 2 + 1] = v; return; }
    if (a >= 0x4800 && a < 0x4c00) { c->road_ram[(a - 0x4800) * 2 + 1] = v; return; }
    if (a >= 0x4c00 && a < 0x5000) { c->alpha_ram[(a - 0x4c00) * 2 + 1] = v; return; }
    if (a >= 0x5000 && a < 0x5800) { c->view_ram[(a - 0x5000) * 2 + 1] = v; return; }
    if (a >= 0x8000 && a < 0x9000) {
        uint16_t off = a & 0x3ff;
        if (off >= 0x3c0) pp_wsg_write(&c->wsg, (uint8_t)(off & 0x3f), v);
        else c->sound_ram[off] = v;
        return;
    }
    if ((a & 0xf000) == 0x9000) {
        mcu_flush(c);
        if (a & 0x100) n06_ctrl_w(c, v);
        else n06_data_w(c, v);
        return;
    }
    if ((a & 0xf000) == 0xa000) {
        switch (a & 0x300) {
        case 0x000: mcu_flush(c); ls259_w(c, a & 7, (v & 1) != 0); break;
        case 0x200: pp_engine_lsb_w(&c->engine, v); break;
        case 0x300: pp_engine_msb_w(&c->engine, v); break;
        default: break;
        }
    }
}

static uint8_t z80_in(z80 *z, uint16_t port)
{
    pp_core *c = z->userdata;
    if ((port & 0xff) == 0) {
        c->adc_ready = true;
        return c->adc_channel ? c->accel : c->brake;
    }
    return 0xff;
}

static void z80_out(z80 *z, uint16_t port, uint8_t v)
{
    (void)v;
    pp_core *c = z->userdata;
    if ((port & 0xff) == 0) c->adc_ready = true;
}

// ---------------------------------------------------------------------------
// Z8002s
// ---------------------------------------------------------------------------
static uint16_t ic25_r(pp_core *c, uint16_t offset)
{
    offset &= 0x1ff;
    int r;
    if (offset < 0x100) {
        c->ic25_signed = (int8_t)(offset & 0xff);
        r = c->ic25_result & 0xff;
    } else {
        c->ic25_unsigned = (uint8_t)(offset & 0xff);
        r = (c->ic25_result >> 8) & 0xff;
        c->ic25_result = (int16_t)(c->ic25_signed * (int16_t)c->ic25_unsigned);
    }
    return (uint16_t)(r | (r << 8));
}

static inline uint8_t PP_HOT sub_read(pp_core *c, int which, uint16_t a)
{
    if (a < 0x8000) {
        if (c->roms.pp2 && which == 0 && a >= 0x4000 && a < 0x6000) {
            if (!(a & 1)) c->ic25_word = ic25_r(c, (uint16_t)((a - 0x4000) >> 1));
            return (uint8_t)(c->ic25_word >> ((a & 1) ? 0 : 8));
        }
        return (which ? c->roms.sub2 : c->roms.sub1)[a];
    }
    if (a < 0x9000) return c->sprite_ram[a & 0xfff];
    if (a < 0x9800) return c->road_ram[a & 0x7ff];
    if (a < 0xa000) return c->alpha_ram[a & 0x7ff];
    if (a < 0xb000) return c->view_ram[a & 0xfff];
    return 0xff;
}

static void PP_HOT sub_write(pp_core *c, int which, uint16_t a, uint8_t v)
{
    if (a < 0x8000) {
        if ((a & 0xe000) == 0x6000 && (a & 1)) {    // NVI enable (low byte of the word)
            c->sub_irq_mask = v & 1;
            if (!c->sub_irq_mask) z8002_set_nvi(&c->sub[which], false);
        }
        return;
    }
    if (a < 0x9000) { c->sprite_ram[a & 0xfff] = v; return; }
    if (a < 0x9800) { c->road_ram[a & 0x7ff] = v; return; }
    if (a < 0xa000) { c->alpha_ram[a & 0x7ff] = v; return; }
    if (a < 0xb000) { c->view_ram[a & 0xfff] = v; return; }
    if ((a & 0xc000) == 0xc000) {
        uint16_t *t = (a & 0x100) ? &c->road_vscroll : &c->view_hscroll;
        if (!(a & 1)) *t = (uint16_t)((*t & 0x00ff) | (v << 8));
        else *t = (uint16_t)((*t & 0xff00) | v);
    }
}

static uint16_t PP_HOT sub_read16(pp_core *c, int which, uint16_t a)
{
    if (a < 0x8000 && !(c->roms.pp2 && which == 0 && a >= 0x4000 && a < 0x6000))
        return be16((which ? c->roms.sub2 : c->roms.sub1) + a);
    if (a >= 0x8000 && a < 0xb000) {
        const uint8_t *p = a < 0x9000 ? &c->sprite_ram[a & 0xfff] : a < 0x9800 ? &c->road_ram[a & 0x7ff]
                         : a < 0xa000 ? &c->alpha_ram[a & 0x7ff] : &c->view_ram[a & 0xfff];
        return be16(p);
    }
    uint8_t hi = sub_read(c, which, a);
    return (uint16_t)((hi << 8) | sub_read(c, which, (uint16_t)(a + 1)));
}

static uint8_t sub1_read(void *u, uint16_t a) { return sub_read(u, 0, a); }
static uint8_t sub2_read(void *u, uint16_t a) { return sub_read(u, 1, a); }
static uint16_t sub1_read16(void *u, uint16_t a) { return sub_read16(u, 0, a); }
static uint16_t sub2_read16(void *u, uint16_t a) { return sub_read16(u, 1, a); }
static void sub1_write(void *u, uint16_t a, uint8_t v) { sub_write(u, 0, a, v); }
static void sub2_write(void *u, uint16_t a, uint8_t v) { sub_write(u, 1, a, v); }

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
static void build_palette(pp_core *c)
{
    const uint8_t *p = c->roms.proms;
    for (int i = 0; i < 128; i++) {
        int ch[3];
        for (int k = 0; k < 3; k++) {
            uint8_t v = p[k * 0x100 + i];
            ch[k] = 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
        }
        c->rgb[i] = ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
    }
    for (int i = 0; i < 64 * 4; i++) {
        int col = p[0x300 + i] & 15;
        c->pens[0x000 + i] = (uint8_t)(col != 15 ? 0x20 + col : 0x2f);
        c->pens[0x100 + i] = (uint8_t)(col != 15 ? 0x60 + col : 0x2f);
        c->pens[0x200 + i] = (uint8_t)(p[0x400 + i] & 15);
    }
    for (int i = 0; i < 64 * 16; i++) {
        int col = p[0xc00 + i] & 15;
        c->pens[0x300 + i] = (uint8_t)(col != 15 ? 0x10 + col : 0x1f);
        c->pens[0x700 + i] = (uint8_t)(col != 15 ? 0x50 + col : 0x1f);
        c->pens[0xb00 + i] = (uint8_t)(0x40 + (p[0x800 + i] & 15));
    }
    for (int i = 0; i < 256; i++)
        c->vpos_mod[i] = (uint16_t)(p[0x500 + i] + (p[0x600 + i] << 4) + (p[0x700 + i] << 8));
}

void pp_init(pp_core *c, const pp_roms *roms, uint32_t sample_rate)
{
    int16_t *audio = c->audio;
    int cap = c->audio_cap;
    memset(c, 0, sizeof *c);
    c->audio = audio;
    c->audio_cap = cap;
    c->roms = *roms;
    c->sample_rate = sample_rate ? sample_rate : 44100;
    // MAME's defaults: polepos (World) uses the "poleposa" switches (mph,
    // practice rank B, extended rank E... demo sounds off), polepos2 the
    // "polepos2j" ones (all off: km/h).
    c->dswa = 0xff;
    c->dswb = roms->pp2 ? 0xff : 0x74;
    c->steer = 0x80;
    c->in0 = 0xff;

    z80_init(&c->cpu);
    c->cpu.read_byte = z80_read;
    c->cpu.write_byte = z80_write;
    c->cpu.port_in = z80_in;
    c->cpu.port_out = z80_out;
    c->cpu.userdata = c;
    c->sub[0].read = sub1_read;
    c->sub[0].read16 = sub1_read16;
    c->sub[0].write = sub1_write;
    c->sub[1].read = sub2_read;
    c->sub[1].read16 = sub2_read16;
    c->sub[1].write = sub2_write;
    c->sub[0].user = c->sub[1].user = c;
    // programs fetched straight from the ROMs (below the PP2 multiplier at 4000 on sub 1)
    c->sub[0].fetch = roms->sub1;
    c->sub[0].fetch_end = roms->pp2 ? 0x4000 : 0x8000;
    c->sub[1].fetch = roms->sub2;
    c->sub[1].fetch_end = 0x8000;
    z8002_init(&c->sub[0]);
    z8002_init(&c->sub[1]);

    namco51_init(&c->n51, roms->n51);
    c->n51.in = n51_in;
    c->n51.user = c;
    namco52_init(&c->n52, roms->n52);
    c->n52.rom_read = n52_rom;
    c->n52.user = c;
    namco53_init(&c->n53, roms->n53);
    c->n53.in = n53_in;
    c->n53.user = c;
    namco54_init(&c->n54, roms->n54);
    // The 51xx and 52xx spend most of their time in polling loops (the 51xx
    // waiting for its timer, the 52xx for its IRQ pin): counted, not run.
    static const uint8_t loop51[5] = { 0x16, 0x2e, 0x68, 0x97, 0xc2 }, loop52[2] = { 0x25, 0xe6 };
#ifndef PP_NO_IDLE_SKIP
    if (roms->n51 && !memcmp(roms->n51 + 0x182, loop51, 5)) mb88_set_idle_loop(&c->n51.cpu, 0x182, 5);
    if (roms->n52 && !memcmp(roms->n52 + 0x66, loop52, 2)) mb88_set_idle_loop(&c->n52.cpu, 0x66, 2);
#endif

    pp_wsg_init(&c->wsg, PP_MASTER_CLOCK / 512, c->sample_rate, roms->wave);
    pp_engine_init(&c->engine, PP_CPU_CLOCK, c->sample_rate, roms->engine, roms->engine_size);
    build_palette(c);
    pp_reset(c);
}

void pp_reset(pp_core *c)
{
    z80_reset(&c->cpu);
    z8002_reset(&c->sub[0]);
    z8002_reset(&c->sub[1]);
    memset(c->nvram, 0xff, sizeof c->nvram);
    memset(c->sound_ram, 0, sizeof c->sound_ram);
    memset(c->sprite_ram, 0, sizeof c->sprite_ram);
    memset(c->road_ram, 0, sizeof c->road_ram);
    memset(c->alpha_ram, 0, sizeof c->alpha_ram);
    memset(c->view_ram, 0, sizeof c->view_ram);
    c->view_hscroll = c->road_vscroll = 0;
    c->ls259 = c->chacl = c->sub_irq_mask = 0;
    c->sub_reset[0] = c->sub_reset[1] = true;
    c->irq_enable = c->adc_channel = false;
    c->adc_ready = true;
    c->scanline = 0;
    c->n06_control = 0;
    c->n06_timer_state = c->n06_read_stretch = false;
    c->n06_cycle_acc = c->n06_period = c->mcu_cycle_acc = 0;
    namco51_reset(&c->n51);
    namco52_reset(&c->n52);
    namco53_reset(&c->n53);
    namco54_reset(&c->n54);
    namco51_set_reset(&c->n51, false);
    namco52_set_reset(&c->n52, false);
    namco53_set_reset(&c->n53, false);
    namco54_set_reset(&c->n54, false);
    pp_wsg_reset(&c->wsg);
    pp_engine_reset(&c->engine);
    c->steer_last = c->steer;
    c->steer_delta = 0;
    c->steer_accum = 0;
    c->ic25_result = 0;
    c->ic25_signed = 0;
    c->ic25_unsigned = 0;
    c->ic25_word = 0;
    c->audio_acc = 0;
    z8002_set_reset_line(&c->sub[0], true);
    z8002_set_reset_line(&c->sub[1], true);
}

void pp_set_inputs(pp_core *c, const pp_inputs *in)
{
    c->in0 = 0xff;
    if (in->coin1) c->in0 &= (uint8_t)~0x10;
    if (in->coin2) c->in0 &= (uint8_t)~0x20;
    if (in->service) c->in0 &= (uint8_t)~0x40;
    if (in->gear && !c->gear_prev) c->gear_hi = !c->gear_hi;
    c->gear_prev = in->gear;
    c->accel = in->gas ? 0x90 : 0x00;
    c->brake = in->brake ? 0x90 : 0x00;
    if (in->wheel) {                    // follow the analog wheel (the dial turns as fast as on the cabinet)
        int d = (int)in->wheel - c->steer;
        if (d > 8) d = 8;
        if (d < -8) d = -8;
        c->steer = (uint8_t)(c->steer + d);
        return;
    }
    if (in->left && c->steer > 8) c->steer = (uint8_t)(c->steer - 4);
    if (in->right && c->steer < 0xf8) c->steer = (uint8_t)(c->steer + 4);
    if (!in->left && !in->right) {
        if (c->steer < 0x80) c->steer++;
        if (c->steer > 0x80) c->steer--;
    }
}

// The Z80 runs whole instructions until it has done `cycles` (as dsp-cpp).
static void PP_HOT run_z80(pp_core *c, int cycles)
{
    // the overshoot of the last instruction is paid from the next slice
    c->z80_budget += cycles;
    while (c->z80_budget > 0) {
        int n = (int)z80_step(&c->cpu);
        z80_cycles(c, n);
        c->z80_budget -= n;
    }
}

void pp_run_frame(pp_core *c)
{
    for (int line = 0; line < PP_LINES; line++) {
        c->scanline = line;
        if ((line == 64 || line == 192) && c->irq_enable) z80_assert_irq(&c->cpu, 0xff);
        if (line == 240) {
            namco51_vblank(&c->n51, true);
            if (c->sub_irq_mask) {
                // a still-high pin gets a new edge every vblank (MAME's scanline timer)
                for (int i = 0; i < 2; i++)
                    if (!c->sub_reset[i]) {
                        z8002_set_nvi(&c->sub[i], false);
                        z8002_set_nvi(&c->sub[i], true);
                    }
            }
        } else if (line == 0) {
            namco51_vblank(&c->n51, false);
        }
        for (int left = PP_LINE_CYCLES; left > 0; left -= 16) {
            int slice = left < 16 ? left : 16;
            run_z80(c, slice);
            for (int i = 0; i < 2; i++) {
                if (c->sub_reset[i]) { c->sub_budget[i] = 0; continue; }
                c->sub_budget[i] += slice;
                if (c->sub_budget[i] > 0) c->sub_budget[i] -= z8002_run(&c->sub[i], c->sub_budget[i]);
            }
        }
        mcu_flush(c);           // (also before each 06xx edge and Z80 access to the 06xx / latch)
    }
    c->frame++;
    if (c->on_frame) c->on_frame(c->user, c);
}
