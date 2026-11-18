// macii_core.c - the Macintosh II (see macii_core.h): C11 migration of
// dsp-cpp's drivers/computers/macii.cpp, on Musashi 4 (cpu/musashi).
#include "macii_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../../cpu/musashi/m68k.h"
#include "via6522.h"
#include "iwm.h"
#include "ncr5380.h"
#include "asc.h"

#define LINES 525
#define CYCLES_PER_FRAME 235008
#define CYCLES_PER_LINE (CYCLES_PER_FRAME / LINES)
#define KEYQ 64

static struct {
    uint8_t *ram, *vram;
    uint32_t ram_size;
    const uint8_t *rom, *decl_rom;
    bool ready;
    via6522 via1, via2;
    iwm iwm;
    ncr5380 scsi;
    asc asc;
    uint8_t scc_ptr[2], scc_wr2;
    bool overlay;
    uint8_t glue;
    bool hmmu_24bit;
    uint8_t nubus_irq;
    bool via1_irq, via2_irq;
    int via_rem;
    int64_t asc_acc;
    // video card (JMFB)
    uint16_t jm_control, jm_preload;
    uint32_t jm_base, jm_stride;
    uint16_t crtc[64];
    bool vbl_disable, slot_irq;
    uint8_t clut_addr, clut_cnt, clut_rgb[3], ramdac_mode;
    uint32_t clut[256];
    int line;
    // clock / PRAM
    bool rtc_enb, rtc_clk, rtc_latch, rtc_out;
    int rtc_state, rtc_bits;
    bool rtc_dir_out;
    uint8_t rtc_byte, rtc_cmd, rtc_xaddr;
    bool rtc_wp;
    uint32_t rtc_seconds;
    uint8_t pram[256];
    int64_t second_cycles;
    bool rtc_1hz;
    // ADB
    int adb_state;
    int64_t adb_due;
    bool adb_int, adb_listen, adb_talk_buf;
    uint8_t adb_index, adb_size, adb_cmd, adb_buf[8];
    uint8_t adb_kbd_addr, adb_mouse_addr, adb_rand;
    uint8_t keyq[KEYQ];
    int kq_head, kq_tail;
    bool held[ADBK_COUNT];
    int mouse_dx, mouse_dy;
    bool mouse_button, mouse_button_sent, adb_mouse_polled;
    int last_px, last_py;
    // CPU time
    bool in_execute;
    uint64_t total_cycles, last_tick_cycle;
    int scsi_busy;
    int16_t audio[MACII_AUDIO_MAX];
    int naudio;
    bool turbo;
} M;

static uint32_t mac_time_now(void)
{
    const time_t now = time(NULL);
    const struct tm *lt = localtime(&now);
    if (!lt) return 0;
    int64_t days = 0;
    for (int y = 1904; y < lt->tm_year + 1900; y++) days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    days += lt->tm_yday;
    return (uint32_t)(days * 86400 + lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
}

// PRAM as a Mac II leaves it after its first boot, so the ROM keeps it: the
// XPRAM signature 'NuMc', SPValid $A8, and slot 9's Display Card 8*24
// (board $0027) with its video mode ($80..$84: 1 / 2 / 4 / 8 / 24 bits)
static void seed_pram(uint8_t video_mode)
{
    static const uint8_t head[32] = {
        0x00, 0x00, 0x4f, 0x48, 0x00, 0x00, 0x00, 0x00, 0x03, 0x88, 0x00, 0xcc, 'N', 'u', 'M', 'c',
        0xa8, 0x00, 0x00, 0x00, 0xcc, 0x0a, 0xcc, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x63, 0x00,
    };
    static const uint8_t slot9[8] = {0x00, 0x27, 0x80, 0xa6, 0xa6, 0x00, 0xff, 0x00};
    static const uint8_t video[5] = {0x01, 0xff, 0xff, 0xff, 0xdf};
    memset(M.pram, 0, sizeof M.pram);
    memcpy(M.pram, head, sizeof head);
    memcpy(M.pram + 0x46, slot9, sizeof slot9);
    M.pram[0x48] = video_mode;
    memcpy(M.pram + 0x77, video, sizeof video);
}

// ---------------------------------------------------------------------------
static void update_irqs(void)
{
    const int level = M.via2_irq ? 2 : M.via1_irq ? 1 : 0;
    m68k_set_irq((unsigned)level);
}

static void via1_irq_cb(void *u, bool on) { (void)u; M.via1_irq = on; update_irqs(); }
static void via2_irq_cb(void *u, bool on) { (void)u; M.via2_irq = on; update_irqs(); }

static void set_slot_irq(bool on)
{
    M.slot_irq = on;
    if (on) M.nubus_irq &= (uint8_t)~0x01;
    else M.nubus_irq |= 0x01;
    via6522_write_ca1(&M.via2, (M.nubus_irq & 0x3f) == 0x3f);
}

static void asc_irq_cb(void *u, bool on) { (void)u; via6522_write_cb1(&M.via2, !on); }

static uint64_t now_cycles(void) { return M.total_cycles + (M.in_execute ? (uint64_t)m68k_cycles_run() : 0); }

static void adb_new_state(void);

// The chips up to the CPU's time
static void tick_devices(void)
{
    const uint64_t now = now_cycles();
    const int64_t delta = (int64_t)now - (int64_t)M.last_tick_cycle;
    if (delta <= 0) return;
    M.last_tick_cycle = now;
    M.via_rem += (int)delta;
    const int via_ticks = M.via_rem / 20;
    M.via_rem -= via_ticks * 20;
    if (via_ticks > 0) {
        via6522_tick(&M.via1, via_ticks);
        via6522_tick(&M.via2, via_ticks);
    }
    iwm_tick(&M.iwm, (int)delta);
    M.asc_acc += delta * MACII_AUDIO_RATE;
    while (M.asc_acc >= MACII_CLOCK) {
        M.asc_acc -= MACII_CLOCK;
        int32_t s = asc_generate(&M.asc) * 48;
        s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
        if (!M.turbo && M.naudio < MACII_AUDIO_MAX) M.audio[M.naudio++] = (int16_t)s;
    }
    M.second_cycles += delta;
    if (M.second_cycles >= MACII_CLOCK / 2) {
        M.second_cycles -= MACII_CLOCK / 2;
        M.rtc_1hz = !M.rtc_1hz;
        via6522_write_ca2(&M.via1, M.rtc_1hz);
        if (M.rtc_1hz) M.rtc_seconds++;
    }
    if (M.adb_due >= 0 && (int64_t)now >= M.adb_due) {
        M.adb_due = -1;
        adb_new_state();
    }
}

// ---------------------------------------------------------------------------
// The clock (343-0042-B, 256 bytes of extended PRAM)
static void rtc_execute(uint8_t data)
{
    enum { NORMAL = 0, WRITE = 1, XP_COMMAND = 2, XP_WRITE = 3 };
    if (M.rtc_state == XP_COMMAND) {
        M.rtc_xaddr = (uint8_t)(((M.rtc_cmd & 7) << 5) | ((data & 0x7c) >> 2));
        if (M.rtc_cmd & 0x80) {
            M.rtc_dir_out = true;
            M.rtc_byte = M.pram[M.rtc_xaddr];
            M.rtc_bits = 8;
            M.rtc_state = NORMAL;
        } else {
            M.rtc_state = XP_WRITE;
            M.rtc_byte = 0;
            M.rtc_bits = 0;
        }
        return;
    }
    if (M.rtc_state == XP_WRITE) {
        if (!M.rtc_wp) M.pram[M.rtc_xaddr] = data;
        M.rtc_state = NORMAL;
        M.rtc_bits = 0;
        return;
    }
    if (M.rtc_state == WRITE) {
        M.rtc_state = NORMAL;
        M.rtc_bits = 0;
        const int i = (M.rtc_cmd >> 2) & 0x1f;
        if (M.rtc_wp && i != 13) return;
        if (i < 8) {
            const int sh = 8 * (i & 3);
            M.rtc_seconds = (M.rtc_seconds & ~(0xffu << sh)) | ((uint32_t)data << sh);
        } else if (i < 12 || i >= 16) {
            M.pram[i] = data;
        } else if (i == 13) {
            M.rtc_wp = (data & 0x80) != 0;
        }
        return;
    }
    M.rtc_cmd = data;
    if ((M.rtc_cmd & 0x78) == 0x38) {
        M.rtc_state = XP_COMMAND;
        M.rtc_byte = 0;
        M.rtc_bits = 0;
        return;
    }
    if (M.rtc_cmd & 0x80) {
        const int i = (M.rtc_cmd >> 2) & 0x1f;
        M.rtc_dir_out = true;
        if (i < 8) M.rtc_byte = (uint8_t)(M.rtc_seconds >> (8 * (i & 3)));
        else if (i < 12 || i >= 16) M.rtc_byte = M.pram[i];
        else M.rtc_byte = 0;
        M.rtc_bits = 8;
    } else {
        M.rtc_state = WRITE;
        M.rtc_byte = 0;
        M.rtc_bits = 0;
    }
}

static void rtc_ce(bool level)
{
    if (level != M.rtc_enb) {
        M.rtc_byte = 0;
        M.rtc_bits = 0;
        M.rtc_dir_out = false;
        M.rtc_out = false;
        M.rtc_state = 0;
    }
    M.rtc_enb = level;
}

static void rtc_clk(bool level)
{
    const bool falling = !level && M.rtc_clk;
    M.rtc_clk = level;
    if (!falling || M.rtc_enb) return;
    if (M.rtc_dir_out) {
        if (M.rtc_bits > 0) M.rtc_bits--;
        M.rtc_out = ((M.rtc_byte >> M.rtc_bits) & 1) != 0;
        return;
    }
    M.rtc_byte = (uint8_t)((M.rtc_byte << 1) | (M.rtc_latch ? 1 : 0));
    if (++M.rtc_bits == 8) rtc_execute(M.rtc_byte);
}

// ---------------------------------------------------------------------------
// ADB transceiver with keyboard and mouse (protocol level, as Mini vMac)
static void adb_shift_in(uint8_t v)
{
    if (((M.via1.acr >> 2) & 7) == 3) via6522_shift_in_external(&M.via1, v);
}

static uint8_t adb_shift_out(void)
{
    if (((M.via1.acr >> 2) & 7) != 7) return 0;
    return via6522_shift_out_external(&M.via1);
}

static void adb_state_changed(void) { M.adb_due = (int64_t)now_cycles() + 10000; }  // (a few hundred us later)

static bool adb_next_key(uint8_t *ev)
{
    if (M.kq_head == M.kq_tail) return false;
    *ev = M.keyq[M.kq_head % KEYQ];
    M.kq_head++;
    return true;
}

static bool adb_any_event(void)
{
    return M.kq_head != M.kq_tail || M.mouse_dx != 0 || M.mouse_dy != 0 || M.mouse_button != M.mouse_button_sent;
}

static void adb_talk(void)
{
    const uint8_t addr = M.adb_cmd >> 4;
    const int reg = M.adb_cmd & 3;
    if (addr == M.adb_mouse_addr) {
        if (reg == 0) {
            M.adb_mouse_polled = true;
            const int dx = M.mouse_dx < -63 ? -63 : M.mouse_dx > 63 ? 63 : M.mouse_dx;
            const int dy = M.mouse_dy < -63 ? -63 : M.mouse_dy > 63 ? 63 : M.mouse_dy;
            M.mouse_dx -= dx;
            M.mouse_dy -= dy;
            const bool change = M.mouse_button != M.mouse_button_sent;
            if (dx || dy || change) {
                M.mouse_button_sent = M.mouse_button;
                M.adb_size = 2;
                M.adb_talk_buf = true;
                M.adb_buf[0] = (uint8_t)((M.mouse_button ? 0x00 : 0x80) | (dy & 0x7f));
                M.adb_buf[1] = (uint8_t)(0x80 | (dx & 0x7f));
            }
        } else if (reg == 3) {
            M.adb_size = 2;
            M.adb_talk_buf = true;
            M.adb_buf[0] = (uint8_t)(0x60 | (M.adb_rand++ & 0x0f));
            M.adb_buf[1] = 0x01;
        }
    } else if (addr == M.adb_kbd_addr) {
        if (reg == 0) {
            uint8_t ev;
            if (adb_next_key(&ev)) {
                M.adb_size = 2;
                M.adb_talk_buf = true;
                M.adb_buf[0] = ev;
                M.adb_buf[1] = adb_next_key(&ev) ? ev : 0xff;
            }
        } else if (reg == 3) {
            M.adb_size = 2;
            M.adb_talk_buf = true;
            M.adb_buf[0] = (uint8_t)(0x60 | (M.adb_rand++ & 0x0f));
            M.adb_buf[1] = 0x01;
        } else if (reg == 2) {
            M.adb_size = 2;
            M.adb_talk_buf = true;
            M.adb_buf[0] = M.adb_buf[1] = 0xff;
        }
    }
}

static void adb_end_listen(void)
{
    const uint8_t addr = M.adb_cmd >> 4;
    if ((M.adb_cmd & 3) == 3 && M.adb_size >= 2 && M.adb_buf[1] == 0xfe) {
        if (addr == M.adb_mouse_addr) M.adb_mouse_addr = M.adb_buf[0] & 0x0f;
        else if (addr == M.adb_kbd_addr) M.adb_kbd_addr = M.adb_buf[0] & 0x0f;
    }
}

static void adb_new_state(void)
{
    M.adb_int = true;
    switch (M.adb_state) {
    case 0:                             // a new command
        if (M.adb_listen) {
            M.adb_listen = false;
            M.adb_size = M.adb_index;
            adb_end_listen();
        }
        M.adb_talk_buf = false;
        M.adb_index = 0;
        M.adb_cmd = adb_shift_out();
        switch ((M.adb_cmd >> 2) & 3) {
        case 0:
            if ((M.adb_cmd & 3) == 0) {          // SendReset
                M.adb_kbd_addr = 2;
                M.adb_mouse_addr = 3;
            } else if ((M.adb_cmd & 3) == 1) {   // Flush
                M.adb_size = 2;
                M.adb_talk_buf = true;
                M.adb_buf[0] = M.adb_buf[1] = 0;
            }
            break;
        case 2: M.adb_listen = true; break;
        case 3: adb_talk(); break;
        default: break;
        }
        break;
    case 1:
    case 2:
        if (!M.adb_listen) {
            if (!M.adb_talk_buf || M.adb_index >= M.adb_size) {
                adb_shift_in(0xff);
                M.adb_int = false;
            } else {
                adb_shift_in(M.adb_buf[M.adb_index++]);
            }
        } else {
            const uint8_t b = adb_shift_out();
            if (M.adb_index < sizeof M.adb_buf) M.adb_buf[M.adb_index++] = b;
        }
        break;
    case 3:                             // idle: auto-poll the last device
        if (M.adb_talk_buf) {
            adb_shift_in(0xff);
        } else if (adb_any_event()) {
            if (((M.adb_cmd >> 2) & 3) == 3) adb_talk();
            adb_shift_in(0xff);
        }
        break;
    }
}

static void adb_update(void)
{
    if (M.adb_state != 3 || M.adb_due >= 0 || M.adb_talk_buf) return;
    if (adb_any_event()) {
        if (((M.adb_cmd >> 2) & 3) == 3) adb_talk();
        adb_shift_in(0xff);
    }
}

// ---------------------------------------------------------------------------
// VIA ports
static uint8_t via1_pa_in(void *u) { (void)u; return 0x81; }

static void via1_pa_out(void *u, uint8_t x)
{
    (void)u, (void)x;
    const uint8_t v = (uint8_t)((M.via1.out_a & M.via1.ddr_a) | (~M.via1.ddr_a & 0xff));
    M.overlay = (v & 0x10) != 0;
    iwm_set_hdsel(&M.iwm, (v & 0x20) != 0);
}

static uint8_t via1_pb_in(void *u)
{
    (void)u;
    uint8_t v = 0xf6;
    if (M.adb_int) v |= 0x08;
    if (M.rtc_out) v |= 0x01;
    return v;
}

static void via1_pb_out(void *u, uint8_t x)
{
    (void)u, (void)x;
    const uint8_t v = (uint8_t)((M.via1.out_b & M.via1.ddr_b) | (~M.via1.ddr_b & 0xff));
    rtc_ce((v & 0x04) != 0);
    M.rtc_latch = (v & 0x01) != 0;
    rtc_clk((v & 0x02) != 0);
    const int st = (v >> 4) & 3;
    if (st != M.adb_state) {
        M.adb_state = st;
        adb_state_changed();
    }
}

static uint8_t via2_pa_in(void *u) { (void)u; return (uint8_t)(0xc0 | M.nubus_irq); }

static void via2_pa_out(void *u, uint8_t x)
{
    (void)u, (void)x;
    const uint8_t v = (uint8_t)((M.via2.out_a & M.via2.ddr_a) | (~M.via2.ddr_a & 0xff));
    M.glue = (uint8_t)((v >> 6) & 3);
}

static uint8_t via2_pb_in(void *u) { (void)u; return 0xcf; }

static void via2_pb_out(void *u, uint8_t x)
{
    (void)u, (void)x;
    const uint8_t v = via6522_out_b(&M.via2);
    M.hmmu_24bit = (v & 0x08) == 0;
    via6522_write_ca1(&M.via1, (v & 0x80) != 0);    // the 60.15 Hz tick: VIA2 T1 on PB7
}

// ---------------------------------------------------------------------------
// Address decoding
static uint32_t translate(uint32_t a)
{
    if (!M.hmmu_24bit) return a;
    a &= 0xffffff;
    if (a >= 0x800000 && a <= 0x8fffff) return a | 0x40000000u;
    if (a >= 0x900000 && a <= 0xefffff) return 0xf0000000u | ((a & 0xf00000) << 4) | (a & 0xfffff);
    if (a >= 0xf00000) return a | 0x50000000u;
    return a;
}

static uint8_t *ram_ptr(uint32_t pa)
{
    if (pa >= 0x40000000u || M.overlay) return NULL;
    if (M.glue == 3) return pa < 0x100000 ? &M.ram[pa] : NULL;
    if (pa < M.ram_size) return &M.ram[pa];
    if (M.ram_size < (8u << 20)) {      // (4 MB: bank A only, mirrored where the 8 MB one's is)
        if (pa >= (8u << 20) && pa < (12u << 20)) return &M.ram[pa - (8u << 20)];
        return NULL;
    }
    if (pa < M.ram_size + M.ram_size / 2) return &M.ram[pa - M.ram_size];    // bank A's mirror
    return NULL;
}

static void bus_error(uint32_t address, bool write)
{
    if (M.in_execute) m68k_pulse_bus_error_at(address, write ? 1 : 0);
}

static uint8_t io_read8(uint32_t off)
{
    tick_devices();
    if (off >= 0x40000 && off < 0x42000) return via6522_read(&M.via1, (uint8_t)((off >> 9) & 0xf));
    if (off < 0x2000) return via6522_read(&M.via1, (uint8_t)((off >> 9) & 0xf));
    if (off < 0x4000) return via6522_read(&M.via2, (uint8_t)((off >> 9) & 0xf));
    if (off < 0x6000) {                 // SCC 85C30: A1 the channel (1 = A), A2 data / control
        const int which = (int)((off >> 1) & 3), ch = (which & 1) ? 0 : 1;
        if (which & 2) return 0;
        const uint8_t reg = M.scc_ptr[ch];
        M.scc_ptr[ch] = 0;
        switch (reg) {
        case 0: return 0x54;            // Tx underrun / EOM, sync / hunt, Tx empty
        case 1: return 0x01;
        case 2: return ch == 1 ? 0x06 : M.scc_wr2;
        default: return 0;
        }
    }
    if (off >= 0x10000 && off < 0x12000) {
        const uint32_t sub = off - 0x10000;
        M.scsi_busy = 8;
        return ncr5380_read(&M.scsi, (((sub >> 4) & 7) << 4) | (sub == 0x260 ? 0x200u : 0u));
    }
    if (off >= 0x14000 && off < 0x16000) return asc_read(&M.asc, off - 0x14000);
    if (off >= 0x16000 && off < 0x18000) return iwm_read(&M.iwm, (uint8_t)((off >> 9) & 0xf));
    return 0;
}

static void io_write8(uint32_t off, uint8_t v)
{
    tick_devices();
    if ((off >= 0x40000 && off < 0x42000) || off < 0x2000) {
        via6522_write(&M.via1, (uint8_t)((off >> 9) & 0xf), v);
        return;
    }
    if (off < 0x4000) {
        via6522_write(&M.via2, (uint8_t)((off >> 9) & 0xf), v);
        return;
    }
    if (off < 0x6000) {
        const int which = (int)((off >> 1) & 3), ch = (which & 1) ? 0 : 1;
        if (which & 2) return;
        if (M.scc_ptr[ch] == 0) {
            const uint8_t cmd = (uint8_t)((v >> 3) & 7);
            M.scc_ptr[ch] = (uint8_t)((v & 7) | (cmd == 1 ? 8 : 0));
        } else {
            if (M.scc_ptr[ch] == 2) M.scc_wr2 = v;
            M.scc_ptr[ch] = 0;
        }
        return;
    }
    if (off >= 0x10000 && off < 0x12000) {
        const uint32_t sub = off - 0x10000;
        M.scsi_busy = 8;
        ncr5380_write(&M.scsi, (((sub >> 4) & 7) << 4) | (sub == 0x200 ? 0x200u : 0u), v);
        return;
    }
    if (off >= 0x14000 && off < 0x16000) {
        asc_write(&M.asc, off - 0x14000, v);
        return;
    }
    if (off >= 0x16000 && off < 0x18000) iwm_write(&M.iwm, (uint8_t)((off >> 9) & 0xf), v);
}

// --- the Display Card 8*24 (JMFB) in slot 9 ---
static uint32_t card_reg_read(uint32_t reg)
{
    if (reg < 0x200010) {
        switch (reg & 0xc) {
        case 0x0: {
            // monitor sense: the Hi-Res 640x480 (MAME's monitor type 6)
            static const unsigned sense_tab[4] = {6, 2, 4, 6};
            unsigned sense = sense_tab[0];
            if (M.jm_control & 0x800) sense &= sense_tab[1];
            if (M.jm_control & 0x400) sense &= sense_tab[2];
            if (M.jm_control & 0x200) sense &= sense_tab[3];
            return (uint32_t)((M.jm_control & 0xf1ff) | (sense << 9));
        }
        case 0x4: return M.jm_preload;
        case 0x8: return M.jm_base;
        default: return M.jm_stride;
        }
    }
    if (reg >= 0x200100 && reg < 0x200200) {
        const uint32_t r = (reg - 0x200100) >> 2;
        if (r == 0xc0 / 4) {
            // the beam (active low): 1 active, 2 back porch, 4 sync, 8 front porch
            const int vactive = M.crtc[0x24 / 4] ? M.crtc[0x24 / 4] : 960;
            const int vbp = M.crtc[0x28 / 4], vs = M.crtc[0x2c / 4], vfp = M.crtc[0x30 / 4];
            int total = vactive + vbp + vs + vfp;
            if (total < 1) total = 1;
            const int64_t in_frame = (int64_t)M.line * CYCLES_PER_LINE;
            const int halfline = (int)((in_frame * total) / CYCLES_PER_FRAME);
            int pos = halfline + vs + vbp;         // (our frame starts with the active video)
            if (pos >= total) pos -= total;
            uint8_t result = 0x0f;
            if (pos < vs) result &= (uint8_t)~0x04;
            else if (pos < vs + vbp) result &= (uint8_t)~0x02;
            else if (pos < vs + vbp + vactive) result &= (uint8_t)~0x01;
            else result &= (uint8_t)~0x08;
            return result;
        }
        if (r == 0xcc / 4) return 0;
        if (r < 64) return M.crtc[r];
        return 0;
    }
    return 0;
}

static void card_reg_write(uint32_t reg, uint32_t data)
{
    if (reg < 0x200010) {
        data &= 0xffff;
        switch (reg & 0xc) {
        case 0x0: M.jm_control = (uint16_t)(data & 0x7fff); break;
        case 0x4: M.jm_preload = (uint16_t)(data & 0xff); break;
        case 0x8: M.jm_base = data; break;
        default: M.jm_stride = data; break;
        }
        return;
    }
    if (reg >= 0x200100 && reg < 0x200200) {
        data &= 0xffff;
        const uint32_t r = (reg - 0x200100) >> 2;
        if (r == 0x3c / 4) M.vbl_disable = (data & 2) != 0;
        if (r == 0x48 / 4) set_slot_irq(false);
        if (r < 64) M.crtc[r] = (uint16_t)(data & 0x0fff);
        return;
    }
    if (reg >= 0x200200 && reg < 0x200210) {
        data &= 0xff;
        switch (reg & 0xc) {
        case 0x0:
            M.clut_addr = (uint8_t)data;
            M.clut_cnt = 0;
            break;
        case 0x4:
            M.clut_rgb[M.clut_cnt++] = (uint8_t)data;
            if (M.clut_cnt == 3) {
                M.clut[M.clut_addr] = ((uint32_t)M.clut_rgb[0] << 16) | ((uint32_t)M.clut_rgb[1] << 8) | M.clut_rgb[2];
                M.clut_addr++;
                M.clut_cnt = 0;
            }
            break;
        case 0x8: M.ramdac_mode = (uint8_t)((data >> 1) & 0xf); break;
        default: break;
        }
    }
}

static uint32_t card_read(uint32_t off, int size)
{
    uint32_t v = 0;
    if (off < 0x100000) {
        for (int i = 0; i < size; i++) v = (v << 8) | M.vram[(off + (uint32_t)i) & 0xfffff];
        return v;
    }
    if (off >= 0xfe0000) {
        for (int i = 0; i < size; i++) {
            const uint32_t o = off + (uint32_t)i;
            v = (v << 8) | ((o & 3) == 3 ? M.decl_rom[((o - 0xfe0000) >> 2) & 0x7fff] : 0);
        }
        return v;
    }
    if (off >= 0x200000 && off < 0x200400) {
        // 32-bit registers: whatever bytes were asked for
        uint32_t cached = 0xffffffffu, r = 0;
        for (int i = 0; i < size; i++) {
            const uint32_t o = off + (uint32_t)i;
            if ((o & ~3u) != cached) {
                cached = o & ~3u;
                r = card_reg_read(cached);
            }
            v = (v << 8) | ((r >> (8 * (3 - (o & 3)))) & 0xff);
        }
        return v;
    }
    return 0;
}

static void card_write(uint32_t off, uint32_t value, int size)
{
    if (off < 0x100000) {
        for (int i = 0; i < size; i++) M.vram[(off + (uint32_t)i) & 0xfffff] = (uint8_t)(value >> (8 * (size - 1 - i)));
        return;
    }
    if (off >= 0x200000 && off < 0x200400) {
        uint32_t data = value;
        if (size == 1) data = (value & 0xff) * 0x01010101u;         // the lanes are smeared
        else if (size == 2) data = (value & 0xffff) * 0x00010001u;
        card_reg_write(off & ~3u, data);
    }
}

static uint32_t phys_read(uint32_t pa, int size)
{
    if (pa < 0x40000000u) {
        uint32_t v = 0;
        if (M.overlay) {
            for (int i = 0; i < size; i++) v = (v << 8) | M.rom[(pa + (uint32_t)i) & 0x3ffff];
            return v;
        }
        if (pa + (uint32_t)size <= M.ram_size && M.glue != 3) {
            for (int i = 0; i < size; i++) v = (v << 8) | M.ram[pa + (uint32_t)i];
            return v;
        }
        for (int i = 0; i < size; i++) {
            const uint8_t *r = ram_ptr(pa + (uint32_t)i);
            v = (v << 8) | (r ? *r : 0);
        }
        return v;
    }
    if (pa < 0x50000000u) {
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v = (v << 8) | M.rom[(pa + (uint32_t)i) & 0x3ffff];
        return v;
    }
    if (pa < 0x60000000u) {
        if ((pa & 0xff000000u) != 0x50000000u) return 0;
        const uint32_t off = pa & 0xfffff;
        // SCSI pseudo-DMA windows: every byte lane is a DMA byte
        if ((off >= 0x06000 && off < 0x06004) || (off >= 0x06060 && off < 0x06064) || (off >= 0x12000 && off < 0x14000)) {
            uint32_t v = 0;
            M.scsi_busy = 8;
            for (int i = 0; i < size; i++) v = (v << 8) | ncr5380_read(&M.scsi, 0x260);
            return v;
        }
        // the byte-wide chips repeat on both lanes of a word
        uint32_t v = 0;
        for (int i = 0; i < size; i += 2) {
            const uint8_t b = io_read8(off + (uint32_t)i);
            if (size == 1) return b;
            v = (v << 16) | ((uint32_t)b << 8) | b;
        }
        return v;
    }
    if (pa >= 0xf9000000u && pa < 0xfa000000u) return card_read(pa & 0xffffff, size);
    if (pa >= 0x90000000u) {
        bus_error(pa, false);
        return 0xffffffffu >> (32 - 8 * size);
    }
    return 0;
}

static void phys_write(uint32_t pa, uint32_t value, int size)
{
    if (pa < 0x40000000u) {
        if (M.overlay) return;
        for (int i = 0; i < size; i++) {
            uint8_t *r = ram_ptr(pa + (uint32_t)i);
            if (r) *r = (uint8_t)(value >> (8 * (size - 1 - i)));
        }
        return;
    }
    if (pa < 0x50000000u) return;       // ROM
    if (pa < 0x60000000u) {
        if ((pa & 0xff000000u) != 0x50000000u) return;
        const uint32_t off = pa & 0xfffff;
        if ((off >= 0x06000 && off < 0x06004) || (off >= 0x06060 && off < 0x06064) || (off >= 0x12000 && off < 0x14000)) {
            M.scsi_busy = 8;
            for (int i = 0; i < size; i++) ncr5380_write(&M.scsi, 0x200, (uint8_t)(value >> (8 * (size - 1 - i))));
            return;
        }
        if (size == 1) {
            io_write8(off, (uint8_t)value);
            return;
        }
        // a word / long: the chip sees each word's high byte
        for (int i = 0; i < size; i += 2) io_write8(off + (uint32_t)i, (uint8_t)(value >> (8 * (size - 1 - i))));
        return;
    }
    if (pa >= 0xf9000000u && pa < 0xfa000000u) {
        card_write(pa & 0xffffff, value, size);
        return;
    }
    if (pa >= 0x90000000u) bus_error(pa, true);
}

static uint32_t cpu_peek(uint32_t a, int size)
{
    const uint32_t pa = translate(a);
    uint32_t v = 0;
    for (int i = 0; i < size; i++) {
        const uint32_t p = pa + (uint32_t)i;
        uint8_t b = 0;
        const uint8_t *r = ram_ptr(p);
        if (r) b = *r;
        else if ((p < 0x40000000u && M.overlay) || (p >= 0x40000000u && p < 0x50000000u)) b = M.rom[p & 0x3ffff];
        v = (v << 8) | b;
    }
    return v;
}

unsigned int m68k_read_memory_8(unsigned int a) { return phys_read(translate(a), 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return phys_read(translate(a), 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return phys_read(translate(a), 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { phys_write(translate(a), v, 1); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { phys_write(translate(a), v, 2); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { phys_write(translate(a), v, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return cpu_peek(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return cpu_peek(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return cpu_peek(a, 4); }
void musashi_reset_instruction(void) {}
void musashi_instruction_hook(unsigned int pc)
{
#ifdef MUSASHI_TRACE
    char buf[128];
    m68k_disassemble(buf, pc, M68K_CPU_TYPE_68020);
    fprintf(stderr, "%08x  %s\n", pc, buf);
#else
    (void)pc;
#endif
}

// ---------------------------------------------------------------------------
bool macii_init(const uint8_t *rom, const uint8_t *card_rom, int ram_mb)
{
    if (!rom || !card_rom) return false;
    const uint32_t ram_size = (uint32_t)(ram_mb == 4 ? 4 : 8) << 20;
    if (M.ram && M.ram_size != ram_size) {
        free(M.ram);
        M.ram = NULL;
    }
    if (!M.ram) M.ram = (uint8_t *)malloc(ram_size);
    if (!M.vram) M.vram = (uint8_t *)malloc(0x100000);
    if (!M.ram || !M.vram) return false;
    M.ram_size = ram_size;
    M.rom = rom;
    M.decl_rom = card_rom;
    M.via1.pa_in = via1_pa_in;
    M.via1.pa_out = via1_pa_out;
    M.via1.pb_in = via1_pb_in;
    M.via1.pb_out = via1_pb_out;
    M.via1.irq = via1_irq_cb;
    M.via2.pa_in = via2_pa_in;
    M.via2.pa_out = via2_pa_out;
    M.via2.pb_in = via2_pb_in;
    M.via2.pb_out = via2_pb_out;
    M.via2.irq = via2_irq_cb;
    M.asc.irq = asc_irq_cb;
    if (!M.ready) {
        iwm_init(&M.iwm);
        ncr5380_init(&M.scsi, false, false);
        seed_pram(0x83);
        M.ready = true;
    }
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68020);
    macii_reset();
    return true;
}

void macii_reset(void)
{
    memset(M.ram, 0, M.ram_size);
    M.overlay = true;
    M.glue = 0;
    M.hmmu_24bit = false;
    M.nubus_irq = 0x3f;
    M.via1_irq = M.via2_irq = false;
    via6522_reset(&M.via1);
    via6522_reset(&M.via2);
    via6522_write_ca1(&M.via2, true);
    via6522_write_cb1(&M.via2, true);
    via6522_write_cb2(&M.via2, true);
    iwm_reset(&M.iwm);
    ncr5380_reset(&M.scsi);
    asc_reset(&M.asc);
    M.scc_ptr[0] = M.scc_ptr[1] = 0;
    memset(M.vram, 0, 0x100000);
    M.jm_control = 2;
    M.jm_preload = 248;
    M.jm_base = 0;
    M.jm_stride = 20;
    memset(M.crtc, 0, sizeof M.crtc);
    M.vbl_disable = true;
    M.slot_irq = false;
    M.clut_addr = M.clut_cnt = 0;
    M.ramdac_mode = 0;
    memset(M.clut, 0, sizeof M.clut);
    // the clock keeps its PRAM (a battery); the time is the host's
    M.rtc_seconds = mac_time_now();
    M.rtc_enb = true;
    M.rtc_state = M.rtc_bits = 0;
    M.rtc_dir_out = false;
    M.adb_state = 3;
    M.adb_due = -1;
    M.adb_int = true;
    M.adb_listen = M.adb_talk_buf = false;
    M.adb_kbd_addr = 2;
    M.adb_mouse_addr = 3;
    M.kq_head = M.kq_tail = 0;
    memset(M.held, 0, sizeof M.held);
    M.mouse_dx = M.mouse_dy = 0;
    M.adb_mouse_polled = false;
    M.last_px = M.last_py = -1;
    M.in_execute = false;
    M.total_cycles = M.last_tick_cycle = 0;
    M.via_rem = 0;
    M.asc_acc = 0;
    M.second_cycles = 0;
    m68k_set_irq(0);
    m68k_pulse_reset();
}

void macii_set_turbo(bool on) { M.turbo = on; }

int macii_run_frame(const int16_t **audio)
{
    M.naudio = 0;
    for (int line = 0; line < LINES; line++) {
        M.line = line;
        if (line == MACII_H && !M.vbl_disable) set_slot_irq(true);
        M.in_execute = true;
        const int done = m68k_execute(CYCLES_PER_LINE);
        M.in_execute = false;
        M.total_cycles += (uint64_t)done;
        tick_devices();
    }
    adb_update();
    if (M.scsi_busy) M.scsi_busy--;
    if (audio) *audio = M.audio;
    return M.naudio;
}

// ---------------------------------------------------------------------------
void macii_key(int code, bool down)
{
    if (code < 0 || code >= ADBK_COUNT || M.held[code] == down) return;
    M.held[code] = down;
    if (M.kq_tail - M.kq_head >= KEYQ) return;
    M.keyq[M.kq_tail % KEYQ] = (uint8_t)(code | (down ? 0 : 0x80));
    M.kq_tail++;
}

void macii_keys_clear(void)
{
    for (int c = 0; c < ADBK_COUNT; c++)
        if (M.held[c]) macii_key(c, false);
}

// The host's pointer is absolute: once the system follows the ADB mouse its
// cursor globals are set (MTemp, RawMouse, Mouse, CrsrNew), as Mini vMac
// does; before that (the ROM), the motion goes as ADB moves
void macii_pointer(int x, int y, bool button)
{
    x = x < 0 ? 0 : x > MACII_W - 1 ? MACII_W - 1 : x;
    y = y < 0 ? 0 : y > MACII_H - 1 ? MACII_H - 1 : y;
    M.mouse_button = button;
    if (M.adb_mouse_polled && !M.overlay) {
        const uint8_t pos[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(x >> 8), (uint8_t)x};
        if (memcmp(&M.ram[0x828], pos, 4) != 0 || memcmp(&M.ram[0x82c], pos, 4) != 0) {
            memcpy(&M.ram[0x828], pos, 4);
            memcpy(&M.ram[0x82c], pos, 4);
            memcpy(&M.ram[0x830], pos, 4);
            M.ram[0x8ce] = 0xff;
        }
        M.mouse_dx = M.mouse_dy = 0;
    } else if (M.last_px >= 0) {
        M.mouse_dx += x - M.last_px;
        M.mouse_dy += y - M.last_py;
    }
    M.last_px = x;
    M.last_py = y;
}

void macii_mouse(int dx, int dy, bool button)
{
    const int x = (M.last_px < 0 ? MACII_W / 2 : M.last_px) + dx, y = (M.last_py < 0 ? MACII_H / 2 : M.last_py) + dy;
    macii_pointer(x, y, button);
}

// ---------------------------------------------------------------------------
void macii_render_row(int y, uint32_t *out)
{
    if (!(M.jm_control & 0x40)) {       // the display's transfer off
        memset(out, 0, MACII_W * sizeof *out);
        return;
    }
    const bool direct = M.ramdac_mode == 0xd;
    const uint32_t base = M.jm_base << (direct ? 6 : 5), stride = M.jm_stride << (direct ? 3 : 2);
    const uint32_t row = base + (uint32_t)y * stride;
    const uint8_t *v = M.vram;
    for (int x = 0; x < MACII_W; x++) {
        uint32_t c;
        switch (M.ramdac_mode) {
        case 0x0: c = M.clut[(v[(row + (uint32_t)(x >> 3)) & 0xfffff] >> (7 - (x & 7))) & 1]; break;
        case 0x4: c = M.clut[(v[(row + (uint32_t)(x >> 2)) & 0xfffff] >> (6 - 2 * (x & 3))) & 3]; break;
        case 0x8: {
            const uint8_t b = v[(row + (uint32_t)(x >> 1)) & 0xfffff];
            c = M.clut[(x & 1) ? (b & 0xf) : (b >> 4)];
            break;
        }
        case 0xc: c = M.clut[v[(row + (uint32_t)x) & 0xfffff]]; break;
        case 0xd: {
            const uint32_t p = row + (uint32_t)x * 3;
            c = ((uint32_t)v[p & 0xfffff] << 16) | ((uint32_t)v[(p + 1) & 0xfffff] << 8) | v[(p + 2) & 0xfffff];
            break;
        }
        default: c = 0; break;
        }
        out[x] = c;
    }
}

void macii_render_row565(int y, uint16_t *out)
{
    uint32_t line[MACII_W];
    macii_render_row(y, line);
    for (int x = 0; x < MACII_W; x++) {
        const uint32_t c = line[x];
        out[x] = (uint16_t)((((c >> 19) & 31) << 11) | (((c >> 10) & 63) << 5) | ((c >> 3) & 31));
    }
}

const char *macii_attach_scsi(blkfile *f) { return ncr5380_attach(&M.scsi, f); }

const char *macii_load_file(const char *path)
{
    blkfile f;
    if (!blkfile_open(&f, path)) return "cannot open the disk image";
    return macii_attach_scsi(&f);
}

void macii_detach_scsi(void) { ncr5380_detach(&M.scsi); }
bool macii_scsi_loaded(void) { return M.scsi.loaded; }
bool macii_scsi_busy(void) { return M.scsi_busy > 0; }
