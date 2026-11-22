// st_core.c - Atari 1040ST (see st_core.h). The machine logic follows
// dsp-cpp's atari_st.cpp; dsp-cpp runs its chips after every instruction,
// here the 68000 runs in slices that end where the next chip event is (an
// MFP timer, the floppy's INTRQ, the next IKBD byte), so interrupts still
// come on the right instruction.
#include "st_core.h"
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "st_mfp.h"
#include "st_floppy.h"
#include "../../../sound/ay8910.h"

#define RAM_SIZE 0x100000u
#define LINES 313
#define LINE_CYCLES 512
#define IKBD_QUEUE 512

#ifndef ST_HOT
#define ST_HOT
#endif

typedef struct { uint8_t value; int32_t cycles; } ikbd_byte_t;

enum { MOUSE_REL, MOUSE_ABS, MOUSE_KEYCODE, MOUSE_OFF };
enum { JOY_EVENT, JOY_INTERROGATE, JOY_MONITOR, JOY_OFF };

static struct {
    uint8_t *ram;
    const uint8_t *rom;
    uint32_t rom_size;
    uint16_t *fb;
    bool half;
    int fb_w, fb_h;

    mfp68901 mfp;
    st_floppy fdc;
    ay8910 psg;
    int prev_enable;
    uint8_t psg_port_a;

    bool rom_at_zero;
    uint8_t memcfg, video_hi, video_mid, video_lo, sync_mode, resolution;
    uint16_t palette[16];
    uint32_t video_count;

    // CPU scheduling
    bool in_cpu, ipl_pending, vbl_hold, hbl_hold;
    int cur_ipl, debt, slice;
    int64_t mfp_acc, audio_acc;
    int16_t audio[ST_AUDIO_MAX];
    int naudio;

    // ACIA + IKBD
    bool rx_full;
    uint8_t acia_rdr, acia_control;
    ikbd_byte_t pend[IKBD_QUEUE];
    int pend_head, pend_count;
    uint8_t cmd[16];
    int cmd_len, cmd_need;
    int mouse_mode, joy_mode;
    bool paused, y_bottom;
    uint8_t button_action;
    int abs_x, abs_y, abs_max_x, abs_max_y;
    uint8_t abs_buttons;
    bool abs_left, abs_right;
    uint8_t joy_state[2], joy_new[2];
    int joy_monitor_rate, joy_monitor_count;
    uint8_t clock[6];
    int clock_frames;
    bool keys[128];
    int mouse_dx, mouse_dy, frac_x, frac_y;
    bool mouse_left, mouse_right, last_left, last_right;

    // blitter
    uint16_t blit_halftone[16];
    int16_t blit_sxinc, blit_syinc, blit_dxinc, blit_dyinc;
    uint32_t blit_src, blit_dst;
    uint16_t blit_emask[3], blit_xcount, blit_ycount;
    uint8_t blit_hop, blit_op, blit_ctrl, blit_skew;
    bool blit_defer_start;
} S;

static void ST_HOT write_byte(uint32_t a, uint8_t v);

// ---------------------------------------------------------------------------
// Interrupts: VBL (4) and HBL (2) are held until acknowledged; the MFP is 6

static void end_slice(void)
{
    // (m68k_end_timeslice would return the wrong count of cycles run)
    m68k_modify_timeslice(-m68k_cycles_remaining());
}

static void update_ipl(void)
{
    const int ipl = S.mfp.irq ? 6 : S.vbl_hold ? 4 : S.hbl_hold ? 2 : 0;
    if (ipl == S.cur_ipl) return;
    if (S.in_cpu) {                     // between instructions: after this one
        S.ipl_pending = true;
        end_slice();
        return;
    }
    S.cur_ipl = ipl;
    m68k_set_irq((unsigned)ipl);
}

static void mfp_irq_changed(bool asserted)
{
    (void)asserted;
    update_ipl();
}

int cpu_irq_ack(int level)
{
    if (level == 6) return mfp_irq_ack(&S.mfp);     // (its line follows)
    if (level == 4) S.vbl_hold = false;
    else if (level == 2) S.hbl_hold = false;
    update_ipl();
    return M68K_INT_ACK_AUTOVECTOR;
}
void cpu_set_fc(int fc) { (void)fc; }
void cpu_instr_callback(unsigned int pc) { (void)pc; }

static void update_fdc_irq(void) { mfp_set_gpip_bit(&S.mfp, 5, S.fdc.fdc_irq ? 0 : 1); }

// ---------------------------------------------------------------------------
// PSG (its samples made up to the CPU's time before every register write)

static void psg_port_a(uint8_t v)
{
    S.psg_port_a = v;
    stf_set_psg_port_a(&S.fdc, v);
}

static inline int cpu_done(void) { return S.in_cpu ? m68k_cycles_run() : 0; }

static void psg_samples(int cycles)
{
    S.audio_acc += (int64_t)cycles * ST_AUDIO_RATE;
    while (S.audio_acc >= ST_CLOCK) {
        S.audio_acc -= ST_CLOCK;
        int32_t s = ay8910_render(&S.psg);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (S.naudio < ST_AUDIO_MAX) S.audio[S.naudio++] = (int16_t)s;
    }
}

// the audio is made per slice; a write inside one first catches up
static int psg_caught;              // cycles of this slice already rendered

static void psg_sync(void)
{
    const int now = cpu_done();
    if (now > psg_caught) {
        psg_samples(now - psg_caught);
        psg_caught = now;
    }
}

static void psg_write(uint8_t v)
{
    psg_sync();
    const uint8_t reg = S.psg.latch;
    ay8910_data_w(&S.psg, v);
    if (reg == 14) psg_port_a(v);
    else if (reg == 7) {
        const int en = S.psg.regs[7];
        if (S.prev_enable < 0 || ((S.prev_enable ^ en) & 0x40))
            psg_port_a((en & 0x40) ? S.psg.regs[14] : 0xff);
        S.prev_enable = en;
    }
}

// ---------------------------------------------------------------------------
// ACIA + IKBD

static void ikbd_push(uint8_t v)
{
    // ~1 ms at 8 MHz. The 6850 has a one-byte buffer; the others wait until
    // TOS reads RDR so GPIP4 can rise and fall again (the MFP sees edges)
    if (S.pend_count >= IKBD_QUEUE) return;
    ikbd_byte_t *b = &S.pend[(S.pend_head + S.pend_count++) % IKBD_QUEUE];
    b->value = v;
    b->cycles = 8000;
}

static void service_acia(void)
{
    if (S.rx_full || !S.pend_count || S.pend[S.pend_head].cycles > 0) return;
    S.acia_rdr = S.pend[S.pend_head].value;
    S.pend_head = (S.pend_head + 1) % IKBD_QUEUE;
    S.pend_count--;
    S.rx_full = true;
    mfp_set_gpip_bit(&S.mfp, 4, 0);
}

static uint8_t acia_read_data(void)
{
    // the 6850 keeps the last byte: reading only clears RDRF
    if (S.rx_full) {
        S.rx_full = false;
        mfp_set_gpip_bit(&S.mfp, 4, 1);
        if (S.pend_count && S.in_cpu) end_slice();     // the next byte's time
    }
    return S.acia_rdr;
}

static void ikbd_reset_modes(void)
{
    S.mouse_mode = MOUSE_REL;
    S.joy_mode = JOY_EVENT;
    S.paused = false;
    S.y_bottom = false;
    S.button_action = 0;
    S.abs_x = S.abs_y = 0;
    S.abs_max_x = 319;
    S.abs_max_y = 199;
    S.abs_buttons = 0;
    S.abs_left = S.abs_right = false;
    S.joy_monitor_rate = S.joy_monitor_count = 0;
}

static int ikbd_command_length(uint8_t c)
{
    switch (c) {
    case 0x07: return 2;
    case 0x08: return 1;
    case 0x09: return 5;
    case 0x0a: case 0x0b: case 0x0c: return 3;
    case 0x0d: return 1;
    case 0x0e: return 6;
    case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: return 1;
    case 0x17: return 2;
    case 0x18: return 1;
    case 0x19: return 7;
    case 0x1a: return 1;
    case 0x1b: return 7;
    case 0x1c: return 1;
    case 0x20: return 4;
    case 0x21: case 0x22: return 3;
    case 0x80: return 2;
    default: break;
    }
    if (c >= 0x87 && c <= 0x9a) return 1;
    return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ikbd_command(const uint8_t *cmd)
{
    if (cmd[0] != 0x13) S.paused = false;
    switch (cmd[0]) {
    case 0x80:
        if (cmd[1] != 0x01) return;
        ikbd_reset_modes();
        ikbd_push(0xf1);
        return;
    case 0x07: S.button_action = cmd[1]; return;
    case 0x08: S.mouse_mode = MOUSE_REL; return;
    case 0x09:
        S.mouse_mode = MOUSE_ABS;
        S.abs_max_x = (cmd[1] << 8) | cmd[2];
        S.abs_max_y = (cmd[3] << 8) | cmd[4];
        if (S.abs_x > S.abs_max_x) S.abs_x = S.abs_max_x;
        if (S.abs_y > S.abs_max_y) S.abs_y = S.abs_max_y;
        return;
    case 0x0a: S.mouse_mode = MOUSE_KEYCODE; return;
    case 0x0b: case 0x0c: return;
    case 0x0d: {
        if (S.mouse_mode != MOUSE_ABS) return;
        ikbd_push(0xf7);
        ikbd_push(S.abs_buttons);
        S.abs_buttons = 0;
        const int y = S.y_bottom ? S.abs_max_y - S.abs_y : S.abs_y;
        ikbd_push((uint8_t)(S.abs_x >> 8));
        ikbd_push((uint8_t)S.abs_x);
        ikbd_push((uint8_t)(y >> 8));
        ikbd_push((uint8_t)y);
        return;
    }
    case 0x0e: {
        S.abs_x = clampi((cmd[2] << 8) | cmd[3], 0, S.abs_max_x);
        const int y = clampi((cmd[4] << 8) | cmd[5], 0, S.abs_max_y);
        S.abs_y = S.y_bottom ? S.abs_max_y - y : y;
        return;
    }
    case 0x0f: S.y_bottom = true; return;
    case 0x10: S.y_bottom = false; return;
    case 0x11: return;
    case 0x12: S.mouse_mode = MOUSE_OFF; return;
    case 0x13: S.paused = true; return;
    case 0x14: S.joy_mode = JOY_EVENT; S.mouse_mode = MOUSE_OFF; return;
    case 0x15: S.joy_mode = JOY_INTERROGATE; S.mouse_mode = MOUSE_OFF; return;
    case 0x16:
        ikbd_push(0xfd);
        ikbd_push(S.joy_state[0]);
        ikbd_push(S.joy_state[1]);
        return;
    case 0x17:
        S.joy_mode = JOY_MONITOR;
        S.mouse_mode = MOUSE_OFF;
        S.joy_monitor_rate = cmd[1];
        S.joy_monitor_count = 0;
        return;
    case 0x18: case 0x19: case 0x1a: S.joy_mode = JOY_OFF; return;
    case 0x1b:
        for (int i = 0; i < 6; i++)
            if ((cmd[1 + i] & 0x0f) <= 9 && (cmd[1 + i] >> 4) <= 9) S.clock[i] = cmd[1 + i];
        S.clock_frames = 0;
        return;
    case 0x1c:
        ikbd_push(0xfc);
        for (int i = 0; i < 6; i++) ikbd_push(S.clock[i]);
        return;
    case 0x20: case 0x22: return;
    case 0x21:
        ikbd_push(0xf6);
        ikbd_push(0x20);
        for (int i = 0; i < 6; i++) ikbd_push(0);
        return;
    default: break;
    }
    if (cmd[0] >= 0x87 && cmd[0] <= 0x9a) {
        uint8_t r[7] = { 0 };
        switch (cmd[0]) {
        case 0x87: r[0] = 0x07; r[1] = S.button_action; break;
        case 0x88:
            r[0] = S.mouse_mode == MOUSE_ABS ? 0x09 : S.mouse_mode == MOUSE_KEYCODE ? 0x0a : 0x08;
            if (S.mouse_mode == MOUSE_ABS) {
                r[1] = (uint8_t)(S.abs_max_x >> 8);
                r[2] = (uint8_t)S.abs_max_x;
                r[3] = (uint8_t)(S.abs_max_y >> 8);
                r[4] = (uint8_t)S.abs_max_y;
            }
            break;
        case 0x8b: r[0] = 0x0b; r[1] = 1; r[2] = 1; break;
        case 0x8c: r[0] = 0x0c; r[1] = 1; r[2] = 1; break;
        case 0x8f: case 0x90: r[0] = S.y_bottom ? 0x0f : 0x10; break;
        case 0x92: r[0] = S.mouse_mode == MOUSE_OFF ? 0x12 : 0x00; break;
        case 0x94: case 0x95: case 0x99: r[0] = S.joy_mode == JOY_INTERROGATE ? 0x15 : 0x14; break;
        case 0x9a: r[0] = S.joy_mode == JOY_OFF ? 0x1a : 0x00; break;
        default: break;
        }
        ikbd_push(0xf6);
        for (int i = 0; i < 7; i++) ikbd_push(r[i]);
    }
}

static void ikbd_byte(uint8_t v)
{
    if (S.cmd_len == 0) {
        const int need = ikbd_command_length(v);
        if (need == 0) return;          // not a command: the 6301 ignores it
        S.cmd_need = need;
    }
    if (S.cmd_len < (int)sizeof S.cmd) S.cmd[S.cmd_len] = v;
    S.cmd_len++;
    // memory load: the count byte says how many data bytes follow
    if (S.cmd[0] == 0x20 && S.cmd_len == 4) S.cmd_need = 4 + S.cmd[3];
    if (S.cmd_len < S.cmd_need) return;
    S.cmd_len = S.cmd_need = 0;
    ikbd_command(S.cmd);
}

static void ikbd_mouse_report(int dx, int dy, bool left, bool right)
{
    if (S.paused) return;
    const bool lch = left != S.abs_left, rch = right != S.abs_right;
    if (lch) S.abs_buttons |= left ? 0x04 : 0x08;
    if (rch) S.abs_buttons |= right ? 0x01 : 0x02;
    S.abs_left = left;
    S.abs_right = right;
    switch (S.mouse_mode) {
    case MOUSE_REL:
        ikbd_push((uint8_t)(0xf8 | (left ? 2 : 0) | (right ? 1 : 0)));
        ikbd_push((uint8_t)dx);
        ikbd_push((uint8_t)(S.y_bottom ? -dy : dy));
        return;
    case MOUSE_ABS:
        S.abs_x = clampi(S.abs_x + dx, 0, S.abs_max_x);
        S.abs_y = clampi(S.abs_y + dy, 0, S.abs_max_y);
        if (S.button_action & 0x04) {
            if (lch) ikbd_push(left ? 0x74 : 0xf4);
            if (rch) ikbd_push(right ? 0x75 : 0xf5);
        }
        return;
    default: return;
    }
}

// the mouse's motion since the last frame, in the ST's pixels
static void ikbd_mouse(void)
{
    const int mode = S.resolution & 3;
    const int st_w = mode == 0 ? 320 : 640, st_h = mode == 2 ? 400 : 200;
    S.frac_x += S.mouse_dx * st_w;
    S.frac_y += S.mouse_dy * st_h;
    S.mouse_dx = S.mouse_dy = 0;
    int dx = S.frac_x / S.fb_w, dy = S.frac_y / S.fb_h;
    S.frac_x -= dx * S.fb_w;
    S.frac_y -= dy * S.fb_h;
    const bool l = S.mouse_left, r = S.mouse_right;
    bool send_button = l != S.last_left || r != S.last_right;
    S.last_left = l;
    S.last_right = r;
    // signed 8-bit reports; never -128 ($80, the IKBD's reset prefix)
    while (dx || dy || send_button) {
        const int sx = clampi(dx, -127, 127), sy = clampi(dy, -127, 127);
        ikbd_mouse_report(sx, sy, l, r);
        dx -= sx;
        dy -= sy;
        send_button = false;
    }
}

static void ikbd_joysticks(void)
{
    for (int j = 0; j < 2; j++) {
        const bool changed = S.joy_new[j] != S.joy_state[j];
        S.joy_state[j] = S.joy_new[j];
        if (!changed || S.paused || S.joy_mode != JOY_EVENT) continue;
        if (j == 0 && S.mouse_mode != MOUSE_OFF) continue;     // the mouse owns port 0
        ikbd_push((uint8_t)(0xfe + j));
        ikbd_push(S.joy_state[j]);
    }
    if (S.joy_mode == JOY_MONITOR && !S.paused) {
        if (++S.joy_monitor_count * 2 >= (S.joy_monitor_rate > 2 ? S.joy_monitor_rate : 2)) {
            S.joy_monitor_count = 0;
            ikbd_push((uint8_t)(((S.joy_state[1] >> 7) << 1) | (S.joy_state[0] >> 7)));
            ikbd_push((uint8_t)(((S.joy_state[1] & 0x0f) << 4) | (S.joy_state[0] & 0x0f)));
        }
    }
}

static uint8_t bcd_inc(uint8_t v)
{
    v = (uint8_t)(v + 1);
    if ((v & 0x0f) > 9) v = (uint8_t)((v & 0xf0) + 0x10);
    return v;
}

static void ikbd_clock_tick(void)
{
    if (++S.clock_frames < 50) return;
    S.clock_frames = 0;
    static const uint8_t kLimit[6] = { 0x99, 0x12, 0x31, 0x23, 0x59, 0x59 };
    for (int i = 5; i >= 3; i--) {
        if (S.clock[i] < kLimit[i]) {
            S.clock[i] = bcd_inc(S.clock[i]);
            return;
        }
        S.clock[i] = 0;
    }
}

static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) % 10) << 4 | (v % 10)); }

void st_set_time(int year, int month, int day, int hour, int min, int sec)
{
    S.clock[0] = to_bcd(year % 100);
    S.clock[1] = to_bcd(month);
    S.clock[2] = to_bcd(day);
    S.clock[3] = to_bcd(hour);
    S.clock[4] = to_bcd(min);
    S.clock[5] = to_bcd(sec);
    S.clock_frames = 0;
}

void st_key(int code, bool down)
{
    code &= 0x7f;
    if (S.keys[code] == down) return;
    S.keys[code] = down;
    ikbd_push((uint8_t)(down ? code : code | 0x80));
}

void st_keys_release(void)
{
    for (int c = 0; c < 128; c++) st_key(c, false);
}

void st_mouse(int dx, int dy)
{
    S.mouse_dx += dx;
    S.mouse_dy += dy;
}

void st_mouse_buttons(bool left, bool right)
{
    S.mouse_left = left;
    S.mouse_right = right;
}

void st_joy(int port, uint8_t bits) { S.joy_new[port & 1] = bits; }

// ---------------------------------------------------------------------------
// Blitter at $FF8A00 (TOS 1.04's Line-A uses it once its probe finds it)

static uint16_t blit_get_word(uint32_t a)
{
    a &= 0xfffffe;
    if (a >= 0xff8a00 && a < 0xff8a20) return S.blit_halftone[(a - 0xff8a00) >> 1];
    switch (a) {
    case 0xff8a20: return (uint16_t)S.blit_sxinc;
    case 0xff8a22: return (uint16_t)S.blit_syinc;
    case 0xff8a24: return (uint16_t)(S.blit_src >> 16);
    case 0xff8a26: return (uint16_t)S.blit_src;
    case 0xff8a28: return S.blit_emask[0];
    case 0xff8a2a: return S.blit_emask[1];
    case 0xff8a2c: return S.blit_emask[2];
    case 0xff8a2e: return (uint16_t)S.blit_dxinc;
    case 0xff8a30: return (uint16_t)S.blit_dyinc;
    case 0xff8a32: return (uint16_t)(S.blit_dst >> 16);
    case 0xff8a34: return (uint16_t)S.blit_dst;
    case 0xff8a36: return S.blit_xcount;
    case 0xff8a38: return S.blit_ycount;
    default: return 0;
    }
}

static void blit_set_word(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a >= 0xff8a00 && a < 0xff8a20) {
        S.blit_halftone[(a - 0xff8a00) >> 1] = v;
        return;
    }
    switch (a) {
    case 0xff8a20: S.blit_sxinc = (int16_t)v; return;
    case 0xff8a22: S.blit_syinc = (int16_t)v; return;
    case 0xff8a24: S.blit_src = (S.blit_src & 0xffff) | ((uint32_t)v << 16); return;
    case 0xff8a26: S.blit_src = (S.blit_src & 0xffff0000u) | v; return;
    case 0xff8a28: S.blit_emask[0] = v; return;
    case 0xff8a2a: S.blit_emask[1] = v; return;
    case 0xff8a2c: S.blit_emask[2] = v; return;
    case 0xff8a2e: S.blit_dxinc = (int16_t)v; return;
    case 0xff8a30: S.blit_dyinc = (int16_t)v; return;
    case 0xff8a32: S.blit_dst = (S.blit_dst & 0xffff) | ((uint32_t)v << 16); return;
    case 0xff8a34: S.blit_dst = (S.blit_dst & 0xffff0000u) | v; return;
    case 0xff8a36: S.blit_xcount = v; return;
    case 0xff8a38: S.blit_ycount = v; return;
    default: return;
    }
}

static uint16_t rom_word_at(uint32_t base, uint32_t a)
{
    if (a >= base && a + 1 < base + S.rom_size) {
        const uint32_t o = a - base;
        return (uint16_t)((S.rom[o] << 8) | S.rom[o + 1]);
    }
    return 0;
}

static uint16_t blit_mem_read(uint32_t a)
{
    a &= 0xfffffe;
    if (a + 1 < RAM_SIZE) return (uint16_t)((S.ram[a] << 8) | S.ram[a + 1]);
    // Line-A text blits the system font out of TOS
    if (a >= 0xfc0000) return rom_word_at(0xfc0000, a);
    if (a >= 0xe00000) return rom_word_at(0xe00000, a);
    return 0xffff;
}

static void blit_mem_write(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a + 1 < RAM_SIZE) {
        S.ram[a] = (uint8_t)(v >> 8);
        S.ram[a + 1] = (uint8_t)v;
    }
}

static void run_blitter(void)
{
    // finished at once, so TOS never waits in `tst.b (a5); bmi.s`
    if (S.blit_ycount == 0) {
        S.blit_ctrl &= 0x3f;
        return;
    }
    const int hop = S.blit_hop & 3, op = S.blit_op & 15, skew = S.blit_skew & 15;
    const bool fxsr = (S.blit_skew & 0x80) != 0, nfsr = (S.blit_skew & 0x40) != 0;
    const bool smudge = (S.blit_ctrl & 0x20) != 0;
    int line = S.blit_ctrl & 15;
    static const bool kOpSrc[16] = { 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0 };
    static const bool kOpDst[16] = { 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0 };
    const bool hop_src = (hop & 2) != 0 || (hop == 1 && smudge);
    const bool need_src = kOpSrc[op] && hop_src;
    const uint32_t xspan = S.blit_xcount ? S.blit_xcount : 65536u;
    uint32_t yleft = S.blit_ycount;
    uint32_t src = S.blit_src & 0xfffffe, dst = S.blit_dst & 0xfffffe;
    uint32_t buffer = 0, words = 0;
    const uint32_t kMaxWords = 0x100000;
    const int16_t sxinc = S.blit_sxinc, syinc = S.blit_syinc;
#define ADD_SRC(inc) (src = (uint32_t)((int32_t)src + (inc)) & 0xfffffe)
#define ADD_DST(inc) (dst = (uint32_t)((int32_t)dst + (inc)) & 0xfffffe)
#define SHIFT_BUF() (sxinc < 0 ? (buffer >>= 16) : (buffer <<= 16))
#define FETCH_SRC() do { SHIFT_BUF(); const uint32_t w_ = blit_mem_read(src); \
        if (sxinc < 0) buffer |= w_ << 16; else buffer |= w_; } while (0)
    while (yleft && words < kMaxWords) {
        uint32_t xleft = xspan;
        bool skip_src = false;
        while (xleft && words < kMaxWords) {
            const bool first = xleft == xspan, last = xleft == 1;
            uint16_t mask = S.blit_emask[1];
            if (first || xspan == 1) mask = S.blit_emask[0];
            else if (last) mask = S.blit_emask[2];
            bool fetched = false;
            if (need_src) {
                if (first && fxsr) {
                    FETCH_SRC();
                    ADD_SRC(sxinc);
                }
                if (!skip_src) {
                    FETCH_SRC();
                    fetched = true;
                } else {
                    SHIFT_BUF();        // NFSR: no read, but the buffer still shifts
                }
            }
            const uint16_t srcw = (uint16_t)(buffer >> skew);
            const uint16_t ht = smudge ? S.blit_halftone[srcw & 15] : S.blit_halftone[line];
            uint16_t hv = 0xffff;
            if (hop == 1) hv = ht;
            else if (hop == 2) hv = srcw;
            else if (hop == 3) hv = (uint16_t)(srcw & ht);
            const bool read_dst = kOpDst[op] || mask != 0xffff;
            const uint16_t dw = read_dst ? blit_mem_read(dst) : 0;
            uint16_t lop;
            switch (op) {
            case 0: lop = 0; break;
            case 1: lop = hv & dw; break;
            case 2: lop = hv & (uint16_t)~dw; break;
            case 3: lop = hv; break;
            case 4: lop = (uint16_t)~hv & dw; break;
            case 5: lop = dw; break;
            case 6: lop = hv ^ dw; break;
            case 7: lop = hv | dw; break;
            case 8: lop = (uint16_t)(~hv & ~dw); break;
            case 9: lop = (uint16_t)(~hv ^ dw); break;
            case 10: lop = (uint16_t)~dw; break;
            case 11: lop = (uint16_t)(hv | ~dw); break;
            case 12: lop = (uint16_t)~hv; break;
            case 13: lop = (uint16_t)(~hv | dw); break;
            case 14: lop = (uint16_t)(~hv | ~dw); break;
            default: lop = 0xffff; break;
            }
            blit_mem_write(dst, (uint16_t)((lop & mask) | (dw & ~mask)));
            words++;
            if (xleft == 2 && nfsr) skip_src = true;
            if (fetched) {
                if (last || skip_src) ADD_SRC(syinc);
                else ADD_SRC(sxinc);
            }
            if (last) {
                ADD_DST(S.blit_dyinc);
                line = S.blit_dyinc >= 0 ? ((line + 1) & 15) : ((line - 1) & 15);
            } else {
                ADD_DST(S.blit_dxinc);
            }
            xleft--;
        }
        yleft--;
    }
#undef ADD_SRC
#undef ADD_DST
#undef SHIFT_BUF
#undef FETCH_SRC
    S.blit_src = src;
    S.blit_dst = dst;
    if (!S.blit_xcount) S.blit_xcount = (uint16_t)xspan;
    S.blit_ycount = 0;
    S.blit_ctrl = (uint8_t)((S.blit_ctrl & 0x30) | (line & 15));
}

// ---------------------------------------------------------------------------
// Memory map

static uint8_t ST_HOT read_byte(uint32_t a)
{
    a &= 0xffffff;
    if (S.rom_at_zero && a < 8) return S.rom[a];
    if (a < RAM_SIZE) return S.ram[a];
    if (a >= 0xfc0000 && a < 0xfc0000 + S.rom_size) return S.rom[a - 0xfc0000];
    if (a >= 0xff8000) {
        switch (a) {
        case 0xff8001: return S.memcfg;
        case 0xff8201: return S.video_hi;
        case 0xff8203: return S.video_mid;
        case 0xff8205: return (uint8_t)(S.video_count >> 16);
        case 0xff8207: return (uint8_t)(S.video_count >> 8);
        case 0xff8209: return (uint8_t)S.video_count;
        case 0xff820a: return S.sync_mode;
        case 0xff8260: return S.resolution;
        case 0xff8604: { const uint16_t v = stf_dma_data_r(&S.fdc); update_fdc_irq(); return (uint8_t)(v >> 8); }
        case 0xff8605: { const uint16_t v = stf_dma_data_r(&S.fdc); update_fdc_irq(); return (uint8_t)v; }
        case 0xff8606: return (uint8_t)(stf_dma_status(&S.fdc) >> 8);
        case 0xff8607: return (uint8_t)stf_dma_status(&S.fdc);
        case 0xff8609: return stf_dma_addr_r(&S.fdc, 0);
        case 0xff860b: return stf_dma_addr_r(&S.fdc, 1);
        case 0xff860d: return stf_dma_addr_r(&S.fdc, 2);
        case 0xff8800: case 0xff8801: return ay8910_data_r(&S.psg);
        case 0xfffc00: return S.rx_full ? 0x83 : 0x02;     // RDRF + IRQ, TDRE
        case 0xfffc02: return acia_read_data();
        case 0xfffc04: return 0x02;                         // MIDI ACIA: TDRE
        case 0xfffc06: return 0;
        default: break;
        }
        if (a >= 0xff8240 && a < 0xff8260) {
            const uint16_t w = S.palette[(a - 0xff8240) >> 1];
            return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
        }
        if (a >= 0xfffa00 && a <= 0xfffa2f && (a & 1)) return mfp_read(&S.mfp, (int)((a - 0xfffa01) >> 1));
        if (a >= 0xff8a00 && a <= 0xff8a3d) {
            if (a == 0xff8a3a) return S.blit_hop;
            if (a == 0xff8a3b) return S.blit_op;
            if (a == 0xff8a3c) return S.blit_ctrl;
            if (a == 0xff8a3d) return S.blit_skew;
            const uint16_t w = blit_get_word(a);
            return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
        }
    }
    return 0xff;
}

// a command may set INTRQ's time: the slice ends to see it
static void fdc_write(uint16_t v)
{
    stf_dma_data_w(&S.fdc, v);
    update_fdc_irq();
    if (S.in_cpu && S.fdc.irq_delay > 0) end_slice();
}

static void ST_HOT write_byte(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (S.rom_at_zero && a < 8) return;
    if (a < RAM_SIZE) {
        S.ram[a] = v;
        return;
    }
    if (a < 0xff8000) return;
    switch (a) {
    case 0xff8001: S.memcfg = v; S.rom_at_zero = false; return;
    case 0xff8201: S.video_hi = v; return;
    case 0xff8203: S.video_mid = v; return;
    case 0xff820d: S.video_lo = v; return;
    case 0xff820a: S.sync_mode = v; return;
    case 0xff8260: S.resolution = (uint8_t)(v & 3); return;
    case 0xff8604: fdc_write(v); return;
    case 0xff8605: fdc_write((uint16_t)((S.fdc.dma_mode & 0xff00) | v)); return;
    case 0xff8606: stf_dma_mode_w(&S.fdc, (uint16_t)(v << 8)); return;
    case 0xff8607: stf_dma_mode_w(&S.fdc, v); return;
    case 0xff8609: stf_dma_addr_w(&S.fdc, 0, v); return;
    case 0xff860b: stf_dma_addr_w(&S.fdc, 1, v); return;
    case 0xff860d: stf_dma_addr_w(&S.fdc, 2, v); return;
    case 0xff8800: case 0xff8801: ay8910_address_w(&S.psg, v); return;
    case 0xff8802: case 0xff8803: psg_write(v); return;
    case 0xfffc00:
        S.acia_control = v;
        if ((v & 3) == 3) {             // master reset
            S.rx_full = false;
            S.pend_count = 0;
            mfp_set_gpip_bit(&S.mfp, 4, 1);
        }
        return;
    case 0xfffc02: ikbd_byte(v); if (S.in_cpu && S.pend_count) end_slice(); return;
    default: break;
    }
    if (a >= 0xff8240 && a < 0xff8260) {
        uint16_t *w = &S.palette[(a - 0xff8240) >> 1];
        if (a & 1) *w = (uint16_t)((*w & 0xff00) | v);
        else *w = (uint16_t)((*w & 0x00ff) | (v << 8));
        *w &= 0x0777;
        return;
    }
    if (a >= 0xfffa00 && a <= 0xfffa2f && (a & 1)) {
        mfp_write(&S.mfp, (int)((a - 0xfffa01) >> 1), v);
        if (S.in_cpu) end_slice();      // a timer may be running now
        return;
    }
    if (a >= 0xff8a00 && a <= 0xff8a3d) {
        if (a == 0xff8a3a) { S.blit_hop = v & 3; return; }
        if (a == 0xff8a3b) { S.blit_op = v & 15; return; }
        if (a == 0xff8a3c) {
            S.blit_ctrl = v;
            if ((v & 0x80) && !S.blit_defer_start) run_blitter();
            return;
        }
        if (a == 0xff8a3d) { S.blit_skew = v; return; }
        uint16_t w = blit_get_word(a);
        if (a & 1) w = (uint16_t)((w & 0xff00) | v);
        else w = (uint16_t)((w & 0x00ff) | (v << 8));
        blit_set_word(a, w);
    }
}

static uint16_t ST_HOT read_word(uint32_t a)
{
    a &= 0xfffffe;
    if (S.rom_at_zero && a < 8) return (uint16_t)((S.rom[a] << 8) | S.rom[a + 1]);
    if (a < RAM_SIZE) return (uint16_t)((S.ram[a] << 8) | S.ram[a + 1]);
    if (a >= 0xfc0000 && a < 0xfc0000 + S.rom_size) {
        const uint32_t o = a - 0xfc0000;
        return (uint16_t)((S.rom[o] << 8) | S.rom[o + 1]);
    }
    if (a == 0xff8604) {
        const uint16_t v = stf_dma_data_r(&S.fdc);
        update_fdc_irq();
        return v;
    }
    if (a == 0xff8606) return stf_dma_status(&S.fdc);
    if (a == 0xff8800) return (uint16_t)(ay8910_data_r(&S.psg) << 8);
    return (uint16_t)((read_byte(a) << 8) | read_byte(a + 1));
}

static void ST_HOT write_word(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (S.rom_at_zero && a < 8) return;
    if (a < RAM_SIZE) {
        S.ram[a] = (uint8_t)(v >> 8);
        S.ram[a + 1] = (uint8_t)v;
        return;
    }
    if (a == 0xff8604) {
        fdc_write(v);
        return;
    }
    if (a == 0xff8606) {
        stf_dma_mode_w(&S.fdc, v);
        return;
    }
    if (a == 0xff8800) {
        ay8910_address_w(&S.psg, (uint8_t)(v >> 8));
        psg_write((uint8_t)v);
        return;
    }
    if (a >= 0xff8240 && a < 0xff8260) {
        S.palette[(a - 0xff8240) >> 1] = v & 0x0777;
        return;
    }
    // a word write to $FF8A3C is control then skew: the blit starts after both
    if (a == 0xff8a3c) {
        S.blit_defer_start = true;
        write_byte(a, (uint8_t)(v >> 8));
        S.blit_defer_start = false;
        write_byte(a + 1, (uint8_t)v);
        if (S.blit_ctrl & 0x80) run_blitter();
        return;
    }
    write_byte(a, (uint8_t)(v >> 8));
    write_byte(a + 1, (uint8_t)v);
}

unsigned int cpu_read_byte(unsigned int a) { return read_byte(a); }
unsigned int cpu_read_word(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long(unsigned int a) { return ((uint32_t)read_word(a) << 16) | read_word(a + 2); }
unsigned int cpu_read_word_dasm(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void cpu_write_byte(unsigned int a, unsigned int v) { write_byte(a, (uint8_t)v); }
void cpu_write_word(unsigned int a, unsigned int v) { write_word(a, (uint16_t)v); }
void cpu_write_long(unsigned int a, unsigned int v)
{
    write_word(a, (uint16_t)(v >> 16));
    write_word(a + 2, (uint16_t)v);
}
void cpu_write_long_pd(unsigned int a, unsigned int v)
{
    write_word(a + 2, (uint16_t)v);
    write_word(a, (uint16_t)(v >> 16));
}

// the RESET instruction: the chips (not the RAM)
void cpu_pulse_reset(void)
{
    mfp_reset(&S.mfp);
    stf_reset(&S.fdc);
    ay8910_reset(&S.psg);
    S.prev_enable = -1;
    S.rx_full = false;
    S.pend_count = 0;
    mfp_set_gpip_bit(&S.mfp, 7, 1);
    mfp_set_gpip_bit(&S.mfp, 5, 1);
    mfp_set_gpip_bit(&S.mfp, 4, 1);
    update_ipl();
}

// ---------------------------------------------------------------------------
// Scheduling

static void advance(int cycles)
{
    stf_tick(&S.fdc, cycles);
    if (S.pend_count) S.pend[S.pend_head].cycles -= cycles;
    service_acia();
    update_fdc_irq();
    S.mfp_acc += (int64_t)cycles * MFP_CLOCK;
    if (S.mfp_acc >= ST_CLOCK) {
        const int ticks = (int)(S.mfp_acc / ST_CLOCK);
        S.mfp_acc -= (int64_t)ticks * ST_CLOCK;
        mfp_tick(&S.mfp, ticks);
    }
}

static const int kPrescale[8] = { 0, 4, 10, 16, 50, 64, 100, 200 };

// CPU cycles until a running timer reaches 0 (`limit` at most)
static int timer_event(const mfp_timer *t, int limit)
{
    if ((t->control & 0x0f) == 8) return limit;
    const int scale = kPrescale[t->control & 7];
    if (scale == 0 || t->data == 0) return limit;
    const int c = t->count ? t->count : t->data;
    const int64_t ticks = (int64_t)(c - 1) * scale + (scale - t->prescale_acc);
    const int64_t need = ticks * ST_CLOCK - S.mfp_acc;
    if (need <= 0) return 1;
    const int64_t cyc = (need + MFP_CLOCK - 1) / MFP_CLOCK;
    return cyc < limit ? (int)cyc : limit;
}

static int next_event(int limit)
{
    if (S.fdc.irq_delay > 0 && S.fdc.irq_delay < limit) limit = S.fdc.irq_delay;
    if (S.pend_count && !S.rx_full) {
        const int c = S.pend[S.pend_head].cycles;
        if (c < limit) limit = c < 1 ? 1 : c;
    }
    limit = timer_event(&S.mfp.ta, limit);
    limit = timer_event(&S.mfp.tb, limit);
    limit = timer_event(&S.mfp.tc, limit);
    limit = timer_event(&S.mfp.td, limit);
    return limit;
}

static void run_cpu(int cycles)
{
    int left = cycles - S.debt;
    while (left > 0) {
        const int run = next_event(left);
        psg_caught = 0;
        S.in_cpu = true;
        const int used = m68k_execute(run);
        S.in_cpu = false;
        if (used > psg_caught) psg_samples(used - psg_caught);
        left -= used;
        advance(used);
        if (S.ipl_pending) {
            S.ipl_pending = false;
            S.cur_ipl = -1;
            update_ipl();
        }
    }
    S.debt = -left;
}

// ---------------------------------------------------------------------------
// Picture

static uint16_t st_rgb565(uint16_t w)
{
    const int r = (w >> 8) & 7, g = (w >> 4) & 7, b = w & 7;
    const int r8 = r * 36, g8 = g * 36, b8 = b * 36;
    return (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
}

static inline uint16_t mix565(uint16_t a, uint16_t b)
{
    return (uint16_t)(((a & 0xf7de) >> 1) + ((b & 0xf7de) >> 1));
}

static inline uint16_t ram_w(uint32_t o) { return (uint16_t)((S.ram[o] << 8) | S.ram[o + 1]); }

static void render(void)
{
    const uint32_t vbase = ((uint32_t)S.video_hi << 16) | ((uint32_t)S.video_mid << 8) | S.video_lo;
    const int mode = S.resolution & 3;
    uint16_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = st_rgb565(S.palette[i]);
    const int w = S.fb_w;
    for (int y = 0; y < 200 * (mode == 2 ? 2 : 1); y++) {
        uint16_t line[640];
        int n = 0;
        if (mode == 2) {                // 640 x 400, one plane
            const uint32_t row = vbase + (uint32_t)y * 80u;
            for (int x = 0; x < 640; x += 16) {
                const uint32_t o = row + (uint32_t)(x / 8);
                const uint16_t p = o + 1 < RAM_SIZE ? ram_w(o) : 0;
                for (int b = 0; b < 16; b++) line[n++] = (p & (0x8000 >> b)) ? pal[1] : pal[0];
            }
        } else if (mode == 1) {         // 640 x 200, two planes
            const uint32_t row = vbase + (uint32_t)y * 160u;
            for (int x = 0; x < 640; x += 16) {
                const uint32_t o = row + (uint32_t)(x / 4);
                uint16_t p0 = 0, p1 = 0;
                if (o + 3 < RAM_SIZE) { p0 = ram_w(o); p1 = ram_w(o + 2); }
                for (int b = 0; b < 16; b++) {
                    line[n++] = pal[((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1)];
                    p0 = (uint16_t)(p0 << 1);
                    p1 = (uint16_t)(p1 << 1);
                }
            }
        } else {                        // 320 x 200, four planes
            const uint32_t row = vbase + (uint32_t)y * 160u;
            for (int x = 0; x < 320; x += 16) {
                const uint32_t o = row + (uint32_t)(x / 2);
                uint16_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;
                if (o + 7 < RAM_SIZE) { p0 = ram_w(o); p1 = ram_w(o + 2); p2 = ram_w(o + 4); p3 = ram_w(o + 6); }
                for (int b = 0; b < 16; b++) {
                    line[n++] = pal[((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1) | (((p2 >> 15) & 1) << 2) |
                                    (((p3 >> 15) & 1) << 3)];
                    p0 = (uint16_t)(p0 << 1);
                    p1 = (uint16_t)(p1 << 1);
                    p2 = (uint16_t)(p2 << 1);
                    p3 = (uint16_t)(p3 << 1);
                }
            }
        }
        // n source pixels (320 or 640) into a w-wide row
        if (!S.half) {
            uint16_t *out = S.fb + (size_t)(mode == 2 ? y : y * 2) * w;
            if (n == w) memcpy(out, line, (size_t)w * 2);
            else for (int x = 0; x < n; x++) out[2 * x] = out[2 * x + 1] = line[x];
            if (mode != 2) memcpy(out + w, out, (size_t)w * 2);
        } else if (mode == 2) {         // two lines and two pixels into one
            uint16_t *out = S.fb + (size_t)(y >> 1) * w;
            for (int x = 0; x < w; x++) {
                const uint16_t p = mix565(line[2 * x], line[2 * x + 1]);
                out[x] = (y & 1) ? mix565(out[x], p) : p;
            }
        } else {
            uint16_t *out = S.fb + (size_t)y * w;
            if (n == w) memcpy(out, line, (size_t)w * 2);
            else for (int x = 0; x < w; x++) out[x] = mix565(line[2 * x], line[2 * x + 1]);
        }
    }
}

// ---------------------------------------------------------------------------
// Machine

bool st_init(const uint8_t *tos, uint32_t size, bool half)
{
    if (!tos || size != ST_TOS_SIZE) return false;
    S.half = half;
    S.fb_w = half ? 320 : ST_W;
    S.fb_h = half ? 200 : ST_H;
    if (!S.ram) S.ram = (uint8_t *)malloc(RAM_SIZE);
    if (!S.fb) S.fb = (uint16_t *)malloc((size_t)S.fb_w * S.fb_h * 2);
    if (!S.ram || !S.fb) return false;
    S.rom = tos;
    S.rom_size = size;
    S.mfp.irq_cb = mfp_irq_changed;
    ay8910_init(&S.psg, 2000000, ST_AUDIO_RATE);
    ay8910_set_amplitude(&S.psg, 1.2f);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    static const uint8_t kClock[6] = { 0x89, 0x01, 0x01, 0x00, 0x00, 0x00 };
    memcpy(S.clock, kClock, sizeof kClock);
    st_reset();
    return true;
}

void st_reset(void)
{
    memset(S.ram, 0, RAM_SIZE);
    memset(S.fb, 0, (size_t)S.fb_w * S.fb_h * 2);
    memset(S.palette, 0, sizeof S.palette);
    S.rom_at_zero = true;
    S.memcfg = 0;
    S.video_hi = S.video_mid = S.video_lo = 0;
    S.sync_mode = 0x02;
    S.resolution = 0;
    S.video_count = 0;
    S.rx_full = false;
    S.acia_rdr = S.acia_control = 0;
    S.pend_head = S.pend_count = 0;
    S.cmd_len = S.cmd_need = 0;
    ikbd_reset_modes();
    memset(S.keys, 0, sizeof S.keys);
    S.mouse_dx = S.mouse_dy = S.frac_x = S.frac_y = 0;
    S.last_left = S.last_right = false;
    memset(S.blit_halftone, 0, sizeof S.blit_halftone);
    S.blit_sxinc = S.blit_syinc = S.blit_dxinc = S.blit_dyinc = 0;
    S.blit_src = S.blit_dst = 0;
    S.blit_emask[0] = S.blit_emask[1] = S.blit_emask[2] = 0;
    S.blit_xcount = S.blit_ycount = 0;
    S.blit_hop = S.blit_op = S.blit_ctrl = S.blit_skew = 0;
    S.blit_defer_start = false;
    S.mfp_acc = S.audio_acc = 0;
    S.naudio = 0;
    S.in_cpu = S.ipl_pending = S.vbl_hold = S.hbl_hold = false;
    S.debt = 0;
    mfp_reset(&S.mfp);
    ay8910_reset(&S.psg);
    S.prev_enable = -1;
    S.psg_port_a = 0xff;
    stf_reset(&S.fdc);
    S.fdc.ram = S.ram;
    S.fdc.ram_size = RAM_SIZE;
    // colour monitor: GPIP 7 high; the FDC's and the ACIA's lines idle high
    mfp_set_gpip_bit(&S.mfp, 7, 1);
    mfp_set_gpip_bit(&S.mfp, 5, 1);
    mfp_set_gpip_bit(&S.mfp, 4, 1);
    S.cur_ipl = 0;
    m68k_set_irq(0);
    m68k_pulse_reset();
}

int st_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    // the inputs of this frame (dsp-cpp: set_inputs before run_frame)
    ikbd_mouse();
    ikbd_joysticks();
    const uint32_t vbase = ((uint32_t)S.video_hi << 16) | ((uint32_t)S.video_mid << 8) | S.video_lo;
    const uint32_t pitch = (S.resolution & 3) == 2 ? 80 : 160;
    S.vbl_hold = true;
    update_ipl();
    for (int line = 0; line < LINES; line++) {
        S.video_count = (line >= 63 && line < 263) ? vbase + (uint32_t)(line - 63) * pitch : vbase;
        mfp_pulse_tb(&S.mfp);
        // HBL every line: TOS's handler raises the IPL of the code it
        // interrupted to 3, which is why programs run at $x300
        S.hbl_hold = true;
        update_ipl();
        run_cpu(LINE_CYCLES);
        update_fdc_irq();
    }
    ikbd_clock_tick();
    render();
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *st_frame(void) { return S.fb; }
int st_width(void) { return S.fb_w; }
int st_height(void) { return S.fb_h; }
int st_resolution(void) { return S.resolution & 3; }

bool st_insert(int drive, uint8_t *data, uint32_t size, const char *name, const char **why)
{
    const char *w = NULL;
    const bool ok = stf_insert(&S.fdc, drive, data, size, name, &w);
    if (why) *why = w;
    return ok;
}
void st_eject(int drive) { stf_eject(&S.fdc, drive); }
bool st_disk_in(int drive) { return stf_loaded(&S.fdc, drive); }
int st_free_drive(void)
{
    for (int i = 0; i < ST_DRIVES; i++)
        if (!st_disk_in(i)) return i;
    return -1;
}
bool st_drive_led(void) { return stf_motor(&S.fdc); }

uint32_t st_pc(void) { return m68k_get_reg(NULL, M68K_REG_PC); }
uint8_t st_peek(uint32_t a)
{
    a &= 0xffffff;
    if (a < RAM_SIZE) return S.ram[a];
    if (a >= 0xfc0000 && a < 0xfc0000 + S.rom_size) return S.rom[a - 0xfc0000];
    return 0xff;
}
