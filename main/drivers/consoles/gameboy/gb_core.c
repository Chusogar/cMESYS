// gb_core.c - Nintendo Game Boy (1989) and Game Boy Color (1998): a C11
// migration of dsp-cpp's drivers/consoles/gameboy and machine/gb_mapper
// (from gb.pas, gb_mappers.pas). The LR35902 (cpu/lr35902), the picture
// (video/gb_ppu), the sound (sound/gb_apu), the timer, OAM DMA, the CGB's
// double speed, WRAM banks and HDMA; MBC1 / MBC2 / MBC3 (with the RTC
// latched from the clock) / MBC5. The model from the cartridge's header.
// No boot ROM: the registers and I/O it leaves behind.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/cons.h"
#include "../../../cpu/lr35902/lr35902.h"
#include "../../../video/gb_ppu.h"
#include "../../../sound/gb_apu.h"

#define CLOCK 4194304
#define LINE_CYCLES 456
#define LINES 154
#define RATE 44100
#define MAX_CART 0x800000

enum { MBC_NONE, MBC1, MBC2, MBC3, MBC5 };

static const char *const kExts[] = { ".gb", ".gbc", ".cgb", ".sgb", NULL };
const cons_info cons_machine = {
    "gameboy", "Nintendo Game Boy / Color", "Nintendo, 1989", kExts, "GB GBC", GB_W, GB_H,
    (double)CLOCK / LINE_CYCLES / LINES, RATE, { "A", "B", NULL, NULL }, "START", "SELECT", CONS_NO_KEYS, 1, NULL,
};

static const uint8_t kNintendoLogo[0x30] = {
    0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
    0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
    0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
};

static struct {
    lr35902 cpu;
    gb_ppu ppu;
    gb_apu apu;
    bool is_cgb, loaded;
    // the mapper
    int type;
    bool has_battery, has_rtc;
    uint8_t *rom;
    size_t rom_size, rom_banks;
    uint8_t *ram;
    size_t ram_size;
    bool ram_enable, rom_mode;
    uint32_t rom_bank, ram_bank, mbc1_reg0, mbc1_reg1;
    uint8_t mbc1_mask;
    int mbc1_shift;
    uint8_t rtc_regs[5];
    bool rtc_latch_armed;
    // the machine
    uint16_t *fb;
    uint8_t wram[8][0x1000];
    int wram_bank;
    uint8_t io_ram[0x100];
    uint16_t div_counter;
    uint8_t tima, tma, tac;
    int timer_cycles;
    uint8_t stat, ly_compare;
    int line, line_cycles, sprites_time;
    bool hdma_done_this_line;
    int oam_dma_remaining;
    uint16_t dma_src, dma_dst;
    bool hdma_active;
    uint8_t hdma_size;
    uint8_t cgb_oam_unused[0x30];
    uint8_t joy_select, joy_val, joystick;
    uint64_t audio_acc;
    int debt;
    int16_t audio[1024];
    int naudio;
} S;

// ---- the mapper (gb_mapper) ----
static void mapper_reset(void)
{
    S.ram_enable = S.rom_mode = false;
    S.rom_bank = 1;
    S.ram_bank = 0;
    S.mbc1_reg0 = 1;
    S.mbc1_reg1 = 0;
    memset(S.rtc_regs, 0, sizeof S.rtc_regs);
    S.rtc_latch_armed = false;
}

static uint8_t read_rom(uint16_t a)
{
    if (!S.rom) return 0xff;
    size_t off;
    if (a < 0x4000) {
        size_t bank0 = 0;
        if (S.type == MBC1 && S.rom_mode) bank0 = (size_t)((S.ram_bank << S.mbc1_shift) % S.rom_banks);
        off = bank0 * 0x4000 + a;
    } else {
        off = (S.rom_bank % S.rom_banks) * 0x4000 + (size_t)(a - 0x4000);
    }
    return off < S.rom_size ? S.rom[off] : 0xff;
}

static void write_register(uint16_t a, uint8_t v)
{
    switch (S.type) {
    case MBC1:
        if (a <= 0x1fff) {
            S.ram_enable = (v & 0xf) == 0xa;
            return;
        } else if (a <= 0x3fff) {
            S.mbc1_reg0 = v & 0x1f;
            if (!S.mbc1_reg0) S.mbc1_reg0 = 1;
        } else if (a <= 0x5fff) {
            S.mbc1_reg1 = v & 3;
        } else {
            S.rom_mode = v & 1;
        }
        S.rom_bank = ((S.mbc1_reg0 & S.mbc1_mask) | (S.mbc1_reg1 << S.mbc1_shift)) % (uint32_t)S.rom_banks;
        S.ram_bank = S.rom_mode ? S.mbc1_reg1 : 0;
        break;
    case MBC2:
        if (a <= 0x3fff) {
            if (!(a & 0x100)) {
                S.ram_enable = (v & 0xf) == 0xa;
            } else {
                S.rom_bank = v & 0xf;
                if (!S.rom_bank) S.rom_bank = 1;
            }
        }
        break;
    case MBC3:
        if (a <= 0x1fff) {
            S.ram_enable = (v & 0xf) == 0xa;
        } else if (a <= 0x3fff) {
            S.rom_bank = v & 0x7f;
            if (!S.rom_bank) S.rom_bank = 1;
        } else if (a <= 0x5fff) {
            S.ram_bank = v;
        } else if (S.has_rtc) {
            if (S.rtc_latch_armed && v == 1) {
                S.rtc_latch_armed = false;
                const time_t now = time(NULL);
                const struct tm *t = localtime(&now);
                if (t) {
                    S.rtc_regs[0] = (uint8_t)t->tm_sec;
                    S.rtc_regs[1] = (uint8_t)t->tm_min;
                    S.rtc_regs[2] = (uint8_t)t->tm_hour;
                }
            } else if (v == 0) {
                S.rtc_latch_armed = true;
            }
        }
        break;
    case MBC5:
        if (a <= 0x1fff) S.ram_enable = (v & 0xf) == 0xa;
        else if (a <= 0x2fff) S.rom_bank = (S.rom_bank & 0x100) | v;
        else if (a <= 0x3fff) S.rom_bank = (S.rom_bank & 0xff) | (uint32_t)((v & 1) << 8);
        else if (a <= 0x5fff) S.ram_bank = v & 0xf;
        break;
    default:
        break;
    }
}

static inline size_t ram_off(uint16_t off)
{
    if (S.ram_size <= 0x2000) return off % S.ram_size;
    return ((size_t)S.ram_bank * 0x2000 + off) % S.ram_size;
}

static uint8_t read_ram(uint16_t a)
{
    const uint16_t off = a & 0x1fff;
    switch (S.type) {
    case MBC1: case MBC5:
        if (!S.ram_enable || !S.ram_size) return 0xff;
        return S.ram[ram_off(off)];
    case MBC2:
        if (!S.ram_enable) return 0xff;
        return (uint8_t)(0xf0 | (S.ram[off & 0x1ff] & 0xf));
    case MBC3:
        if (S.ram_bank <= 3) {
            if (!S.ram_enable || !S.ram_size) return 0xff;
            return S.ram[((size_t)S.ram_bank * 0x2000 + off) % S.ram_size];
        }
        if (S.ram_bank >= 8 && S.ram_bank <= 0xc) return S.rtc_regs[S.ram_bank - 8];
        return 0xff;
    default:
        return 0xff;
    }
}

static void write_ram(uint16_t a, uint8_t v)
{
    const uint16_t off = a & 0x1fff;
    switch (S.type) {
    case MBC1: case MBC5:
        if (S.ram_enable && S.ram_size) S.ram[ram_off(off)] = v;
        break;
    case MBC2:
        if (S.ram_enable) S.ram[off & 0x1ff] = (uint8_t)(0xf0 | (v & 0xf));
        break;
    case MBC3:
        if (S.ram_bank <= 3) {
            if (S.ram_enable && S.ram_size) S.ram[((size_t)S.ram_bank * 0x2000 + off) % S.ram_size] = v;
        } else if (S.ram_bank >= 8 && S.ram_bank <= 0xc) {
            S.rtc_regs[S.ram_bank - 8] = v;
        }
        break;
    default:
        break;
    }
}

// ---- the machine ----
static uint8_t dma_read(uint16_t a)
{
    if (a <= 0x7fff) return read_rom(a);
    if (a <= 0x9fff) return 0xff;
    if (a <= 0xbfff) return read_ram((uint16_t)(a & 0x1fff));
    if (a <= 0xcfff) return S.wram[0][a & 0xfff];
    if (a <= 0xdfff) return S.wram[S.wram_bank][a & 0xfff];
    return read_ram((uint16_t)(a & 0x1fff));
}

static uint8_t read_io(uint8_t o)
{
    switch (o) {
    case 0x00:
        if (!(S.joy_select & 0x10)) S.joystick = (uint8_t)((S.joystick | 0xf) & (S.joy_val | 0xf0));
        if (!(S.joy_select & 0x20)) S.joystick = (uint8_t)((S.joystick | 0xf) & ((S.joy_val >> 4) | 0xf0));
        return S.joystick;
    case 0x01: return 0;
    case 0x02: return S.is_cgb ? 0x7c : 0x7e;
    case 0x04: return (uint8_t)(S.div_counter >> 8);
    case 0x05: return S.tima;
    case 0x06: return S.tma;
    case 0x07: return (uint8_t)(0xf8 | S.tac);
    case 0x0f: {
        uint8_t v = 0xe0;
        if (S.cpu.vblank_req) v |= 0x01;
        if (S.cpu.lcdstat_req) v |= 0x02;
        if (S.cpu.timer_req) v |= 0x04;
        if (S.cpu.serial_req) v |= 0x08;
        if (S.cpu.joystick_req) v |= 0x10;
        return v;
    }
    case 0x40: return S.ppu.lcdc;
    case 0x41: return (uint8_t)(0x80 | S.stat);
    case 0x44: return (uint8_t)S.line;
    case 0x45: return S.ly_compare;
    case 0x4d: return S.is_cgb ? (uint8_t)((S.cpu.speed << 7) | 0x7e | (S.cpu.change_speed ? 1 : 0)) : 0xff;
    case 0x4f: return S.is_cgb ? (uint8_t)(0xfe | S.ppu.vbk) : 0xff;
    case 0x51: case 0x52: case 0x53: case 0x54: return 0xff;
    case 0x55: return S.is_cgb ? S.hdma_size : 0xff;
    case 0x56: return S.is_cgb ? 1 : 0xff;
    case 0x68: return S.is_cgb ? S.ppu.bgp_index : 0xff;
    case 0x69: return S.is_cgb ? gb_ppu_read_bg_pal(&S.ppu) : 0xff;
    case 0x6a: return S.is_cgb ? S.ppu.obp_index : 0xff;
    case 0x6b: return S.is_cgb ? gb_ppu_read_obj_pal(&S.ppu) : 0xff;
    case 0x70: return S.is_cgb ? (uint8_t)(0xf8 | S.wram_bank) : 0xff;
    default:
        if (o >= 0x10 && o <= 0x26) return gb_apu_read(&S.apu, (uint8_t)(o - 0x10));
        if (o >= 0x30 && o <= 0x3f) return gb_apu_wave_read(&S.apu, (uint8_t)(o - 0x30));
        return S.io_ram[o];
    }
}

static void hdma_block(void)
{
    for (int i = 0; i < 0x10; i++) {
        gb_ppu_vram_write(&S.ppu, (uint16_t)(S.dma_dst & 0x1fff), dma_read(S.dma_src));
        S.dma_dst++;
        S.dma_src++;
    }
    if (S.hdma_size == 0) {
        S.hdma_size = 0xff;
        S.hdma_active = false;
    } else {
        S.hdma_size--;
    }
}

static void write_io(uint8_t o, uint8_t v)
{
    S.io_ram[o] = v;
    lr35902 *c = &S.cpu;
    switch (o) {
    case 0x00: S.joy_select = (uint8_t)(0xcf | (v & 0x30)); S.joystick = S.joy_select; break;
    case 0x02: if ((v & 0x81) == 0x81) c->serial_req = true; break;
    case 0x04: S.div_counter = 0; break;
    case 0x05: S.tima = v; break;
    case 0x06: S.tma = v; break;
    case 0x07: S.tac = v & 7; break;
    case 0x0f:
        c->vblank_req = v & 0x01;
        c->lcdstat_req = (v & 0x02) != 0;
        c->timer_req = (v & 0x04) != 0;
        c->serial_req = (v & 0x08) != 0;
        c->joystick_req = (v & 0x10) != 0;
        break;
    case 0x40:
        S.ppu.lcdc = v;
        if (!(v & 0x80)) S.stat &= 0xfc;
        break;
    case 0x41: S.stat = (uint8_t)((S.stat & 0x7) | (v & 0xf8)); break;
    case 0x42: gb_ppu_write_scy_mid_line(&S.ppu, v, (S.line_cycles >> c->speed) / 4); break;
    case 0x43: S.ppu.scx = v; break;
    case 0x45: S.ly_compare = v; break;
    case 0x46: {
        const uint16_t src = (uint16_t)(v << 8);
        for (int i = 0; i < 0xa0; i++) S.ppu.oam[i] = dma_read((uint16_t)(src + i));
        S.oam_dma_remaining = 160;
        break;
    }
    case 0x47: S.ppu.bgp = v; break;
    case 0x48: S.ppu.obp0 = v; break;
    case 0x49: S.ppu.obp1 = v; break;
    case 0x4a: S.ppu.wy = v; break;
    case 0x4b: S.ppu.wx = v; break;
    case 0x4d: if (S.is_cgb) c->change_speed = v & 1; break;
    case 0x4f: if (S.is_cgb) S.ppu.vbk = v & 1; break;
    case 0x51: S.dma_src = (uint16_t)((S.dma_src & 0xff) | (v << 8)); break;
    case 0x52: S.dma_src = (uint16_t)((S.dma_src & 0xff00) | (v & 0xf0)); break;
    case 0x53: S.dma_dst = (uint16_t)((S.dma_dst & 0xff) | ((v & 0x1f) << 8)); break;
    case 0x54: S.dma_dst = (uint16_t)((S.dma_dst & 0xff00) | (v & 0xf0)); break;
    case 0x55:
        if (!S.is_cgb) break;
        if (v & 0x80) {
            S.hdma_size = v & 0x7f;
            S.hdma_active = true;
        } else if (S.hdma_active) {
            S.hdma_active = false;
            S.hdma_size |= 0x80;
        } else {
            const int blocks = v + 1;
            for (int i = 0; i < blocks * 0x10; i++) {
                gb_ppu_vram_write(&S.ppu, (uint16_t)(S.dma_dst & 0x1fff), dma_read(S.dma_src));
                S.dma_dst++;
                S.dma_src++;
            }
            S.hdma_size = 0xff;
            lr35902_stall(c, (220 >> c->speed) + 8 * blocks);
        }
        break;
    case 0x68: if (S.is_cgb) { S.ppu.bgp_index = v & 0x3f; S.ppu.bgp_inc = (v & 0x80) != 0; } break;
    case 0x69: if (S.is_cgb) gb_ppu_write_bg_pal(&S.ppu, v, (S.stat & 3) == 3); break;
    case 0x6a: if (S.is_cgb) { S.ppu.obp_index = v & 0x3f; S.ppu.obp_inc = (v & 0x80) != 0; } break;
    case 0x6b: if (S.is_cgb) gb_ppu_write_obj_pal(&S.ppu, v, (S.stat & 3) == 3); break;
    case 0x70:
        if (!S.is_cgb) break;
        S.wram_bank = v & 7;
        if (!S.wram_bank) S.wram_bank = 1;
        break;
    case 0xff:
        c->vblank_ena = v & 0x01;
        c->lcdstat_ena = (v & 0x02) != 0;
        c->timer_ena = (v & 0x04) != 0;
        c->serial_ena = (v & 0x08) != 0;
        c->joystick_ena = (v & 0x10) != 0;
        break;
    default:
        if (o >= 0x10 && o <= 0x26) gb_apu_write(&S.apu, (uint8_t)(o - 0x10), v);
        else if (o >= 0x30 && o <= 0x3f) gb_apu_wave_write(&S.apu, (uint8_t)(o & 0xf), v);
        break;
    }
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff) return read_rom(a);
    if (a <= 0x9fff) return gb_ppu_vram_read(&S.ppu, (uint16_t)(a - 0x8000));
    if (a <= 0xbfff) return read_ram((uint16_t)(a - 0xa000));
    if (a <= 0xcfff) return S.wram[0][a & 0xfff];
    if (a <= 0xdfff) return S.wram[S.wram_bank][a & 0xfff];
    if (a <= 0xefff) return S.wram[0][a & 0xfff];
    if (a <= 0xfdff) return S.wram[S.wram_bank][a & 0xfff];
    if (a <= 0xfe9f) return S.ppu.oam[a & 0xff];
    if (a <= 0xfeff) {
        if (!S.is_cgb) return 0;
        const uint8_t lo = (uint8_t)a;
        return lo <= 0xcf ? S.cgb_oam_unused[lo - 0xa0] : S.cgb_oam_unused[0x20 + (lo & 0x0f)];
    }
    return read_io((uint8_t)a);
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) { write_register(a, v); return; }
    if (a <= 0x9fff) { gb_ppu_vram_write(&S.ppu, (uint16_t)(a - 0x8000), v); return; }
    if (a <= 0xbfff) { write_ram((uint16_t)(a - 0xa000), v); return; }
    if (a <= 0xcfff) { S.wram[0][a & 0xfff] = v; return; }
    if (a <= 0xdfff) { S.wram[S.wram_bank][a & 0xfff] = v; return; }
    if (a <= 0xefff) { S.wram[0][a & 0xfff] = v; return; }
    if (a <= 0xfdff) { S.wram[S.wram_bank][a & 0xfff] = v; return; }
    if (a <= 0xfe9f) { S.ppu.oam[a & 0xff] = v; return; }
    if (a <= 0xfeff) {
        if (!S.is_cgb) return;
        const uint8_t lo = (uint8_t)a;
        if (lo <= 0xcf) S.cgb_oam_unused[lo - 0xa0] = v;
        else S.cgb_oam_unused[0x20 + (lo & 0x0f)] = v;
        return;
    }
    write_io((uint8_t)a, v);
}

static void run_line_checkpoint_zero(void)
{
    bool lcd_compare = false, lcd_mode = false;
    if (S.line == S.ly_compare) {
        lcd_compare = (S.stat & 0x40) != 0;
        S.stat |= 0x04;
    } else {
        S.stat &= 0xfb;
    }
    if (S.line < 144) {
        lcd_mode = (S.stat & 0x20) != 0;
        S.stat = (uint8_t)((S.stat & 0xfc) | 0x02);
    }
    if (S.line == 144) {
        lcd_mode = (S.stat & 0x10) != 0;
        S.stat = (uint8_t)((S.stat & 0xfc) | 0x01);
    }
    if (lcd_compare || lcd_mode) S.cpu.lcdstat_req = true;
    S.hdma_done_this_line = false;
}

static void cycles(void *u, int n)
{
    (void)u;
    lr35902 *c = &S.cpu;
    S.div_counter = (uint16_t)(S.div_counter + n);
    if (S.tac & 4) {
        static const int kPeriods[4] = { 1024, 16, 64, 256 };
        S.timer_cycles += n;
        const int period = kPeriods[S.tac & 3];
        while (S.timer_cycles >= period) {
            S.timer_cycles -= period;
            if (++S.tima == 0) {
                S.tima = S.tma;
                c->timer_req = true;
            }
        }
    }
    if (S.oam_dma_remaining > 0) S.oam_dma_remaining -= S.is_cgb ? (n >> c->speed) : n;
    if (S.is_cgb && S.wram[0][0x1a4] == S.wram[1][0x1a4]) S.wram[0][0x1a4] = 0xed;
    if (c->changed_speed) c->changed_speed = false;
    S.audio_acc += (uint64_t)(n >> c->speed) * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        const int16_t s = gb_apu_update(&S.apu);
        if (S.naudio < 1024) S.audio[S.naudio++] = s;
    }
    if (!(S.ppu.lcdc & 0x80)) {
        if (S.is_cgb && S.hdma_active) hdma_block();
        return;
    }
    const int prev = S.line_cycles;
    int cur = prev + n;
    const int ds = 1 << c->speed;
    const int prev_c = prev / ds;
    int cur_c = cur / ds;
    bool lcd_mode = false;
    if (prev_c < 80 && cur_c >= 80) {
        if (S.line < 144) {
            lcd_mode = (S.stat & 0x20) && !(S.stat & 0x10);
            S.stat = (uint8_t)((S.stat & 0xfc) | 0x03);
        }
        if (S.line == 144) c->vblank_req = true;
    }
    if (S.line < 144 && cur_c >= 248 && cur_c <= 600) {
        if (S.is_cgb && !S.hdma_done_this_line && cur_c >= 300 && S.hdma_active) {
            S.hdma_done_this_line = true;
            hdma_block();
            cur += 8;
            cur_c = cur / ds;
        }
        const int threshold = S.sprites_time + 248;
        if (prev_c < threshold && cur_c >= threshold && (S.stat & 3)) {
            lcd_mode = (S.stat & 0x08) && !(S.stat & 0x20);
            S.stat &= 0xfc;
        }
    }
    if (lcd_mode) c->lcdstat_req = true;
    S.line_cycles = cur;
}

static void post_boot(void)
{
    lr35902 *c = &S.cpu;
    c->pc = 0x100;
    c->sp = 0xfffe;
    c->fz = true;
    c->fn = false;
    if (S.is_cgb) {
        c->a = 0x11; c->fh = c->fc = false;
        c->b = 0x00; c->c = 0x00; c->d = 0xff; c->e = 0x56; c->h = 0x00; c->l = 0x0d;
    } else {
        c->a = 0x01; c->fh = c->fc = true;
        c->b = 0x00; c->c = 0x13; c->d = 0x00; c->e = 0xd8; c->h = 0x01; c->l = 0x4d;
    }
    static const uint8_t io[][2] = {
        {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00}, {0x10, 0x80}, {0x11, 0xbf}, {0x12, 0xf3}, {0x14, 0xbf},
        {0x16, 0x3f}, {0x17, 0x00}, {0x19, 0xbf}, {0x1a, 0x7f}, {0x1b, 0x0f}, {0x1c, 0x9f}, {0x1e, 0xbf},
        {0x20, 0xff}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0xbf}, {0x24, 0x77}, {0x25, 0xf3}, {0x26, 0xf1},
        {0x40, 0x91}, {0x42, 0x00}, {0x43, 0x00}, {0x45, 0x00}, {0x47, 0xfc}, {0x48, 0xff}, {0x49, 0xff},
        {0x4a, 0x00}, {0x4b, 0x00}, {0x00, 0x00},
    };
    for (size_t i = 0; i < sizeof io / sizeof io[0]; i++) write_io(io[i][0], io[i][1]);
}

bool cons_init(void)
{
    S.fb = (uint16_t *)calloc(GB_W * GB_H, sizeof(uint16_t));
    if (!S.fb) return false;
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.cycles = cycles;
    gb_apu_init(&S.apu);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    lr35902_reset(&S.cpu);
    gb_apu_reset(&S.apu);
    gb_ppu_reset(&S.ppu, S.is_cgb);
    mapper_reset();
    memset(S.wram, 0, sizeof S.wram);
    memset(S.io_ram, 0, sizeof S.io_ram);
    S.wram_bank = 1;
    S.div_counter = 0;
    S.tima = S.tma = S.tac = 0;
    S.timer_cycles = 0;
    S.stat = 0;
    S.ly_compare = 0xff;
    S.line = S.line_cycles = S.sprites_time = 0;
    S.oam_dma_remaining = 0;
    S.dma_src = S.dma_dst = 0;
    S.hdma_active = false;
    S.hdma_size = 0xff;
    memset(S.cgb_oam_unused, 0, sizeof S.cgb_oam_unused);
    S.joy_select = 0x30;
    S.joy_val = S.joystick = 0xff;
    S.audio_acc = 0;
    S.debt = 0;
    S.ppu.lcdc = 0x80;
    S.ppu.window_line = 0;
    post_boot();
}

static size_t ram_size_for(uint8_t code)
{
    switch (code) {
    case 1: return 0x800;
    case 2: return 0x2000;
    case 3: return 0x8000;
    case 4: return 0x20000;
    case 5: return 0x10000;
    default: return 0;
    }
}

static bool fail(uint8_t *data, const char **why, const char *msg)
{
    if (why) *why = msg;
    free(data);
    return false;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || size < 0x150) return fail(data, why, "not a valid Game Boy cartridge (too small)");
    if (size > MAX_CART) size = MAX_CART;
    const bool unlicensed = memcmp(data + 0x104, kNintendoLogo, sizeof kNintendoLogo) != 0;
    const uint8_t header_type = data[0x147], ram_code = data[0x149];
    if (unlicensed && header_type == 0 && data[0x148] == 0 && size > 32768)
        return fail(data, why, "Wisdom Tree / blank-header oversized carts are not supported");
    int type;
    bool battery = false, rtc = false;
    size_t ram_bytes = ram_size_for(ram_code);
    switch (header_type) {
    case 0x00: type = MBC_NONE; break;
    case 0x01: case 0x02: type = MBC1; break;
    case 0x03: type = MBC1; battery = true; break;
    case 0x05: type = MBC2; ram_bytes = 0x200; break;
    case 0x06: type = MBC2; ram_bytes = 0x200; battery = true; break;
    case 0x0f: case 0x10: type = MBC3; battery = rtc = true; break;
    case 0x11: case 0x12: type = MBC3; break;
    case 0x13: type = MBC3; battery = true; break;
    case 0x19: case 0x1a: case 0x1c: case 0x1d: type = MBC5; break;
    case 0x1b: case 0x1e: type = MBC5; battery = true; break;
    default: {
        static char msg[40];
        snprintf(msg, sizeof msg, "mapper $%02x is not supported", header_type);
        return fail(data, why, msg);
    }
    }
    uint8_t *ram = ram_bytes ? (uint8_t *)malloc(ram_bytes) : NULL;
    if (ram_bytes && !ram) return fail(data, why, "out of memory");
    if (ram) memset(ram, 0xff, ram_bytes);
    const uint32_t crc = cons_crc32(data, size);
    switch (crc) {                       // multicarts with a 4-bit MBC1 bank field
    case 0xb91d6c8d: case 0x509a6b73: case 0xf724b5ce: case 0xb1a8dfd0:
    case 0x339f1694: case 0xad376905: case 0x7d1d8fdc: case 0x018b4a02:
        S.mbc1_mask = 0x0f;
        S.mbc1_shift = 4;
        break;
    default:
        S.mbc1_mask = 0x1f;
        S.mbc1_shift = 5;
        break;
    }
    free(S.rom);
    free(S.ram);
    S.rom = data;
    S.rom_size = size;
    S.rom_banks = size / 0x4000 < 2 ? 2 : size / 0x4000;
    S.ram = ram;
    S.ram_size = ram_bytes;
    S.type = type;
    S.has_battery = battery;
    S.has_rtc = rtc;
    S.is_cgb = (data[0x143] & 0x80) != 0;
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(S.rom);
    free(S.ram);
    S.rom = S.ram = NULL;
    S.rom_size = S.ram_size = 0;
    S.type = MBC_NONE;
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    for (S.line = 0; S.line < LINES; S.line++) {
        S.line_cycles = 0;
        if (S.ppu.lcdc & 0x80) run_line_checkpoint_zero();
        const int want = (LINE_CYCLES << S.cpu.speed) - S.debt;
        S.debt = want > 0 ? lr35902_run(&S.cpu, want) - want : -want;
        if (S.line < 144) {
            S.ppu.oam_dma = S.oam_dma_remaining > 0;
            gb_ppu_render_scanline(&S.ppu, S.line, S.fb + S.line * GB_W);
            if (S.ppu.lcdc & 0x80) {
                if ((S.ppu.lcdc & 2) && S.oam_dma_remaining <= 0)
                    S.sprites_time = gb_ppu_sprite_mode3_penalty(&S.ppu, S.line, S.cpu.speed);
                if (gb_ppu_window_visible_on(&S.ppu, S.line)) S.ppu.window_line++;
            }
        }
    }
    S.ppu.window_line = 0;
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = GB_W;
    *h = GB_H;
    *stride = GB_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    if (player) return;
    uint8_t v = 0xff;
    if (b & CP_RIGHT) v &= 0xfe;
    if (b & CP_LEFT) v &= 0xfd;
    if (b & CP_UP) v &= 0xfb;
    if (b & CP_DOWN) v &= 0xf7;
    if (b & CP_B1) v &= 0xef;
    if (b & CP_B2) v &= 0xdf;
    if (b & (CP_SELECT | CP_B3 | CP_COIN)) v &= 0xbf;
    if (b & CP_START) v &= 0x7f;
    S.joy_val = v;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return S.loaded ? (S.is_cgb ? "Game Boy Color" : "Game Boy") : NULL; }
