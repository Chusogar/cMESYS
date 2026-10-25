// ql_core.c - Sinclair QL. See ql_core.h.
//
// Memory map (68008, 20 address lines):
//   $00000-$0BFFF  system ROM (JS)        $0C000-$0FFFF  ROM port (empty)
//   $18000-$18003  ZX8302 RTC / TCR / IPC link
//   $18020-$18023  ZX8302 status, interrupts, microdrive data
//   $18063         ZX8301 control         $20000-$3FFFF  128 KB RAM (screen at $20000/$28000)
#include "ql_core.h"

#include <string.h>

#include "m68k.h"
#include "mcs48.h"
#include "zx8302.h"
#include "ql_mdv.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define QL_HOT IRAM_ATTR
#else
#define QL_HOT
#endif

#define SLICE           128             // 68008 cycles per lockstep slice (17 us)
#define IPC_MCLOCK      (QL_IPC_CLOCK / 15)
#define BAUDX4_HZ       38400           // 9600 baud x 4 until programmed

typedef struct {
    const uint8_t *rom, *ipc_rom;
    uint8_t *ram[QL_RAM_BLOCKS];
    mcs48 ipc;
    zx8302 zx;
    ql_mdv mdv[2];
    uint8_t keys[8];
    uint8_t auto_keys[8];               // keys pressed by ql_autoboot()
    int auto_frame;                     // frames since the reset (-1: off)
    uint8_t keylatch, video_ctrl;
    int comdata_to_ipc, ipc_ipl, zx_irq2, speaker;
    uint32_t ipc_acc, baud_acc, mdv_acc, audio_acc;
    int mdv_stall;
    int debt;
    bool irq_dirty;
    int rtc_frames, flash_frames;
    bool flash;
    uint32_t dirty[2][8];               // written rows of the two screens
    bool all_dirty;
    // audio: speaker high time over the sample
    uint32_t spk_on, spk_total;
    int16_t audio[QL_AUDIO_MAX + 8];
    int naudio;
} ql_t;

static ql_t q;
volatile uint32_t ql_diag_slices;          // 68008 slices run (diagnostics)

// ---------------------------------------------------------------------------
// Interrupts: IPL from the IPC (P2 bits 2-3) and the ZX8302 (IPL1L, level 2)
// ---------------------------------------------------------------------------
static void apply_irqs(void)
{
    int level = 0;
    if (q.ipc_ipl == 0) level = 7;
    else if (q.ipc_ipl == 1) level = 5;
    else if (q.zx_irq2 || q.ipc_ipl == 2) level = 2;
    m68k_set_irq(level);
}

int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
void cpu_set_fc(int fc) { (void)fc; }
void cpu_pulse_reset(void) {}
void cpu_instr_callback(unsigned int pc) { (void)pc; }

// ---------------------------------------------------------------------------
// IPC (8749): keyboard, speaker, IPL, serial link to the ZX8302
// ---------------------------------------------------------------------------
static uint8_t ipc_port_in(void *u, uint16_t port)
{
    (void)u;
    if (port == MCS48_P2) return (uint8_t)(q.comdata_to_ipc << 7);
    if (port == MCS48_BUS) {
        uint8_t d = 0;
        for (int r = 0; r < 8; r++)
            if (q.keylatch & (1 << r)) d |= q.keys[r] | q.auto_keys[r];
        return d;
    }
    if (port == MCS48_T1) return q.zx.baudx4 ? 1 : 0;
    return 0xff;
}

static void ipc_port_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    if (port == MCS48_P1) { q.keylatch = v; return; }
    if (port != MCS48_P2) return;
    q.speaker = (v >> 1) & 1;
    int ipl = (((v >> 2) & 1) << 1) | ((v >> 3) & 1);
    if (ipl != q.ipc_ipl) {
        q.ipc_ipl = ipl;
        q.irq_dirty = true;
    }
    zx8302_comdata_w(&q.zx, (v >> 7) & 1);
}

static void ipc_ext_out(void *u, uint8_t a, uint8_t v)
{
    (void)u; (void)a; (void)v;
    zx8302_comctl(&q.zx);
}

// ---------------------------------------------------------------------------
// Microdrives
// ---------------------------------------------------------------------------
static void update_mdv_gap(void)
{
    int gap = 1;
    if (ql_mdv_selected(&q.mdv[0]) && ql_mdv_loaded(&q.mdv[0])) gap = ql_mdv_gap(&q.mdv[0]);
    else if (ql_mdv_selected(&q.mdv[1]) && ql_mdv_loaded(&q.mdv[1])) gap = ql_mdv_gap(&q.mdv[1]);
    zx8302_mdv_gap_w(&q.zx, gap);
}

static void tick_mdv_bits(void)
{
    if (!q.mdv[0].motor && !q.mdv[1].motor) return;
    update_mdv_gap();
    // JS only polls RX-full for ~20 instructions between pairs: an unread
    // pair is held briefly (dsp-cpp), not skipped.
    if (zx8302_mdv_delivering(&q.zx) && zx8302_mdv_rx_full(&q.zx)) {
        if (++q.mdv_stall < 80) return;
    } else {
        q.mdv_stall = 0;
    }
    bool data = (ql_mdv_selected(&q.mdv[0]) && ql_mdv_gap(&q.mdv[0]) == 0) ||
                (ql_mdv_selected(&q.mdv[1]) && ql_mdv_gap(&q.mdv[1]) == 0);
    if (data) {
        zx8302_mdv_raw1_w(&q.zx, ql_mdv_data1(&q.mdv[0]) | ql_mdv_data1(&q.mdv[1]));
        zx8302_mdv_raw2_w(&q.zx, ql_mdv_data2(&q.mdv[0]) | ql_mdv_data2(&q.mdv[1]));
    }
    ql_mdv_tick_bit(&q.mdv[0]);
    ql_mdv_tick_bit(&q.mdv[1]);
}

static uint16_t mdv_tx_pop(void *u) { (void)u; return zx8302_mdv_tx_pop(&q.zx); }

// ZX8302 outputs
static void zx_ipl1l(void *u, int s) { (void)u; q.zx_irq2 = s; q.irq_dirty = true; }
static void zx_comdata(void *u, int s) { (void)u; q.comdata_to_ipc = s; }
static void zx_mdseld(void *u, int s) { (void)u; ql_mdv_comms_in_w(&q.mdv[0], s); }
static void zx_mdselck(void *u, int s)
{
    (void)u;
    ql_mdv_clk_w(&q.mdv[1], s);
    ql_mdv_clk_w(&q.mdv[0], s);
    ql_mdv_comms_in_w(&q.mdv[1], ql_mdv_comms_out(&q.mdv[0]));
    update_mdv_gap();
}
static void zx_mdrdw(void *u, int s) { (void)u; ql_mdv_read_write_w(&q.mdv[0], s); ql_mdv_read_write_w(&q.mdv[1], s); }
static void zx_erase(void *u, int s) { (void)u; ql_mdv_erase_w(&q.mdv[0], s); ql_mdv_erase_w(&q.mdv[1], s); }
static void zx_mdv_burst(void *u) { (void)u; for (int i = 0; i < 8; i++) tick_mdv_bits(); }

// ---------------------------------------------------------------------------
// Time: the IPC, BAUDx4 and the microdrive clock follow the 68008
// ---------------------------------------------------------------------------
static void QL_HOT on_cycles(int cycles)
{
    uint32_t acc = q.ipc_acc + (uint32_t)cycles * IPC_MCLOCK;
    while (acc >= QL_CPU_CLOCK) {        // an IPC instruction per IPC cycle (dsp-cpp)
        acc -= QL_CPU_CLOCK;
        mcs48_step(&q.ipc);
    }
    q.ipc_acc = acc;
    uint32_t baud = q.baud_acc + (uint32_t)cycles * BAUDX4_HZ;
    while (baud >= QL_CPU_CLOCK) {
        baud -= QL_CPU_CLOCK;
        zx8302_tick_baudx4(&q.zx);
    }
    q.baud_acc = baud;
    uint32_t mdv = q.mdv_acc + (uint32_t)cycles * QL_MDV_BIT_RATE;
    while (mdv >= QL_CPU_CLOCK) {
        mdv -= QL_CPU_CLOCK;
        tick_mdv_bits();
    }
    q.mdv_acc = mdv;
    // audio: share of the slice with the speaker high, one sample per period
    uint32_t a = q.audio_acc + (uint32_t)cycles * QL_AUDIO_RATE;
    if (q.speaker) q.spk_on += (uint32_t)cycles;
    q.spk_total += (uint32_t)cycles;
    if (a >= QL_CPU_CLOCK) {
        while (a >= QL_CPU_CLOCK) {
            a -= QL_CPU_CLOCK;
            int16_t s = (int16_t)(q.spk_total ? (int32_t)(q.spk_on * 6000u / q.spk_total) - 3000 : -3000);
            if (q.naudio < QL_AUDIO_MAX) q.audio[q.naudio++] = s;
        }
        q.spk_on = q.spk_total = 0;
    }
    q.audio_acc = a;
}

// ---------------------------------------------------------------------------
// 68008 memory
// ---------------------------------------------------------------------------
static inline uint8_t *ram_ptr(uint32_t a)      // a in $20000-$3FFFF
{
    a -= 0x20000;
    return q.ram[a >> 15] + (a & 0x7fff);
}

static inline void mark_dirty(uint32_t a)
{
    if (a < 0x30000) {                  // screens: $20000 (0), $28000 (1)
        uint32_t o = a - 0x20000;
        uint32_t row = (o & 0x7fff) >> 7;
        q.dirty[o >> 15][row >> 5] |= 1u << (row & 31);
    }
}

static uint8_t io_read(uint32_t a)
{
    if (a >= 0x18000 && a <= 0x18003) return zx8302_rtc_r(&q.zx, a & 3);
    if (a == 0x18020) return zx8302_status_r(&q.zx);
    if (a == 0x18021) return q.zx.irq;
    if (a == 0x18022 || a == 0x18023) return zx8302_mdv_track_r(&q.zx, a & 1);
    return 0;
}

static void io_write(uint32_t a, uint8_t v)
{
    switch (a) {
    case 0x18000: case 0x18001: break;                  // RTC: not settable
    case 0x18002: zx8302_control_w(&q.zx, v); break;
    case 0x18003:
        zx8302_ipc_command_w(&q.zx, v);
        mcs48_run(&q.ipc, 64);
        break;
    case 0x18020: zx8302_mdv_control_w(&q.zx, v); break;
    case 0x18021: zx8302_irq_acknowledge_w(&q.zx, v); break;
    case 0x18022: zx8302_data_w(&q.zx, v); break;
    case 0x18063:
        if (v != q.video_ctrl) q.all_dirty = true;
        q.video_ctrl = v;
        break;
    default: break;
    }
}

unsigned int QL_HOT cpu_read_byte(unsigned int a)
{
    a &= 0xfffff;
    if (a >= 0x20000 && a < 0x40000) return *ram_ptr(a);
    if (a < QL_ROM_BYTES) return q.rom[a];
    if (a < 0x10000) return 0;
    return io_read(a);
}

unsigned int QL_HOT cpu_read_word(unsigned int a)
{
    a &= 0xfffff;
    if (a >= 0x20000 && a < 0x40000) {
        const uint8_t *p = ram_ptr(a);
        return ((unsigned)p[0] << 8) | p[1];
    }
    if (a < QL_ROM_BYTES - 1) return ((unsigned)q.rom[a] << 8) | q.rom[a + 1];
    return (cpu_read_byte(a) << 8) | cpu_read_byte(a + 1);
}

unsigned int QL_HOT cpu_read_long(unsigned int a)
{
    return (cpu_read_word(a) << 16) | cpu_read_word(a + 2);
}

unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }

void QL_HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    a &= 0xfffff;
    if (a >= 0x20000 && a < 0x40000) {
        *ram_ptr(a) = (uint8_t)v;
        mark_dirty(a);
        return;
    }
    if (a >= 0x18000 && a < 0x20000) io_write(a, (uint8_t)v);
}

void QL_HOT cpu_write_word(unsigned int a, unsigned int v)
{
    a &= 0xfffff;
    if (a >= 0x20000 && a < 0x40000 - 1) {
        uint8_t *p = ram_ptr(a);         // even address: same block
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
        mark_dirty(a);
        return;
    }
    cpu_write_byte(a, v >> 8);
    cpu_write_byte(a + 1, v & 0xff);
}

void QL_HOT cpu_write_long(unsigned int a, unsigned int v)
{
    cpu_write_word(a, v >> 16);
    cpu_write_word(a + 2, v & 0xffff);
}

void QL_HOT cpu_write_long_pd(unsigned int a, unsigned int v)
{
    cpu_write_word(a + 2, v & 0xffff);
    cpu_write_word(a, v >> 16);
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
void ql_init(const uint8_t *rom, const uint8_t *ipc, uint8_t *const ram[QL_RAM_BLOCKS], uint32_t unix_time)
{
    memset(&q, 0, sizeof q);
    q.rom = rom;
    q.ipc_rom = ipc;
    for (int i = 0; i < QL_RAM_BLOCKS; i++) q.ram[i] = ram[i];

    mcs48_init(&q.ipc, MCS48_I8749, ipc);
    q.ipc.port_in = ipc_port_in;
    q.ipc.port_out = ipc_port_out;
    q.ipc.ext_out = ipc_ext_out;

    q.zx.ipl1l = zx_ipl1l;
    q.zx.comdata = zx_comdata;
    q.zx.mdseld = zx_mdseld;
    q.zx.mdselck = zx_mdselck;
    q.zx.mdrdw = zx_mdrdw;
    q.zx.erase = zx_erase;
    q.zx.mdv_burst = zx_mdv_burst;
    zx8302_reset(&q.zx, unix_time);

    for (int i = 0; i < 2; i++) {
        ql_mdv_init(&q.mdv[i]);
        q.mdv[i].tx_pop = mdv_tx_pop;
    }

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    ql_reset();
}

void ql_reset(void)
{
    uint32_t ctr = q.zx.ctr;
    zx8302_reset(&q.zx, 0);
    q.zx.ctr = ctr;
    q.video_ctrl = 0;
    memset(q.keys, 0, sizeof q.keys);
    q.comdata_to_ipc = 1;
    q.ipc_ipl = 3;
    q.zx_irq2 = 0;
    q.speaker = 0;
    q.keylatch = 0;
    q.ipc_acc = q.baud_acc = q.mdv_acc = q.audio_acc = 0;
    q.mdv_stall = 0;
    q.debt = 0;
    q.spk_on = q.spk_total = 0;
    for (int i = 0; i < 2; i++) ql_mdv_reset(&q.mdv[i]);
    q.rtc_frames = q.flash_frames = 0;
    q.auto_frame = -1;
    memset(q.auto_keys, 0, sizeof q.auto_keys);
    q.flash = false;
    q.all_dirty = true;
    mcs48_reset(&q.ipc);
    mcs48_run(&q.ipc, 8000);            // to the COMDATA wait / keyboard scan
    m68k_pulse_reset();
    q.irq_dirty = false;
    apply_irqs();
}

int ql_run_frame(const int16_t **audio)
{
    q.naudio = 0;
    memset(q.auto_keys, 0, sizeof q.auto_keys);
    if (q.auto_frame >= 0) {
        // F1 (monitor) at the "F1...monitor F2...TV" prompt (on screen from
        // frame ~90): QDOS then runs mdv1_boot when there is a cartridge
        int f = q.auto_frame++;
        if (f >= QL_AUTOBOOT_FRAME && f < QL_AUTOBOOT_FRAME + 15) q.auto_keys[QLK_F1 >> 3] |= 1 << (QLK_F1 & 7);
        if (f >= QL_AUTOBOOT_FRAME + 15) q.auto_frame = -1;
    }
    int left = QL_FRAME_CYCLES - q.debt;
    while (left > 0) {
        int run = left < SLICE ? left : SLICE;
        int used = m68k_execute(run);
        ql_diag_slices++;
        on_cycles(used);
        left -= used;
        if (q.irq_dirty) {
            q.irq_dirty = false;
            apply_irqs();
        }
    }
    q.debt = -left;
    zx8302_vsync(&q.zx);
    if (q.irq_dirty) {
        q.irq_dirty = false;
        apply_irqs();
    }
    if (++q.rtc_frames >= 50) {
        q.rtc_frames = 0;
        zx8302_tick_rtc(&q.zx);
    }
    if (++q.flash_frames >= 25) {
        q.flash_frames = 0;
        q.flash = !q.flash;
        if (q.video_ctrl & 0x08) q.all_dirty = true;
    }
    if (audio) *audio = q.audio;
    return q.naudio;
}

void ql_key(int code, bool down)
{
    uint8_t bit = (uint8_t)(1u << (code & 7));
    if (down) q.keys[(code >> 3) & 7] |= bit;
    else q.keys[(code >> 3) & 7] &= (uint8_t)~bit;
}

void ql_autoboot(void) { q.auto_frame = 0; }

void ql_mount(const uint8_t *image)
{
    ql_cart_insert(0, image);
    ql_reset();
    ql_autoboot();
}

bool ql_autobooting(void) { return q.auto_frame >= 0; }

void ql_keys_clear(void) { memset(q.keys, 0, sizeof q.keys); }

void ql_cart_insert(int drive, const uint8_t *image) { ql_mdv_insert(&q.mdv[drive & 1], image); }
bool ql_cart_loaded(int drive) { return ql_mdv_loaded(&q.mdv[drive & 1]); }
bool ql_cart_motor(int drive) { return q.mdv[drive & 1].motor; }

const uint8_t *ql_screen(void) { return q.ram[(q.video_ctrl & 0x80) ? 1 : 0]; }
uint8_t ql_video_control(void) { return q.video_ctrl; }
bool ql_flash(void) { return q.flash; }

void ql_take_dirty_rows(uint32_t rows[8])
{
    int s = (q.video_ctrl & 0x80) ? 1 : 0;
    for (int i = 0; i < 8; i++) {
        rows[i] = q.all_dirty ? 0xffffffffu : q.dirty[s][i];
        q.dirty[s][i] = 0;
    }
    q.all_dirty = false;
}

uint16_t ql_ipc_pc(void) { return q.ipc.pc; }
uint32_t ql_pc(void) { return m68k_get_reg(NULL, M68K_REG_PC); }
uint8_t ql_peek(uint32_t a) { return (uint8_t)cpu_read_byte(a); }
