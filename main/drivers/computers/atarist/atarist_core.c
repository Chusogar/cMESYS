// atarist_core.c - Atari ST: a C11 migration of dsp-cpp's
// drivers/computers/atari_st (see atarist_core.h).
//
// dsp-cpp runs its own 68000 with a cycle callback after every instruction;
// here Musashi runs slices of SLICE cycles and the floppy, the ACIA's IKBD
// queue, the MFP's timers and the sound advance after each slice. dsp-cpp's
// "hold" interrupts (the VBL at level 4, the HBL at level 2) stay pending
// until the 68000 acknowledges them; the MFP is level 6 with its own vector.
#include "atarist_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay8910.h"
#include "m68k.h"
#include "mc68901.h"
#include "st_floppy.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ST_HOT IRAM_ATTR
#else
#define ST_HOT
#endif

#define LINES           313
#define CYCLES_PER_LINE 512
#define SLICE           64
#define CHUNK_BITS      15                      // RAM in 32 KB pieces (the boards' heap)
#define CHUNK           (1u << CHUNK_BITS)
#define MAX_CHUNKS      (0x100000 >> CHUNK_BITS)
#define RX_MAX          64
#define PENDING_MAX     512

enum { MOUSE_REL, MOUSE_ABS, MOUSE_KEYCODE, MOUSE_OFF };
enum { JOY_EVENT, JOY_INTERROGATE, JOY_MONITOR, JOY_OFF };

typedef struct { uint8_t value; int cycles; } ikbd_byte_t;

typedef struct {
    uint8_t *ram[MAX_CHUNKS];
    uint32_t ram_size;
    const uint8_t *rom;
    int rate;

    ay8910 psg;
    mc68901 mfp;
    st_floppy fdc;

    uint16_t palette[16];
    bool rom_at_zero;
    uint8_t memcfg, video_hi, video_mid, video_lo, sync_mode, resolution, psg_port_a;

    uint8_t acia_control, acia_rdr;
    uint8_t rx[RX_MAX];
    int rx_head, rx_n;
    ikbd_byte_t pending[PENDING_MAX];
    int pend_head, pend_n;
    uint8_t cmd[64];
    int cmd_n, cmd_need;
    int mouse_mode, joy_mode;
    bool paused, mouse_y_bottom;
    uint8_t mouse_button_action;
    int abs_x, abs_y, abs_max_x, abs_max_y;
    uint8_t abs_buttons;
    bool abs_left, abs_right;
    uint8_t joy_state[2];
    int joy_monitor_rate, joy_monitor_count;
    uint8_t clock[6];
    int clock_frames;
    int last_pointer_x, last_pointer_y, pointer_frac_x, pointer_frac_y;
    bool pointer_seen, seed_valid;
    int seed_x, seed_y;
    bool last_b1, last_b2;
    uint32_t linea;
    int linea_scan_wait;
    bool sync_sent, sync_off;
    int sync_fail, sync_from_x, sync_from_y, sync_rest_x, sync_rest_y;
    uint32_t video_count;

    uint16_t blit_halftone[16];
    int16_t blit_sxinc, blit_syinc, blit_dxinc, blit_dyinc;
    uint32_t blit_src, blit_dst;
    uint16_t blit_emask[3];
    uint16_t blit_xcount, blit_ycount;
    uint8_t blit_hop, blit_op, blit_ctrl, blit_skew;
    bool blit_defer_start;

    // input for the next frame
    bool keys[128], keys_sent[128];
    unsigned joy_in[2];
    atarist_pointer ptr;

    // the interrupt lines
    bool vbl, hbl, mfp_irq;
    int debt;
    int64_t mfp_acc, audio_acc;
    int16_t audio[ST_AUDIO_MAX];
    int naudio;
    bool turbo;
    const char *error;

    // the picture of the last frame
    uint32_t shown_base;
    int shown_res;
    uint16_t shown_pal[16];
    uint16_t pal565[16];
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
} st_t;

static st_t s;
bool st_ipl_dirty;

// --- RAM ------------------------------------------------------------------------------
static inline uint8_t ram_rd(uint32_t a) { return s.ram[(a >> CHUNK_BITS) & (MAX_CHUNKS - 1)][a & (CHUNK - 1)]; }
static inline void ram_wr(uint32_t a, uint8_t v) { s.ram[(a >> CHUNK_BITS) & (MAX_CHUNKS - 1)][a & (CHUNK - 1)] = v; }
static inline uint16_t ram_rd16(uint32_t a)
{
    const uint8_t *p = s.ram[(a >> CHUNK_BITS) & (MAX_CHUNKS - 1)] + (a & (CHUNK - 1));
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline void ram_wr16(uint32_t a, uint16_t v)
{
    uint8_t *p = s.ram[(a >> CHUNK_BITS) & (MAX_CHUNKS - 1)] + (a & (CHUNK - 1));
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static uint16_t ram_word(uint32_t a) { return a + 1 < s.ram_size ? ram_rd16(a) : 0; }

static uint8_t fdc_ram_read(void *u, uint32_t a) { (void)u; return a < s.ram_size ? ram_rd(a) : 0xff; }
static void fdc_ram_write(void *u, uint32_t a, uint8_t v) { (void)u; if (a < s.ram_size) ram_wr(a, v); }

// --- interrupts -------------------------------------------------------------------------
static int ipl(void) { return s.mfp_irq ? 6 : s.vbl ? 4 : s.hbl ? 2 : 0; }

static void apply_irq(void)
{
    st_ipl_dirty = false;
    m68k_set_irq((unsigned)ipl());
}

void cpu_instr_callback(unsigned int pc)
{
    (void)pc;
    apply_irq();
}

static void on_mfp_irq(void *u, bool asserted)
{
    (void)u;
    s.mfp_irq = asserted;
    st_ipl_dirty = true;
}

int cpu_irq_ack(int level)
{
    int vec = M68K_INT_ACK_AUTOVECTOR;
    if (level == 6) {
        vec = mc68901_irq_ack(&s.mfp);
        if (vec == 0x18) vec = M68K_INT_ACK_SPURIOUS;
    } else if (level == 4) {
        s.vbl = false;
    } else if (level == 2) {
        s.hbl = false;
    }
    st_ipl_dirty = true;                // (the next level: before the handler's first instruction)
    return vec;
}

void cpu_set_fc(int fc) { (void)fc; }

static void ikbd_clear(void)
{
    s.rx_head = s.rx_n = 0;
    s.pend_head = s.pend_n = 0;
}

// The RESET instruction: the peripherals' reset line, not the 68000's
void cpu_pulse_reset(void)
{
    mc68901_reset(&s.mfp);
    st_floppy_reset(&s.fdc);
    ay8910_reset(&s.psg);
    ikbd_clear();
    mc68901_set_gpip_bit(&s.mfp, 7, 1);
    mc68901_set_gpip_bit(&s.mfp, 5, 1);
    mc68901_set_gpip_bit(&s.mfp, 4, 1);
}

static void update_irqs(void) { mc68901_set_gpip_bit(&s.mfp, 5, st_floppy_irq(&s.fdc) ? 0 : 1); }

// --- the IKBD (HD6301) behind the 6850 ACIA ---------------------------------------------
static void ikbd_push(uint8_t v)
{
    // ~1 ms at 8 MHz; the ACIA holds one byte, the others wait for TOS to read it
    if (s.pend_n >= PENDING_MAX) return;
    ikbd_byte_t *b = &s.pending[(s.pend_head + s.pend_n++) % PENDING_MAX];
    b->value = v;
    b->cycles = 8000;
}

static void service_acia(void)
{
    if (s.rx_n || !s.pend_n) return;
    ikbd_byte_t *b = &s.pending[s.pend_head];
    if (b->cycles > 0) return;
    s.rx[(s.rx_head + s.rx_n++) % RX_MAX] = b->value;
    s.acia_rdr = b->value;
    s.pend_head = (s.pend_head + 1) % PENDING_MAX;
    s.pend_n--;
    mc68901_set_gpip_bit(&s.mfp, 4, 0);
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

static uint8_t bcd_inc(uint8_t v)
{
    v = (uint8_t)(v + 1);
    if ((v & 0x0f) > 9) v = (uint8_t)((v & 0xf0) + 0x10);
    return v;
}

static void ikbd_reset_modes(void)
{
    s.mouse_mode = MOUSE_REL;
    s.joy_mode = JOY_EVENT;
    s.paused = false;
    s.mouse_y_bottom = false;
    s.mouse_button_action = 0;
    s.abs_x = s.abs_y = 0;
    s.abs_max_x = 319;
    s.abs_max_y = 199;
    s.abs_buttons = 0;
    s.abs_left = s.abs_right = false;
    s.joy_monitor_rate = 0;
    s.joy_monitor_count = 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ikbd_command(const uint8_t *c)
{
    if (c[0] != 0x13) s.paused = false;
    switch (c[0]) {
    case 0x80:
        if (c[1] != 0x01) return;
        ikbd_reset_modes();
        ikbd_push(0xf1);
        return;
    case 0x07: s.mouse_button_action = c[1]; return;
    case 0x08: s.mouse_mode = MOUSE_REL; return;
    case 0x09:
        s.mouse_mode = MOUSE_ABS;
        s.abs_max_x = (c[1] << 8) | c[2];
        s.abs_max_y = (c[3] << 8) | c[4];
        if (s.abs_x > s.abs_max_x) s.abs_x = s.abs_max_x;
        if (s.abs_y > s.abs_max_y) s.abs_y = s.abs_max_y;
        return;
    case 0x0a: s.mouse_mode = MOUSE_KEYCODE; return;
    case 0x0b: case 0x0c: return;
    case 0x0d: {
        if (s.mouse_mode != MOUSE_ABS) return;
        ikbd_push(0xf7);
        ikbd_push(s.abs_buttons);
        s.abs_buttons = 0;
        const int y = s.mouse_y_bottom ? s.abs_max_y - s.abs_y : s.abs_y;
        ikbd_push((uint8_t)(s.abs_x >> 8));
        ikbd_push((uint8_t)s.abs_x);
        ikbd_push((uint8_t)(y >> 8));
        ikbd_push((uint8_t)y);
        return;
    }
    case 0x0e: {
        s.abs_x = clampi((c[2] << 8) | c[3], 0, s.abs_max_x);
        const int y = clampi((c[4] << 8) | c[5], 0, s.abs_max_y);
        s.abs_y = s.mouse_y_bottom ? s.abs_max_y - y : y;
        return;
    }
    case 0x0f: s.mouse_y_bottom = true; return;
    case 0x10: s.mouse_y_bottom = false; return;
    case 0x11: return;
    case 0x12: s.mouse_mode = MOUSE_OFF; return;
    case 0x13: s.paused = true; return;
    case 0x14: s.joy_mode = JOY_EVENT; s.mouse_mode = MOUSE_OFF; return;
    case 0x15: s.joy_mode = JOY_INTERROGATE; s.mouse_mode = MOUSE_OFF; return;
    case 0x16:
        ikbd_push(0xfd);
        ikbd_push(s.joy_state[0]);
        ikbd_push(s.joy_state[1]);
        return;
    case 0x17:
        s.joy_mode = JOY_MONITOR;
        s.mouse_mode = MOUSE_OFF;
        s.joy_monitor_rate = c[1];
        s.joy_monitor_count = 0;
        return;
    case 0x18: case 0x19: case 0x1a: s.joy_mode = JOY_OFF; return;
    case 0x1b:
        for (int i = 0; i < 6; i++)
            if ((c[1 + i] & 0x0f) <= 9 && (c[1 + i] >> 4) <= 9) s.clock[i] = c[1 + i];
        s.clock_frames = 0;
        return;
    case 0x1c:
        ikbd_push(0xfc);
        for (int i = 0; i < 6; i++) ikbd_push(s.clock[i]);
        return;
    case 0x20: case 0x22: return;
    case 0x21:
        ikbd_push(0xf6);
        ikbd_push(0x20);
        for (int i = 0; i < 6; i++) ikbd_push(0);
        return;
    default: break;
    }
    if (c[0] >= 0x87 && c[0] <= 0x9a) {
        uint8_t reply[7] = { 0 };
        switch (c[0]) {
        case 0x87: reply[0] = 0x07; reply[1] = s.mouse_button_action; break;
        case 0x88:
            reply[0] = s.mouse_mode == MOUSE_ABS ? 0x09 : s.mouse_mode == MOUSE_KEYCODE ? 0x0a : 0x08;
            if (s.mouse_mode == MOUSE_ABS) {
                reply[1] = (uint8_t)(s.abs_max_x >> 8);
                reply[2] = (uint8_t)s.abs_max_x;
                reply[3] = (uint8_t)(s.abs_max_y >> 8);
                reply[4] = (uint8_t)s.abs_max_y;
            }
            break;
        case 0x8b: reply[0] = 0x0b; reply[1] = 1; reply[2] = 1; break;
        case 0x8c: reply[0] = 0x0c; reply[1] = 1; reply[2] = 1; break;
        case 0x8f: case 0x90: reply[0] = s.mouse_y_bottom ? 0x0f : 0x10; break;
        case 0x92: reply[0] = s.mouse_mode == MOUSE_OFF ? 0x12 : 0x00; break;
        case 0x94: case 0x95: case 0x99: reply[0] = s.joy_mode == JOY_INTERROGATE ? 0x15 : 0x14; break;
        case 0x9a: reply[0] = s.joy_mode == JOY_OFF ? 0x1a : 0x00; break;
        default: break;
        }
        ikbd_push(0xf6);
        for (int i = 0; i < 7; i++) ikbd_push(reply[i]);
    }
}

static void ikbd_byte(uint8_t v)
{
    if (s.cmd_n == 0) {
        const int need = ikbd_command_length(v);
        if (need == 0) return;
        s.cmd_need = need;
    }
    if (s.cmd_n < (int)sizeof s.cmd) s.cmd[s.cmd_n] = v;
    s.cmd_n++;
    if (s.cmd[0] == 0x20 && s.cmd_n == 4) s.cmd_need = 4 + s.cmd[3];
    if (s.cmd_n < s.cmd_need) return;
    uint8_t c[sizeof s.cmd];
    memcpy(c, s.cmd, sizeof c);
    s.cmd_n = 0;
    s.cmd_need = 0;
    ikbd_command(c);
}

static void ikbd_mouse_packet(int dx, int dy, bool left, bool right)
{
    uint8_t head = 0xf8;
    if (left) head |= 2;
    if (right) head |= 1;
    ikbd_push(head);
    ikbd_push((uint8_t)dx);
    ikbd_push((uint8_t)dy);
}

static void ikbd_mouse_report(int dx, int dy, bool left, bool right)
{
    if (s.paused) return;
    const bool left_changed = left != s.abs_left, right_changed = right != s.abs_right;
    if (left_changed) s.abs_buttons |= (uint8_t)(left ? 0x04 : 0x08);
    if (right_changed) s.abs_buttons |= (uint8_t)(right ? 0x01 : 0x02);
    s.abs_left = left;
    s.abs_right = right;
    switch (s.mouse_mode) {
    case MOUSE_REL:
        ikbd_mouse_packet(dx, s.mouse_y_bottom ? -dy : dy, left, right);
        return;
    case MOUSE_ABS:
        s.abs_x = clampi(s.abs_x + dx, 0, s.abs_max_x);
        s.abs_y = clampi(s.abs_y + dy, 0, s.abs_max_y);
        if (s.mouse_button_action & 0x04) {
            if (left_changed) ikbd_push(left ? 0x74 : 0xf4);
            if (right_changed) ikbd_push(right ? 0x75 : 0xf5);
        }
        return;
    default:
        return;
    }
}

static void ikbd_joysticks(void)
{
    // joystick 1 is the game port, joystick 0 shares the mouse port
    const uint8_t now[2] = { (uint8_t)s.joy_in[0], (uint8_t)s.joy_in[1] };
    for (int j = 0; j < 2; j++) {
        const bool changed = now[j] != s.joy_state[j];
        s.joy_state[j] = now[j];
        if (!changed || s.paused || s.joy_mode != JOY_EVENT) continue;
        if (j == 0 && s.mouse_mode != MOUSE_OFF) continue;
        ikbd_push((uint8_t)(0xfe + j));
        ikbd_push(s.joy_state[j]);
    }
    if (s.joy_mode == JOY_MONITOR && !s.paused) {
        if (++s.joy_monitor_count * 2 >= (s.joy_monitor_rate > 2 ? s.joy_monitor_rate : 2)) {
            s.joy_monitor_count = 0;
            ikbd_push((uint8_t)(((s.joy_state[1] >> 7) << 1) | (s.joy_state[0] >> 7)));
            ikbd_push((uint8_t)(((s.joy_state[1] & 0x0f) << 4) | (s.joy_state[0] & 0x0f)));
        }
    }
}

static void ikbd_clock_tick(void)
{
    if (++s.clock_frames < 50) return;
    s.clock_frames = 0;
    static const uint8_t kLimit[6] = { 0x99, 0x12, 0x31, 0x23, 0x59, 0x59 };
    for (int i = 5; i >= 3; i--) {
        if (s.clock[i] < kLimit[i]) {
            s.clock[i] = bcd_inc(s.clock[i]);
            return;
        }
        s.clock[i] = 0;
    }
}

// GEM's cursor kept under the host pointer while it rests (dsp-cpp's
// gem_sync, through Line-A's GCURX / GCURY)
static bool linea_valid(void)
{
    if (s.linea < 0x400 || s.linea + 4 >= s.ram_size) return false;
    static const int kHz[3] = { 320, 640, 640 }, kVt[3] = { 200, 200, 400 }, kPlanes[3] = { 4, 2, 1 };
    const int mode = (s.resolution & 3) < 2 ? (s.resolution & 3) : 2;
    return ram_word(s.linea) == kPlanes[mode] && ram_word(s.linea - 0xc) == kHz[mode] &&
           ram_word(s.linea - 0x4) == kVt[mode] && ram_word(s.linea - 0x2) == ram_word(s.linea + 2) &&
           ram_word(s.linea + 2) == kHz[mode] * kPlanes[mode] / 8;
}

static void linea_find(void)
{
    const uint32_t end = s.ram_size < 0x20000 ? s.ram_size : 0x20000;
    for (uint32_t a = 0x800; a < end; a += 2) {
        s.linea = a;
        if (linea_valid()) return;
    }
    s.linea = 0;
}

static void gem_sync(int target_x, int target_y)
{
    if (s.mouse_mode != MOUSE_REL || s.paused || s.rx_n || s.pend_n) return;
    if (!linea_valid()) {
        if (s.linea_scan_wait > 0) {
            --s.linea_scan_wait;
            return;
        }
        s.linea_scan_wait = 25;
        linea_find();
        s.sync_sent = s.sync_off = false;
        s.sync_fail = 0;
        if (!linea_valid()) return;
    }
    const int gx = (int16_t)ram_word(s.linea - 0x25a);
    const int gy = (int16_t)ram_word(s.linea - 0x258);
    if (s.sync_sent) {
        s.sync_sent = false;
        if (gx == s.sync_from_x && gy == s.sync_from_y) {
            if (++s.sync_fail >= 3) s.sync_off = true;
        } else {
            s.sync_fail = 0;
        }
    }
    if (s.sync_off) {
        if (s.sync_rest_x >= 0 && (gx != s.sync_rest_x || gy != s.sync_rest_y)) {
            s.sync_off = false;
            s.sync_fail = 0;
        }
        s.sync_rest_x = gx;
        s.sync_rest_y = gy;
        return;
    }
    const int mode = (s.resolution & 3) < 2 ? (s.resolution & 3) : 2;
    const int tx = clampi(target_x, 0, (mode == 0 ? 320 : 640) - 1);
    const int ty = clampi(target_y, 0, (mode == 2 ? 400 : 200) - 1);
    const int dx = clampi(tx - gx, -127, 127);
    const int dy = clampi(ty - gy, -127, 127);
    if (!dx && !dy) return;
    s.sync_from_x = gx;
    s.sync_from_y = gy;
    s.sync_sent = true;
    ikbd_mouse_report(dx, dy, s.last_b1, s.last_b2);
}

static void ikbd_mouse(const atarist_pointer *in)
{
    if (!in->has_pointer) {
        s.pointer_seen = false;
        s.seed_valid = false;
        return;
    }
    if (in->resync) {
        s.pointer_seen = false;
        s.seed_valid = false;
    }
    int dx_host = 0, dy_host = 0;
    if (in->relative) {
        dx_host = in->dx;
        dy_host = in->dy;
        s.pointer_seen = true;
    } else if (!s.pointer_seen) {
        if (!s.seed_valid || (in->x == s.seed_x && in->y == s.seed_y)) {
            s.seed_valid = true;
            s.seed_x = in->x;
            s.seed_y = in->y;
            return;
        }
        s.last_pointer_x = in->x;
        s.last_pointer_y = in->y;
        s.last_b1 = in->left;
        s.last_b2 = in->right;
        s.pointer_seen = true;
        s.pointer_frac_x = s.pointer_frac_y = 0;
        for (int i = 0; i < 6; i++) ikbd_mouse_report(-127, -127, in->left, in->right);
        dx_host = in->x;
        dy_host = in->y;
    } else {
        dx_host = in->x - s.last_pointer_x;
        dy_host = in->y - s.last_pointer_y;
        s.last_pointer_x = in->x;
        s.last_pointer_y = in->y;
        if (in->x <= 0) dx_host -= 16;
        if (in->x >= ST_W - 1) dx_host += 16;
        if (in->y <= 0) dy_host -= 16;
        if (in->y >= ST_H - 1) dy_host += 16;
        if (!dx_host && !dy_host && in->left == s.last_b1 && in->right == s.last_b2) {
            const int mode = s.resolution & 3;
            gem_sync(in->x / (mode == 0 ? 2 : 1), in->y / (mode == 2 ? 1 : 2));
            return;
        }
    }
    const int mode = s.resolution & 3;
    const int xs = mode == 0 ? 2 : 1, ys = mode == 2 ? 1 : 2;
    s.pointer_frac_x += dx_host;
    s.pointer_frac_y += dy_host;
    int dx = s.pointer_frac_x / xs, dy = s.pointer_frac_y / ys;
    s.pointer_frac_x -= dx * xs;
    s.pointer_frac_y -= dy * ys;
    const bool left = in->left, right = in->right;
    const bool bchange = left != s.last_b1 || right != s.last_b2;
    s.last_b1 = left;
    s.last_b2 = right;
    if (!dx && !dy && !bchange) return;
    bool send_button = bchange;
    while (dx || dy || send_button) {
        const int sx = clampi(dx, -127, 127), sy = clampi(dy, -127, 127);
        ikbd_mouse_report(sx, sy, left, right);
        dx -= sx;
        dy -= sy;
        send_button = false;
        if (!dx && !dy) break;
    }
}

// dsp-cpp's ikbd_keys: the keys' changes (in scan code order here), the
// mouse, the joysticks
static void ikbd_inputs(void)
{
    for (int k = 1; k < 128; k++) {
        if (s.keys[k] == s.keys_sent[k]) continue;
        ikbd_push((uint8_t)(s.keys[k] ? k : (k | 0x80)));
        s.keys_sent[k] = s.keys[k];
    }
    ikbd_mouse(&s.ptr);
    s.ptr.resync = false;
    s.ptr.dx = s.ptr.dy = 0;
    ikbd_joysticks();
}

static uint8_t acia_status(void)
{
    uint8_t v = 0x02;
    if (s.rx_n) v |= 0x81;
    return v;
}

static uint8_t acia_read_data(void)
{
    if (!s.rx_n) return s.acia_rdr;
    s.acia_rdr = s.rx[s.rx_head];
    s.rx_head = (s.rx_head + 1) % RX_MAX;
    s.rx_n--;
    if (!s.rx_n) mc68901_set_gpip_bit(&s.mfp, 4, 1);
    return s.acia_rdr;
}

static void acia_write_control(uint8_t v)
{
    s.acia_control = v;
    if ((v & 3) == 3) {
        ikbd_clear();
        mc68901_set_gpip_bit(&s.mfp, 4, 1);
    }
}

// --- the blitter -------------------------------------------------------------------------
static uint16_t blit_get_word(uint32_t a)
{
    a &= 0xfffffe;
    if (a >= 0xff8a00 && a < 0xff8a20) return s.blit_halftone[(a - 0xff8a00) >> 1];
    switch (a) {
    case 0xff8a20: return (uint16_t)s.blit_sxinc;
    case 0xff8a22: return (uint16_t)s.blit_syinc;
    case 0xff8a24: return (uint16_t)(s.blit_src >> 16);
    case 0xff8a26: return (uint16_t)s.blit_src;
    case 0xff8a28: return s.blit_emask[0];
    case 0xff8a2a: return s.blit_emask[1];
    case 0xff8a2c: return s.blit_emask[2];
    case 0xff8a2e: return (uint16_t)s.blit_dxinc;
    case 0xff8a30: return (uint16_t)s.blit_dyinc;
    case 0xff8a32: return (uint16_t)(s.blit_dst >> 16);
    case 0xff8a34: return (uint16_t)s.blit_dst;
    case 0xff8a36: return s.blit_xcount;
    case 0xff8a38: return s.blit_ycount;
    default: return 0;
    }
}

static void blit_set_word(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a >= 0xff8a00 && a < 0xff8a20) {
        s.blit_halftone[(a - 0xff8a00) >> 1] = v;
        return;
    }
    switch (a) {
    case 0xff8a20: s.blit_sxinc = (int16_t)v; return;
    case 0xff8a22: s.blit_syinc = (int16_t)v; return;
    case 0xff8a24: s.blit_src = (s.blit_src & 0xffff) | ((uint32_t)v << 16); return;
    case 0xff8a26: s.blit_src = (s.blit_src & 0xffff0000u) | v; return;
    case 0xff8a28: s.blit_emask[0] = v; return;
    case 0xff8a2a: s.blit_emask[1] = v; return;
    case 0xff8a2c: s.blit_emask[2] = v; return;
    case 0xff8a2e: s.blit_dxinc = (int16_t)v; return;
    case 0xff8a30: s.blit_dyinc = (int16_t)v; return;
    case 0xff8a32: s.blit_dst = (s.blit_dst & 0xffff) | ((uint32_t)v << 16); return;
    case 0xff8a34: s.blit_dst = (s.blit_dst & 0xffff0000u) | v; return;
    case 0xff8a36: s.blit_xcount = v; return;
    case 0xff8a38: s.blit_ycount = v; return;
    default: return;
    }
}

static uint16_t rom_word_at(uint32_t a, uint32_t base)
{
    if (a >= base && a + 1 < base + ST_ROM_SIZE) {
        const uint32_t o = a - base;
        return (uint16_t)((s.rom[o] << 8) | s.rom[o + 1]);
    }
    return 0;
}

static uint16_t blit_mem_read(uint32_t a)
{
    a &= 0xfffffe;
    if (a + 1 < s.ram_size) return ram_rd16(a);
    if (a >= 0xfc0000) return rom_word_at(a, 0xfc0000);
    if (a >= 0xe00000) return rom_word_at(a, 0xe00000);
    return 0xffff;
}

static void blit_mem_write(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a + 1 < s.ram_size) ram_wr16(a, v);
}

static void run_blitter(void)
{
    if (s.blit_ycount == 0) {
        s.blit_ctrl &= 0x3f;
        return;
    }
    const int hop = s.blit_hop & 3, op = s.blit_op & 15, skew = s.blit_skew & 15;
    const bool fxsr = (s.blit_skew & 0x80) != 0, nfsr = (s.blit_skew & 0x40) != 0;
    const bool smudge = (s.blit_ctrl & 0x20) != 0;
    int line = s.blit_ctrl & 15;
    static const bool kOpSrc[16] = { 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0 };
    static const bool kOpDst[16] = { 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0 };
    const bool hop_src = (hop & 2) != 0 || (hop == 1 && smudge);
    const bool need_src = kOpSrc[op] && hop_src;
    const uint32_t xspan = s.blit_xcount ? s.blit_xcount : 65536u;
    uint32_t yleft = s.blit_ycount;
    uint32_t src = s.blit_src & 0xfffffe, dst = s.blit_dst & 0xfffffe;
    uint32_t buffer = 0, words = 0;
    const uint32_t kMaxWords = 0x100000;
#define ADD_SRC(inc) (src = (uint32_t)((int32_t)src + (inc)) & 0xfffffe)
#define ADD_DST(inc) (dst = (uint32_t)((int32_t)dst + (inc)) & 0xfffffe)
#define SHIFT_BUF() do { if (s.blit_sxinc < 0) buffer >>= 16; else buffer <<= 16; } while (0)
#define FETCH_SRC() do { SHIFT_BUF(); const uint32_t w_ = blit_mem_read(src); \
                         if (s.blit_sxinc < 0) buffer |= w_ << 16; else buffer |= w_; } while (0)
    while (yleft && words < kMaxWords) {
        uint32_t xleft = xspan;
        bool skip_src = false;
        while (xleft && words < kMaxWords) {
            const bool first = xleft == xspan, last = xleft == 1;
            uint16_t mask = s.blit_emask[1];
            if (first || xspan == 1) mask = s.blit_emask[0];
            else if (last) mask = s.blit_emask[2];
            bool fetched = false;
            if (need_src) {
                if (first && fxsr) {
                    FETCH_SRC();
                    ADD_SRC(s.blit_sxinc);
                }
                if (!skip_src) {
                    FETCH_SRC();
                    fetched = true;
                } else {
                    SHIFT_BUF();
                }
            }
            const uint16_t srcw = (uint16_t)(buffer >> skew);
            const uint16_t ht = smudge ? s.blit_halftone[srcw & 15] : s.blit_halftone[line];
            uint16_t hopv = 0xffff;
            if (hop == 1) hopv = ht;
            else if (hop == 2) hopv = srcw;
            else if (hop == 3) hopv = (uint16_t)(srcw & ht);
            const bool read_dst = kOpDst[op] || mask != 0xffff;
            const uint16_t dstw = read_dst ? blit_mem_read(dst) : 0;
            uint16_t lop;
            switch (op) {
            case 0: lop = 0; break;
            case 1: lop = hopv & dstw; break;
            case 2: lop = hopv & (uint16_t)~dstw; break;
            case 3: lop = hopv; break;
            case 4: lop = (uint16_t)~hopv & dstw; break;
            case 5: lop = dstw; break;
            case 6: lop = hopv ^ dstw; break;
            case 7: lop = hopv | dstw; break;
            case 8: lop = (uint16_t)(~hopv & ~dstw); break;
            case 9: lop = (uint16_t)(~hopv ^ dstw); break;
            case 10: lop = (uint16_t)~dstw; break;
            case 11: lop = (uint16_t)(hopv | ~dstw); break;
            case 12: lop = (uint16_t)~hopv; break;
            case 13: lop = (uint16_t)(~hopv | dstw); break;
            case 14: lop = (uint16_t)(~hopv | ~dstw); break;
            default: lop = 0xffff; break;
            }
            blit_mem_write(dst, (uint16_t)((lop & mask) | (dstw & ~mask)));
            words++;
            if (xleft == 2 && nfsr) skip_src = true;
            if (fetched) {
                if (last || skip_src) ADD_SRC(s.blit_syinc);
                else ADD_SRC(s.blit_sxinc);
            }
            if (last) {
                ADD_DST(s.blit_dyinc);
                line = s.blit_dyinc >= 0 ? ((line + 1) & 15) : ((line - 1) & 15);
            } else {
                ADD_DST(s.blit_dxinc);
            }
            xleft--;
        }
        yleft--;
    }
#undef ADD_SRC
#undef ADD_DST
#undef SHIFT_BUF
#undef FETCH_SRC
    s.blit_src = src;
    s.blit_dst = dst;
    s.blit_xcount = s.blit_xcount ? s.blit_xcount : (uint16_t)xspan;
    s.blit_ycount = 0;
    s.blit_ctrl = (uint8_t)((s.blit_ctrl & 0x30) | (line & 15));
}

// --- the memory map ----------------------------------------------------------------------
static uint8_t io_read_byte(uint32_t a)
{
    switch (a) {
    case 0xff8001: return s.memcfg;
    case 0xff8201: return s.video_hi;
    case 0xff8203: return s.video_mid;
    case 0xff8205: return (uint8_t)(s.video_count >> 16);
    case 0xff8207: return (uint8_t)(s.video_count >> 8);
    case 0xff8209: return (uint8_t)s.video_count;
    case 0xff820a: return s.sync_mode;
    case 0xff8260: return s.resolution;
    case 0xff8604: {
        const uint16_t v = st_floppy_dma_data_r(&s.fdc);
        update_irqs();
        return (uint8_t)(v >> 8);
    }
    case 0xff8605: {
        const uint16_t v = st_floppy_dma_data_r(&s.fdc);
        update_irqs();
        return (uint8_t)v;
    }
    case 0xff8606: return (uint8_t)(st_floppy_dma_status(&s.fdc) >> 8);
    case 0xff8607: return (uint8_t)st_floppy_dma_status(&s.fdc);
    case 0xff8609: return st_floppy_dma_addr_r(&s.fdc, 0);
    case 0xff860b: return st_floppy_dma_addr_r(&s.fdc, 1);
    case 0xff860d: return st_floppy_dma_addr_r(&s.fdc, 2);
    case 0xff8800: case 0xff8801: return ay8910_data_r(&s.psg);
    case 0xfffc00: return acia_status();
    case 0xfffc02: return acia_read_data();
    case 0xfffc04: return 0x02;
    case 0xfffc06: return 0;
    default: break;
    }
    if (a >= 0xff8240 && a < 0xff8260) {
        const uint16_t w = s.palette[(a - 0xff8240) >> 1];
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (a >= 0xfffa00 && a <= 0xfffa2f && (a & 1)) return mc68901_read(&s.mfp, (int)((a - 0xfffa01) >> 1));
    if (a >= 0xff8a00 && a <= 0xff8a3d) {
        if (a == 0xff8a3a) return s.blit_hop;
        if (a == 0xff8a3b) return s.blit_op;
        if (a == 0xff8a3c) return s.blit_ctrl;
        if (a == 0xff8a3d) return s.blit_skew;
        const uint16_t w = blit_get_word(a & 0xfffffe);
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    return 0xff;
}

unsigned int ST_HOT cpu_read_byte(unsigned int a)
{
    a &= 0xffffff;
    if (s.rom_at_zero && a < 8) return s.rom[a];
    if (a < s.ram_size) return ram_rd(a);
    if (a >= 0xfc0000) {
        if (a < 0xfc0000 + ST_ROM_SIZE) return s.rom[a - 0xfc0000];
    }
    if (a >= 0xff8000) return io_read_byte(a);
    return 0xff;
}

static void io_write_byte(uint32_t a, uint8_t v)
{
    switch (a) {
    case 0xff8001: s.memcfg = v; s.rom_at_zero = false; return;
    case 0xff8201: s.video_hi = v; return;
    case 0xff8203: s.video_mid = v; return;
    case 0xff820d: s.video_lo = v; return;
    case 0xff820a: s.sync_mode = v; return;
    case 0xff8260: s.resolution = (uint8_t)(v & 3); return;
    case 0xff8604: st_floppy_dma_data_w(&s.fdc, v); update_irqs(); return;
    case 0xff8605:
        st_floppy_dma_data_w(&s.fdc, (uint16_t)((st_floppy_dma_mode(&s.fdc) & 0xff00) | v));
        update_irqs();
        return;
    case 0xff8606: st_floppy_dma_mode_w(&s.fdc, (uint16_t)(v << 8)); return;
    case 0xff8607: st_floppy_dma_mode_w(&s.fdc, v); return;
    case 0xff8609: st_floppy_dma_addr_w(&s.fdc, 0, v); return;
    case 0xff860b: st_floppy_dma_addr_w(&s.fdc, 1, v); return;
    case 0xff860d: st_floppy_dma_addr_w(&s.fdc, 2, v); return;
    case 0xff8800: case 0xff8801: ay8910_address_w(&s.psg, v); return;
    case 0xff8802: case 0xff8803: ay8910_data_w(&s.psg, v); return;
    case 0xfffc00: acia_write_control(v); return;
    case 0xfffc02: ikbd_byte(v); return;
    default: break;
    }
    if (a >= 0xff8240 && a < 0xff8260) {
        const int n = (int)(a - 0xff8240) >> 1;
        uint16_t w = s.palette[n];
        if (a & 1) w = (uint16_t)((w & 0xff00) | v);
        else w = (uint16_t)((w & 0x00ff) | (v << 8));
        s.palette[n] = (uint16_t)(w & 0x0777);
        return;
    }
    if (a >= 0xfffa00 && a <= 0xfffa2f && (a & 1)) {
        mc68901_write(&s.mfp, (int)((a - 0xfffa01) >> 1), v);
        return;
    }
    if (a >= 0xff8a00 && a <= 0xff8a3d) {
        if (a == 0xff8a3a) { s.blit_hop = (uint8_t)(v & 3); return; }
        if (a == 0xff8a3b) { s.blit_op = (uint8_t)(v & 15); return; }
        if (a == 0xff8a3c) {
            s.blit_ctrl = v;
            if ((v & 0x80) && !s.blit_defer_start) run_blitter();
            return;
        }
        if (a == 0xff8a3d) { s.blit_skew = v; return; }
        const uint32_t even = a & 0xfffffe;
        uint16_t w = blit_get_word(even);
        if (a & 1) w = (uint16_t)((w & 0xff00) | v);
        else w = (uint16_t)((w & 0x00ff) | (v << 8));
        blit_set_word(even, w);
    }
}

void ST_HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    a &= 0xffffff;
    if (s.rom_at_zero && a < 8) return;
    if (a < s.ram_size) {
        ram_wr(a, (uint8_t)v);
        return;
    }
    if (a >= 0xff8000) io_write_byte(a, (uint8_t)v);
}

unsigned int ST_HOT cpu_read_word(unsigned int a)
{
    a &= 0xfffffe;
    if (s.rom_at_zero && a < 8) return (unsigned)((s.rom[a] << 8) | s.rom[a + 1]);
    if (a + 1 < s.ram_size) return ram_rd16(a);
    if (a >= 0xfc0000 && a + 1 < 0xfc0000 + ST_ROM_SIZE) {
        const uint32_t o = a - 0xfc0000;
        return (unsigned)((s.rom[o] << 8) | s.rom[o + 1]);
    }
    if (a == 0xff8604) {
        const uint16_t v = st_floppy_dma_data_r(&s.fdc);
        update_irqs();
        return v;
    }
    if (a == 0xff8606) return st_floppy_dma_status(&s.fdc);
    if (a == 0xff8800) return (unsigned)(ay8910_data_r(&s.psg) << 8);
    return (cpu_read_byte(a) << 8) | cpu_read_byte(a + 1);
}

void ST_HOT cpu_write_word(unsigned int a, unsigned int v)
{
    a &= 0xfffffe;
    if (s.rom_at_zero && a < 8) return;
    if (a + 1 < s.ram_size) {
        ram_wr16(a, (uint16_t)v);
        return;
    }
    if (a == 0xff8604) {
        st_floppy_dma_data_w(&s.fdc, (uint16_t)v);
        update_irqs();
        return;
    }
    if (a == 0xff8606) {
        st_floppy_dma_mode_w(&s.fdc, (uint16_t)v);
        return;
    }
    if (a == 0xff8800) {
        ay8910_address_w(&s.psg, (uint8_t)(v >> 8));
        ay8910_data_w(&s.psg, (uint8_t)v);
        return;
    }
    if (a >= 0xff8240 && a < 0xff8260) {
        s.palette[(a - 0xff8240) >> 1] = (uint16_t)(v & 0x0777);
        return;
    }
    if (a == 0xff8a3c) {
        s.blit_defer_start = true;
        cpu_write_byte(a, (v >> 8) & 0xff);
        s.blit_defer_start = false;
        cpu_write_byte(a + 1, v & 0xff);
        if (s.blit_ctrl & 0x80) run_blitter();
        return;
    }
    cpu_write_byte(a, (v >> 8) & 0xff);
    cpu_write_byte(a + 1, v & 0xff);
}

unsigned int ST_HOT cpu_read_long(unsigned int a) { return (cpu_read_word(a) << 16) | cpu_read_word(a + 2); }
void ST_HOT cpu_write_long(unsigned int a, unsigned int v)
{
    cpu_write_word(a, v >> 16);
    cpu_write_word(a + 2, v & 0xffff);
}
void cpu_write_long_pd(unsigned int a, unsigned int v)
{
    cpu_write_word(a + 2, v & 0xffff);
    cpu_write_word(a, v >> 16);
}
unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }

// --- running -------------------------------------------------------------------------------
static void on_psg_port_a(void *u, uint8_t v)
{
    (void)u;
    s.psg_port_a = v;
    st_floppy_set_psg_port_a(&s.fdc, v);
}

static void ST_HOT on_cycles(int cycles)
{
    st_floppy_tick(&s.fdc, cycles);
    if (s.pend_n) s.pending[s.pend_head].cycles -= cycles;
    service_acia();
    update_irqs();
    s.mfp_acc += (int64_t)cycles * MC68901_CLOCK;
    if (s.mfp_acc >= ST_CPU_CLOCK) {
        const int ticks = (int)(s.mfp_acc / ST_CPU_CLOCK);
        s.mfp_acc -= (int64_t)ticks * ST_CPU_CLOCK;
        mc68901_tick(&s.mfp, ticks);
    }
    s.audio_acc += (int64_t)cycles * s.rate;
    while (s.audio_acc >= ST_CPU_CLOCK) {
        s.audio_acc -= ST_CPU_CLOCK;
        int32_t v = ay8910_render(&s.psg);
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        if (s.naudio < ST_AUDIO_MAX) s.audio[s.naudio++] = (int16_t)v;
    }
}

static void run_cycles(int cycles)
{
    int left = cycles - s.debt;
    while (left > 0) {
        if (st_ipl_dirty) apply_irq();
        const int run = left < SLICE ? left : SLICE;
        const int used = m68k_execute(run);
        on_cycles(used);
        left -= used;
    }
    s.debt = -left;
}

int atarist_run_frame(const int16_t **audio)
{
    s.naudio = 0;
    ikbd_inputs();
    const uint32_t vbase = ((uint32_t)s.video_hi << 16) | ((uint32_t)s.video_mid << 8) | s.video_lo;
    const uint32_t pitch = (s.resolution & 3) == 2 ? 80 : 160;
    s.vbl = true;
    apply_irq();
    for (int line = 0; line < LINES; line++) {
        s.video_count = line >= 63 && line < 263 ? vbase + (uint32_t)(line - 63) * pitch : vbase;
        mc68901_pulse_tb(&s.mfp);
        s.hbl = true;
        apply_irq();
        run_cycles(CYCLES_PER_LINE);
        update_irqs();
    }
    ikbd_clock_tick();
    // the picture as dsp-cpp's render() takes it, at the end of the frame
    s.shown_base = ((uint32_t)s.video_hi << 16) | ((uint32_t)s.video_mid << 8) | s.video_lo;
    s.shown_res = s.resolution & 3;
    memcpy(s.shown_pal, s.palette, sizeof s.shown_pal);
    if (s.rgb565)
        for (int i = 0; i < 16; i++) {
            const uint16_t w = s.shown_pal[i];
            s.pal565[i] = s.rgb565((uint8_t)(((w >> 8) & 7) * 36), (uint8_t)(((w >> 4) & 7) * 36), (uint8_t)((w & 7) * 36));
        }
    if (audio) *audio = s.audio;
    return s.turbo ? 0 : s.naudio;
}

// --- the picture ----------------------------------------------------------------------------
static uint32_t st_color(uint16_t w)
{
    return (uint32_t)(((w >> 8) & 7) * 36) << 16 | (uint32_t)(((w >> 4) & 7) * 36) << 8 | (uint32_t)((w & 7) * 36);
}

// One ST line's colour indices, 640 (high / medium) or 320 (low) of them;
// false: out of RAM (the background colour)
static bool line_indices(int line, uint8_t *idx, int *n)
{
    const int mode = s.shown_res;
    const uint32_t row = s.shown_base + (uint32_t)line * (mode == 2 ? 80u : 160u);
    if (mode == 2) {
        *n = 640;
        for (int x = 0; x < 640; x += 16) {
            const uint32_t o = row + (uint32_t)(x / 8);
            const uint16_t w = o + 1 < s.ram_size ? ram_rd16(o) : 0;
            for (int b = 0; b < 16; b++) idx[x + b] = (w & (0x8000 >> b)) ? 1 : 0;
        }
        return true;
    }
    if (mode == 1) {
        *n = 640;
        for (int x = 0; x < 640; x += 16) {
            const uint32_t o = row + (uint32_t)(x / 4);
            if (o + 3 >= s.ram_size) {
                memset(idx + x, 0, 16);
                continue;
            }
            uint16_t p0 = ram_rd16(o), p1 = ram_rd16(o + 2);
            for (int b = 0; b < 16; b++) {
                idx[x + b] = (uint8_t)(((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1));
                p0 <<= 1;
                p1 <<= 1;
            }
        }
        return true;
    }
    *n = 320;
    for (int x = 0; x < 320; x += 16) {
        const uint32_t o = row + (uint32_t)(x / 2);
        if (o + 7 >= s.ram_size) {
            memset(idx + x, 0, 16);
            continue;
        }
        uint16_t p0 = ram_rd16(o), p1 = ram_rd16(o + 2), p2 = ram_rd16(o + 4), p3 = ram_rd16(o + 6);
        for (int b = 0; b < 16; b++) {
            idx[x + b] = (uint8_t)(((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1) | (((p2 >> 15) & 1) << 2) |
                                   (((p3 >> 15) & 1) << 3));
            p0 <<= 1;
            p1 <<= 1;
            p2 <<= 1;
            p3 <<= 1;
        }
    }
    return true;
}

void atarist_render_row(int y, uint32_t *out)
{
    uint8_t idx[640];
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = st_color(s.shown_pal[i]);
    int n;
    line_indices(s.shown_res == 2 ? y : y >> 1, idx, &n);
    if (n == 640) {
        for (int x = 0; x < 640; x++) out[x] = pal[idx[x]];
    } else {
        for (int x = 0; x < 320; x++) out[2 * x] = out[2 * x + 1] = pal[idx[x]];
    }
}

void atarist_render_row320(int y, uint16_t *out)
{
    uint8_t idx[640];
    int n;
    line_indices(s.shown_res == 2 ? y * 2 : y, idx, &n);
    if (n == 320) {
        for (int x = 0; x < 320; x++) out[x] = s.pal565[idx[x]];
        return;
    }
    // two pixels into one: their colours blended (text stays readable)
    for (int x = 0; x < 320; x++) {
        const uint16_t a = s.pal565[idx[2 * x]], b = s.pal565[idx[2 * x + 1]];
        out[x] = a == b ? a : (uint16_t)(((a & 0xf7de) >> 1) + ((b & 0xf7de) >> 1));
    }
}

static uint16_t rgb565_default(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void atarist_set_rgb565(uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b)) { s.rgb565 = fn ? fn : rgb565_default; }
int atarist_resolution(void) { return s.shown_res; }

// --- set-up ---------------------------------------------------------------------------------
bool atarist_init(const uint8_t *tos, size_t tos_size, uint32_t ram_size, int audio_rate)
{
    if (!tos || tos_size < ST_ROM_SIZE) return false;
    // (again: a new TOS or memory size, the old RAM and disks out first)
    for (uint32_t i = 0; i < MAX_CHUNKS; i++) {
        free(s.ram[i]);
        s.ram[i] = NULL;
    }
    s.ram_size = 0;
    st_floppy_eject(&s.fdc, 0);
    st_floppy_eject(&s.fdc, 1);
    if (ram_size < CHUNK) ram_size = CHUNK;
    if (ram_size > 0x100000) ram_size = 0x100000;
    ram_size &= ~(CHUNK - 1);
    for (uint32_t i = 0; i < ram_size >> CHUNK_BITS; i++) {
        s.ram[i] = calloc(1, CHUNK);
        if (!s.ram[i]) {
            for (uint32_t j = 0; j < i; j++) free(s.ram[j]);
            memset(s.ram, 0, sizeof s.ram);
            return false;
        }
    }
    s.ram_size = ram_size;
    s.rom = tos;
    if (!s.rgb565) s.rgb565 = rgb565_default;
    s.rate = audio_rate > 0 ? audio_rate : ST_AUDIO_RATE;
    ay8910_init(&s.psg, 2000000, s.rate);
    ay8910_set_amplitude(&s.psg, 1.2f);
    s.psg.port_a_write = on_psg_port_a;
    s.mfp.irq_cb = on_mfp_irq;
    st_floppy_init(&s.fdc);
    s.fdc.ram_read = fdc_ram_read;
    s.fdc.ram_write = fdc_ram_write;
    s.fdc.ram_size = ram_size;
    static const uint8_t kClock[6] = { 0x89, 0x01, 0x01, 0x00, 0x00, 0x00 };
    memcpy(s.clock, kClock, sizeof s.clock);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    atarist_reset();
    return true;
}

uint32_t atarist_ram_size(void) { return s.ram_size; }

void atarist_reset(void)
{
    for (uint32_t i = 0; i < s.ram_size >> CHUNK_BITS; i++) memset(s.ram[i], 0, CHUNK);
    s.linea = 0;
    s.linea_scan_wait = s.sync_fail = 0;
    s.sync_sent = s.sync_off = false;
    s.sync_rest_x = s.sync_rest_y = -1;
    memset(s.palette, 0, sizeof s.palette);
    s.rom_at_zero = true;
    s.memcfg = 0;
    s.video_hi = s.video_mid = s.video_lo = 0;
    s.sync_mode = 0x02;
    s.resolution = 0;
    s.psg_port_a = 0xff;
    s.acia_control = 0;
    s.acia_rdr = 0;
    ikbd_clear();
    s.cmd_n = s.cmd_need = 0;
    ikbd_reset_modes();
    s.last_pointer_x = s.last_pointer_y = 0;
    s.pointer_frac_x = s.pointer_frac_y = 0;
    s.pointer_seen = s.seed_valid = false;
    s.last_b1 = s.last_b2 = false;
    s.video_count = 0;
    memset(s.blit_halftone, 0, sizeof s.blit_halftone);
    s.blit_sxinc = s.blit_syinc = s.blit_dxinc = s.blit_dyinc = 0;
    s.blit_src = s.blit_dst = 0;
    s.blit_emask[0] = s.blit_emask[1] = s.blit_emask[2] = 0;
    s.blit_xcount = s.blit_ycount = 0;
    s.blit_hop = s.blit_op = s.blit_ctrl = s.blit_skew = 0;
    s.blit_defer_start = false;
    memset(s.keys_sent, 0, sizeof s.keys_sent);
    s.mfp_acc = s.audio_acc = 0;
    s.naudio = 0;
    s.debt = 0;
    s.vbl = s.hbl = s.mfp_irq = false;
    mc68901_reset(&s.mfp);
    ay8910_reset(&s.psg);
    st_floppy_reset(&s.fdc);
    mc68901_set_gpip_bit(&s.mfp, 7, 1);         // colour monitor
    mc68901_set_gpip_bit(&s.mfp, 5, 1);
    mc68901_set_gpip_bit(&s.mfp, 4, 1);
    if (s.ram_size < 0x80000) {
        // Less than the 520ST's 512 KB (the boards' memory): no bank layout
        // TOS's memory test knows, so the system variables say the memory
        // is already sized, as after a warm reset (memvalid, memval2,
        // memval3; memctrl 128 KB + 128 KB; phystop)
        static const struct { uint32_t at, v; } kWarm[] = {
            { 0x420, 0x752019f3 }, { 0x43a, 0x237698aa }, { 0x51a, 0x5555aaaa },
        };
        for (size_t i = 0; i < sizeof kWarm / sizeof kWarm[0]; i++) {
            ram_wr16(kWarm[i].at, (uint16_t)(kWarm[i].v >> 16));
            ram_wr16(kWarm[i].at + 2, (uint16_t)kWarm[i].v);
        }
        ram_wr(0x424, 0x00);
        ram_wr16(0x42e, (uint16_t)(s.ram_size >> 16));
        ram_wr16(0x430, (uint16_t)s.ram_size);
    }
    m68k_pulse_reset();
    st_ipl_dirty = false;
    m68k_set_irq(0);
}

void atarist_key(int code, bool down)
{
    if (code > 0 && code < 128) s.keys[code] = down;
}

void atarist_keys_clear(void) { memset(s.keys, 0, sizeof s.keys); }

void atarist_joy(int port, unsigned bits)
{
    if (port == 0 || port == 1) s.joy_in[port] = bits & 0x8f;
}

void atarist_pointer_input(const atarist_pointer *p)
{
    // (relative motion adds up until the next frame takes it)
    const int dx = s.ptr.dx + (p->relative ? p->dx : 0), dy = s.ptr.dy + (p->relative ? p->dy : 0);
    const bool resync = s.ptr.resync || p->resync;
    s.ptr = *p;
    s.ptr.dx = dx;
    s.ptr.dy = dy;
    s.ptr.resync = resync;
}

// --- media ------------------------------------------------------------------------------------
const char *atarist_last_error(void) { return s.error ? s.error : ""; }
bool atarist_has_disk(int drive) { return st_floppy_loaded(&s.fdc, drive & 1); }
void atarist_eject(int drive) { st_floppy_eject(&s.fdc, drive & 1); }
bool atarist_drive_light(void) { return st_floppy_active(&s.fdc); }
void atarist_set_turbo(bool on) { s.turbo = on; }

bool atarist_load_pbuf(int drive, zx_pbuf *pb, const char *name)
{
    const char *why = "";
    s.error = NULL;
    if (!st_floppy_load(&s.fdc, drive & 1, pb, name, &why)) {
        s.error = why;
        return false;
    }
    return true;
}

bool atarist_load_file(int drive, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        s.error = "cannot open the disk image";
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        fclose(f);
        s.error = "not a disk image";
        return false;
    }
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        fclose(f);
        s.error = "cannot read the disk image";
        return false;
    }
    fclose(f);
    zx_pbuf pb;
    zx_pb_init(&pb);
    zx_pb_take_flat(&pb, data, (size_t)n);
    return atarist_load_pbuf(drive, &pb, path);
}

int atarist_char_key(char c, bool *shift)
{
    static const char kRow1[] = "1234567890-=";             // 0x02..0x0d
    static const char kRow1s[] = "!@#$%^&*()_+";
    static const char kQ[] = "qwertyuiop[]";                 // 0x10..0x1b
    static const char kQs[] = "QWERTYUIOP{}";
    static const char kA[] = "asdfghjkl;'`";                 // 0x1e..0x29
    static const char kAs[] = "ASDFGHJKL:\"~";
    static const char kZ[] = "zxcvbnm,./";                   // 0x2c..0x35
    static const char kZs[] = "ZXCVBNM<>?";
    *shift = false;
    const char *p;
    if (c == ' ') return STK_SPACE;
    if (c == '\r' || c == '\n') return STK_RETURN;
    if (c == '\t') return STK_TAB;
    if (c == 27) return STK_ESC;
    if (c == 8 || c == 127) return STK_BACKSPACE;
    if (c == '\\') return STK_BACKSLASH;
    if (c == '|') { *shift = true; return STK_BACKSLASH; }
    if (!c) return 0;
    if ((p = strchr(kRow1, c))) return 0x02 + (int)(p - kRow1);
    if ((p = strchr(kRow1s, c))) { *shift = true; return 0x02 + (int)(p - kRow1s); }
    if ((p = strchr(kQ, c))) return 0x10 + (int)(p - kQ);
    if ((p = strchr(kQs, c))) { *shift = true; return 0x10 + (int)(p - kQs); }
    if ((p = strchr(kA, c))) return 0x1e + (int)(p - kA);
    if ((p = strchr(kAs, c))) { *shift = true; return 0x1e + (int)(p - kAs); }
    if ((p = strchr(kZ, c))) return 0x2c + (int)(p - kZ);
    if ((p = strchr(kZs, c))) { *shift = true; return 0x2c + (int)(p - kZs); }
    return 0;
}
