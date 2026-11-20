// exelv_core.c - Exelvision EXL-100 / EXELTEL (see exelv_core.h).
#include "exelv_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exelv_tape.h"
#include "../../../cpu/tms7000/tms7000.h"
#include "../../../sound/tms5220.h"
#include "../../../video/tms3556.h"
#include "../../consoles/common/cons.h"

#define EXL100_CRYSTAL 4915200
#define EXELTEL_CRYSTAL 9830400
#define FPS 50

static struct {
    int model;
    tms7000 maincpu, subcpu;
    tms3556 vdp;
    tms5220 speech;
    uint8_t *vram, *fb;
    uint8_t main_rom[0x1000], sub_rom[0x1000];
    uint8_t *sys_rom;                   // EXELTEL: 64 KB, or NULL
    uint8_t *cart;
    size_t cart_size;
    uint8_t ram[0x800];

    uint8_t tms7020_portb, tms7041_portb, tms7041_portc, tms7041_portd;
    uint8_t wx318, wx319;
    bool sub_present, hle_io_sent;
    int hle_io_delay, main_debt, sub_debt;
    bool page_bit1, page_bit2;
    uint8_t p64, last_sent, last_key;

    exl_tape tape;
    bool tape_playing, tape_fast;
    uint64_t main_cycles;
    int tape_idle_frames;
    uint8_t *saved;
    size_t saved_n;

    int chord_phase;                    // 1: sending the modifier, 2: the key
    uint8_t chord_mod, chord_key;
    bool keys[EK_COUNT];
    uint8_t joy;
    uint8_t k_channels[3];
    uint8_t k_ch_byte, k_ch_bit;
    bool k_bit_bit, k_bit_num, k_started;
    int k_timer_cycles;
    int64_t k_boot_cycles;

    int64_t audio_acc;
    int16_t audio[EXELV_AUDIO_MAX];
    int naudio;
    int cass_bit;
} S;

static uint32_t argb[8];
static uint16_t c565[8];

// ---- the main CPU's bus ---------------------------------------------------------

static int exeltel_page(void)
{
    // bit 0: port B bit 2 (inverted); bits 1 and 2 set by reading P56 / P57,
    // cleared by writing them, counted only while P64 bit 6 is set
    int page = (S.tms7020_portb & 0x04) ? 0 : 1;
    if (S.p64 & 0x40) page |= (S.page_bit1 ? 2 : 0) | (S.page_bit2 ? 4 : 0);
    return page;
}

static uint8_t cart_r(uint16_t offset)
{
    if (S.model == EXELV_EXELTEL) {
        // page 2: the upper half of the system ROM, page 3 the lower half;
        // an EXL-100 cartridge answers on page 6
        const int page = exeltel_page();
        if (page == 2 || page == 3) {
            const size_t a = (size_t)offset + 0x200 + (page == 2 ? 0x8000 : 0);
            return S.sys_rom && a < 0x10000 ? S.sys_rom[a] : 0xff;
        }
        if (page != 6) return 0xff;
    }
    if (!S.cart_size) return 0xff;
    // linear, smaller ROMs mirrored across $0200-$7FFF; a $7E00-byte image starts at $0200
    if (S.cart_size == 0x7e00) return S.cart[offset % S.cart_size];
    return S.cart[((size_t)offset + 0x200) % S.cart_size];
}

static uint8_t read_main(void *u, uint16_t a)
{
    (void)u;
    if (a == 0x0124) return tms3556_vram_r(&S.vdp);
    if (a == 0x0125) return tms3556_reg_r(&S.vdp);
    if (a == 0x0128) return tms3556_initptr_r(&S.vdp);
    if (a == 0x0130) return S.wx319;
    if (S.model == EXELV_EXELTEL) {
        if (a == 0x0138) { S.page_bit1 = true; return 0xff; }
        if (a == 0x0139) { S.page_bit2 = true; return 0xff; }
        if (a == 0x0140) return S.p64;
    }
    if (a >= 0x0200 && a <= 0x7fff) return cart_r((uint16_t)(a - 0x0200));
    if (a >= 0xc000 && a <= 0xc7ff) return S.ram[a - 0xc000];
    return 0xff;
}

static void write_main(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a == 0x012d) { tms3556_reg_w(&S.vdp, v); return; }
    if (a == 0x012e) { tms3556_vram_w(&S.vdp, v); return; }
    if (a == 0x0130) { S.wx318 = v; return; }
    if (S.model == EXELV_EXELTEL) {
        if (a == 0x0138) { S.page_bit1 = false; return; }
        if (a == 0x0139) { S.page_bit2 = false; return; }
        if (a == 0x0140) { S.p64 = v; return; }
    }
    if (a >= 0xc000 && a <= 0xc7ff) S.ram[a - 0xc000] = v;
}

static bool in_tape_read(void)
{
    const uint16_t pc = S.maincpu.pc;      // the TRAP 14 code (load, save, bit timing)
    return S.model == EXELV_EXL100 ? pc >= 0xfca1 && pc < 0xfea0 : pc >= 0xfac0 && pc < 0xfcdb;
}

static bool in_tape_write(void)
{
    const uint16_t pc = S.maincpu.pc;
    return S.model == EXELV_EXL100 ? pc >= 0xfde5 && pc < 0xfe10 : pc >= 0xfc20 && pc < 0xfc4b;
}

static uint8_t main_port_in(void *u, int port)
{
    (void)u;
    if (port != TMS7000_PORTA) return 0xff;
    uint8_t d = (S.tms7041_portb & 0x80) ? 0x01 : 0x00;
    if (!exl_tape_loaded(&S.tape) || S.tape.level) d |= 0x10;     // PA.4: the cassette, high when idle
    return d;
}

static void main_port_out(void *u, int port, uint8_t d)
{
    (void)u;
    if (port != TMS7000_PORTB) return;
    // every write of the ROM's byte writer is a half-period boundary
    if (in_tape_write()) exl_tape_record_edge(&S.tape, S.main_cycles);
    S.tms7020_portb = d;
    // with no I/O CPU, "byte read" (PB.1) is acknowledged on PA.0 as the 7041 does
    if (!S.sub_present) S.tms7041_portb = (uint8_t)((S.tms7041_portb & 0x7f) | ((d & 0x02) ? 0x80 : 0));
    S.cass_bit = (d & 0x08) ? -1 : 1;
}

// ---- the I/O CPU's ports ------------------------------------------------------------

static uint8_t sub_port_in(void *u, int port)
{
    (void)u;
    switch (port) {
    case TMS7000_PORTA: {
        uint8_t d = 0;
        d |= tms5220_intq(&S.speech) ? 0x08 : 0;        // A3: /INT
        d |= tms5220_readyq(&S.speech) ? 0x80 : 0;      // A7: /READY
        d |= (S.tms7020_portb & 0x01) ? 0x04 : 0;
        d |= (S.tms7020_portb & 0x02) ? 0x10 : 0;
        return d;
    }
    case TMS7000_PORTC: return (S.tms7041_portb & 0x20) ? 0xff : S.wx318;
    case TMS7000_PORTD: return tms5220_status(&S.speech);
    default: return 0xff;
    }
}

static void sub_port_out(void *u, int port, uint8_t d)
{
    (void)u;
    switch (port) {
    case TMS7000_PORTB:
        tms5220_strobe_ws_rs(&S.speech, d & 0x03);
        if ((S.tms7041_portb & 0x04) && !(d & 0x04)) tms7000_set_input_line(&S.maincpu, TMS7000_INT1, TMS7000_HOLD);
        if (!(S.tms7041_portb & 0x40) && (d & 0x40)) {
            S.wx319 = S.tms7041_portc;
            if (S.last_sent == 0x01 && S.wx319 != 0x04) S.last_key = S.wx319;
            S.last_sent = S.wx319;
        }
        S.tms7041_portb = d;
        break;
    case TMS7000_PORTC: S.tms7041_portc = d; break;
    case TMS7000_PORTD:
        tms5220_set_data_latch(&S.speech, d);
        S.tms7041_portd = d;
        break;
    default: break;
    }
}

// ---- the cassette and the sound -------------------------------------------------------

static void fast_load_hook(void)
{
    // the ROM's TRAP 14: the leader / sync search, the instruction after
    // the $70 sync byte, the read-byte routine
    const bool exl = S.model == EXELV_EXL100;
    const uint16_t sync_start = exl ? 0xfca8 : 0xfadc, after_sync = exl ? 0xfcfe : 0xfb32;
    const uint16_t read_byte = exl ? 0xfe82 : 0xfcbd;
    tms7000 *c = &S.maincpu;
    if (c->pc == sync_start) {
        if (exl_tape_seek_sync(&S.tape)) c->pc = after_sync;
    } else if (c->pc == read_byte) {
        const int v = exl_tape_next_byte(&S.tape);
        if (v < 0) return;                  // the end: the ROM times out
        tms7000_set_a(c, (uint8_t)v);
        // RETS: the return address's low byte on top of the stack
        const uint8_t sp = c->sp;
        c->pc = (uint16_t)((tms7000_ram_at(c, (uint8_t)(sp - 1)) << 8) | tms7000_ram_at(c, sp));
        c->sp = (uint8_t)(sp - 2);
    }
}

static void on_main_cycles(void *u, int cycles)
{
    (void)u;
    S.main_cycles += (uint64_t)cycles;
    if (S.tape_fast && S.tape_playing && exl_tape_loaded(&S.tape)) fast_load_hook();
    if (S.tape_playing && exl_tape_loaded(&S.tape) && in_tape_read()) exl_tape_advance(&S.tape, cycles);
    tms5220_tick(&S.speech, cycles);
    const uint32_t clk = tms7000_cpu_clock(&S.maincpu);
    S.audio_acc += (int64_t)cycles * EXELV_AUDIO_RATE;
    while (S.audio_acc >= clk) {
        S.audio_acc -= clk;
        int32_t s = tms5220_update(&S.speech, EXELV_AUDIO_RATE);
        s += S.cass_bit * 800;
        if (S.naudio < EXELV_AUDIO_MAX) S.audio[S.naudio++] = (int16_t)(s < -32768 ? -32768 : s > 32767 ? 32767 : s);
    }
}

// ---- the infrared keyboard ----------------------------------------------------------------

static uint8_t scan_key_channel(void)
{
    // the modifiers (SHIFT, CTL, FCT) are pressed and released before the
    // key; a chord becomes the modifier's channel once, then the key's
    uint8_t modifier = 0xff, key = 0xff;
    for (int ch = 0; ch < 62; ch++) {
        if (!S.keys[ch] || ch == EK_NONE45) continue;
        if (ch == EK_SHIFT || ch == EK_CTL || ch == EK_FCT) {
            if (modifier == 0xff) modifier = (uint8_t)ch;
        } else if (key == 0xff) {
            key = (uint8_t)ch;
        }
    }
    if (key == 0xff) {
        if (S.joy & EXELV_JOY_UP) key = EK_UP;
        else if (S.joy & EXELV_JOY_RIGHT) key = EK_RIGHT;
        else if (S.joy & EXELV_JOY_DOWN) key = EK_DOWN;
        else if (S.joy & EXELV_JOY_LEFT) key = EK_LEFT;
        else if (S.joy & EXELV_JOY_FIRE) key = EK_SPACE;
    }
    if (S.chord_phase == 2 && S.chord_key != 0xff) return S.chord_key;
    if (key == 0xff || modifier == 0xff) {
        if (S.chord_phase == 1 && S.chord_key != 0xff) return S.chord_mod;
        S.chord_phase = 0;
        return key != 0xff ? key : modifier;
    }
    if (S.chord_phase == 0) {
        S.chord_phase = 1;
        S.chord_mod = modifier;
        S.chord_key = key;
    }
    return S.chord_phase == 1 ? S.chord_mod : key;
}

static void assert_ir(bool on) { tms7000_set_input_line(&S.subcpu, TMS7000_INT1, on ? TMS7000_ASSERT : TMS7000_CLEAR); }
static void wait_us(int us) { S.k_timer_cycles += (int)((int64_t)us * tms7000_cpu_clock(&S.subcpu) / 1000000); }

static void tick_keyboard(int cpu_cycles)
{
    if (!S.sub_present) return;
    S.k_boot_cycles += cpu_cycles;
    if (!S.k_started) {
        if (S.k_boot_cycles < (int64_t)tms7000_cpu_clock(&S.subcpu) * 2) return;
        S.k_started = true;
        S.k_timer_cycles = 0;
    }
    S.k_timer_cycles -= cpu_cycles;
    if (S.k_timer_cycles > 0) return;

    if (S.k_ch_byte < 2) {
        S.k_channels[0] = scan_key_channel();
        if (S.k_channels[0] != 0xff && S.k_ch_byte == 0) {
            if (S.k_channels[1] == 0xff) S.k_channels[1] = S.k_channels[0];
            S.k_ch_bit = 0;
            S.k_bit_num = false;
            S.k_ch_byte = 1;
        }
    }
    if (S.k_ch_byte == 0) { wait_us(25000); return; }

    if (S.k_ch_bit == 0 || S.k_ch_bit == 1) {
        if (!S.k_bit_num) {
            assert_ir(true);
            S.k_bit_num = true;
            wait_us(540);
        } else {
            assert_ir(false);
            S.k_bit_num = false;
            wait_us(S.k_ch_bit == 0 ? 2840 : 590);
            S.k_ch_bit++;
        }
        return;
    }
    if (S.k_ch_bit == 8) {
        // a chord's modifier is sent once and released before the key
        if (S.k_ch_byte == 1 && S.chord_phase == 1) {
            S.chord_phase = 2;
            S.k_channels[0] = 0xff;
        } else if (S.k_ch_byte == 1 && S.chord_phase == 2 && S.k_channels[1] == S.chord_key) {
            S.chord_key = 0xff;             // sent: from now on it repeats only while held
            S.k_channels[0] = scan_key_channel();
        }
        assert_ir(false);
        S.k_ch_bit = 0;
        if (S.k_ch_byte == 1) {
            if (S.k_channels[0] < 0xff) {
                wait_us(90000);
            } else {
                S.k_ch_byte = 2;
                wait_us(3000);
            }
        } else if (S.k_ch_byte == 2) {
            S.k_channels[1] = 0xff;
            S.k_ch_byte = 0;
            wait_us(20000);
        }
        return;
    }
    if (!S.k_bit_num) {
        S.k_bit_bit = (S.k_channels[S.k_ch_byte] >> (S.k_ch_bit - 2)) & 1;
        assert_ir(S.k_bit_bit);
        S.k_bit_num = true;
        wait_us(590);
        return;
    }
    assert_ir(!S.k_bit_bit);
    S.k_bit_num = false;
    S.k_ch_bit++;
    wait_us(540);
}

void exelv_key(int code, bool down)
{
    if (code >= 0 && code < EK_COUNT) S.keys[code] = down;
}

void exelv_keys_clear(void) { memset(S.keys, 0, sizeof S.keys); }
void exelv_joy(uint8_t bits) { S.joy = bits; }

// ---- the frame --------------------------------------------------------------------------

int exelv_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    const uint32_t clk = tms7000_cpu_clock(&S.maincpu);
    const int per_line = (int)((double)clk / (FPS * TMS3556_LINES) + 0.5);
    for (int line = 0; line < TMS3556_LINES; line++) {
        tms3556_line(&S.vdp);
        int remain = per_line;
        while (remain > 0) {
            const int slice = remain < 16 ? remain : 16;
            // the overrun carried, so neither CPU runs ahead (the IR decoder
            // times its bits with timer 1)
            if (slice > S.main_debt) S.main_debt += tms7000_run(&S.maincpu, slice - S.main_debt);
            S.main_debt -= slice;
            if (S.sub_present) {
                if (slice > S.sub_debt) S.sub_debt += tms7000_run(&S.subcpu, slice - S.sub_debt);
                S.sub_debt -= slice;
            }
            tick_keyboard(slice);
            remain -= slice;
        }
        // with no I/O CPU ROM: its "initialized" byte ($08)
        if (!S.sub_present && !S.hle_io_sent) {
            S.hle_io_delay -= per_line;
            if (S.hle_io_delay <= 0) {
                S.wx319 = 0x08;
                S.tms7041_portb &= (uint8_t)~0x80;
                tms7000_set_input_line(&S.maincpu, TMS7000_INT1, TMS7000_HOLD);
                S.hle_io_sent = true;
            }
        }
    }
    // a SAVE is complete once the ROM has left the tape routine for a second
    if (exl_tape_recording(&S.tape)) {
        if (in_tape_read()) {
            S.tape_idle_frames = 0;
        } else if (++S.tape_idle_frames >= FPS) {
            size_t n = 0;
            uint8_t *b = exl_tape_take_recording(&S.tape, &n);
            if (b) {
                uint8_t *p = (uint8_t *)realloc(S.saved, S.saved_n + n);
                if (p) {
                    memcpy(p + S.saved_n, b, n);
                    S.saved = p;
                    S.saved_n += n;
                }
                free(b);
            }
            S.tape_idle_frames = 0;
        }
    }
    *audio = S.audio;
    return S.naudio;
}

uint8_t *exelv_take_tape_save(size_t *n)
{
    uint8_t *b = S.saved;
    *n = S.saved_n;
    S.saved = NULL;
    S.saved_n = 0;
    return b;
}

double exelv_fps(void) { return FPS; }
int exelv_model(void) { return S.model; }

void exelv_render_row(int y, uint32_t *out)
{
    const uint8_t *s = S.fb + y * EXELV_W;
    for (int x = 0; x < EXELV_W; x++) out[x] = argb[s[x] & 7];
}

void exelv_render_row565(int y, uint16_t *out)
{
    const uint8_t *s = S.fb + y * EXELV_W;
    for (int x = 0; x < EXELV_W; x++) out[x] = c565[s[x] & 7];
}

// ---- set up ---------------------------------------------------------------------------------

void exelv_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    S.tms7020_portb = S.tms7041_portb = S.tms7041_portc = S.tms7041_portd = 0;
    S.wx318 = S.wx319 = 0;
    S.main_debt = S.sub_debt = 0;
    S.page_bit1 = S.page_bit2 = false;
    S.last_sent = S.last_key = 0;
    exl_tape_rewind(&S.tape);
    S.tape_idle_frames = 0;
    S.chord_phase = 0;
    S.chord_key = S.chord_mod = 0xff;
    S.p64 = 0;
    S.hle_io_sent = false;
    S.hle_io_delay = (int)(tms7000_cpu_clock(&S.maincpu) / 5);
    S.k_channels[0] = S.k_channels[1] = 0xff;
    S.k_channels[2] = 0x3e;
    S.k_ch_byte = S.k_ch_bit = 0;
    S.k_bit_bit = S.k_bit_num = S.k_started = false;
    S.k_timer_cycles = 0;
    S.k_boot_cycles = 0;
    S.audio_acc = 0;
    S.naudio = 0;
    S.cass_bit = 1;
    tms3556_reset(&S.vdp);
    tms5220_reset(&S.speech);
    tms7000_reset(&S.maincpu);
    if (S.sub_present) {
        tms7000_reset(&S.subcpu);
        tms7000_set_input_line(&S.subcpu, TMS7000_INT1, TMS7000_CLEAR);
        tms7000_set_input_line(&S.subcpu, TMS7000_INT3, TMS7000_CLEAR);
    }
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

// MAME's EXELTEL 7042 is a BAD_DUMP: the EXL-100 7041 program with its
// second and third KB swapped. Swapped back, and with the EXELTEL's two
// differences (its font request from character 1; command $01 a NOP), it
// is a working I/O CPU (as dsp-cpp does).
static void fix_7042(uint8_t *r)
{
    if (crc32(r, 0x1000) != 0xa0163507u) return;
    uint8_t tmp[0x400];
    memcpy(tmp, r + 0x400, 0x400);
    memcpy(r + 0x400, r + 0x800, 0x400);
    memcpy(r + 0x800, tmp, 0x400);
    if (r[0x2eb] == 0x88 && r[0x2ec] == 0xf7 && r[0x2ed] == 0x22) r[0x2ed] = 0x2c;
    if (r[0x1e7] == 0xf3 && r[0x1e8] == 0x61 && r[0x247] == 0x73 && r[0x248] == 0xef && r[0x24a] == 0x0a) {
        r[0x1e7] = 0xf2;
        r[0x1e8] = 0x47;
    }
}

bool exelv_init(int model, const uint8_t *main_rom, const uint8_t *sub_rom, const uint8_t *sys_rom)
{
    if (!main_rom) return false;
    for (int c = 0; c < 8; c++) {
        argb[c] = tms3556_rgb((uint8_t)c);
        c565[c] = (uint16_t)(((c & 1) ? 0xf800 : 0) | ((c & 2) ? 0x07e0 : 0) | ((c & 4) ? 0x001f : 0));
    }
    S.model = model == EXELV_EXELTEL ? EXELV_EXELTEL : EXELV_EXL100;
    if (!S.vram) S.vram = (uint8_t *)malloc(0x10000);
    if (!S.fb) S.fb = (uint8_t *)malloc(EXELV_W * EXELV_H);
    if (!S.vram || !S.fb) return false;
    const bool exl = S.model == EXELV_EXL100;
    memcpy(S.main_rom, main_rom, exl ? 0x800 : 0x1000);
    S.sub_present = sub_rom != NULL;
    if (sub_rom) {
        memcpy(S.sub_rom, sub_rom, 0x1000);
        if (!exl) fix_7042(S.sub_rom);
    }
    free(S.sys_rom);
    S.sys_rom = NULL;
    if (!exl && sys_rom) {
        S.sys_rom = (uint8_t *)malloc(0x10000);
        if (S.sys_rom) memcpy(S.sys_rom, sys_rom, 0x10000);
    }
    const uint32_t crystal = exl ? EXL100_CRYSTAL : EXELTEL_CRYSTAL;
    const unsigned div = exl ? 2 : 4;
    tms7000 *m = &S.maincpu, *s = &S.subcpu;
    memset(m, 0, sizeof *m);
    m->read = read_main;
    m->write = write_main;
    m->port_in = main_port_in;
    m->port_out = main_port_out;
    m->cycles = on_main_cycles;
    m->user = &S;
    tms7000_init(m, crystal, exl ? TMS7020 : TMS7040, div);
    m->exl_lvdp = true;
    tms7000_set_rom(m, S.main_rom, exl ? 0x800 : 0x1000);
    memset(s, 0, sizeof *s);
    s->port_in = sub_port_in;
    s->port_out = sub_port_out;
    s->user = &S;
    tms7000_init(s, crystal, exl ? TMS7041 : TMS7042, div);
    if (S.sub_present) tms7000_set_rom(s, S.sub_rom, 0x1000);
    tms3556_init(&S.vdp, S.vram, S.fb);
    tms5220_init(&S.speech, 640000, false);
    S.tape_playing = true;
    S.tape_fast = true;
    memset(S.keys, 0, sizeof S.keys);
    S.joy = 0;
    exelv_reset();
    return true;
}

// ---- media ------------------------------------------------------------------------------------

const char *exelv_insert_cart(const uint8_t *data, size_t size)
{
    if (!size) return "empty cartridge";
    uint8_t *p = (uint8_t *)malloc(size);
    if (!p) return "out of memory";
    memcpy(p, data, size);
    free(S.cart);
    S.cart = p;
    S.cart_size = size;
    return NULL;
}

void exelv_eject_cart(void)
{
    free(S.cart);
    S.cart = NULL;
    S.cart_size = 0;
}

bool exelv_cart_inserted(void) { return S.cart_size > 0; }
const char *exelv_insert_tape(const uint8_t *data, size_t size) { return exl_tape_load(&S.tape, data, size); }
void exelv_eject_tape(void) { exl_tape_eject(&S.tape); }
bool exelv_tape_inserted(void) { return exl_tape_loaded(&S.tape); }
void exelv_tape_play(bool on) { S.tape_playing = on; }
bool exelv_tape_playing(void) { return S.tape_playing; }

static bool ends_with(const char *s, const char *ext)
{
    const size_t n = strlen(s), m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

bool exelv_is_tape(const char *name) { return ends_with(name, ".k7") || ends_with(name, ".wav"); }

const char *exelv_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (16L << 20)) { fclose(f); return "not a cartridge or a cassette"; }
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data) { fclose(f); return "out of memory"; }
    const bool ok = fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) { free(data); return "read error"; }
    char name[256];
    snprintf(name, sizeof name, "%s", path);
    size_t n = (size_t)size;
    static const char *const exts[] = EXELV_MEDIA_EXTS;
    size_t un = 0;
    uint8_t *inner = cons_unzip(data, n, exts, &un, name, sizeof name);
    if (inner) {
        free(data);
        data = inner;
        n = un;
    }
    const char *err;
    if (exelv_is_tape(name)) {
        err = exelv_insert_tape(data, n);
    } else {
        err = exelv_insert_cart(data, n);
        if (!err) exelv_reset();
    }
    free(data);
    return err;
}
