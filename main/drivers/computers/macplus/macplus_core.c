// macplus_core.c - the Macintosh Plus (see macplus_core.h): C11 migration of
// dsp-cpp's drivers/computers/macplus.cpp, on Musashi.
//
// dsp-cpp's 68000 calls the machine after every instruction; Musashi's
// instruction hook is called before every one, with REG_PPC still the
// previous instruction's address and REG_IR its opcode: the same moment.
// There the chips are brought up to the CPU's time, the .Sony / _LoadSeg
// traps are watched, and the interrupt level is given to the CPU (between
// instructions: a change in the middle of one waits for the hook).
#include "macplus_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "via6522.h"
#include "iwm.h"
#include "mac_rtc.h"
#include "mac_dsk.h"
#include "ncr5380.h"
#include "mac_dcmp.h"

#define VTOTAL 370
#define VBLANK_LINES 28
#define CYCLES_PER_LINE 352
#define ROM_SIZE 0x20000u
#define DCMP_STACK 8

static struct {
    uint8_t *ram;
    const uint8_t *rom_even, *rom_odd;
    via6522 via;
    iwm iwm;
    mac_rtc rtc;
    ncr5380 scsi;
    bool overlay;
    int screen_buffer;
    bool main_sound, snd_enable;
    int snd_vol;
    bool via_irq, scc_irq;
    int ipl, cur_ipl;
    bool mouse_button;
    uint8_t mouse_bit[2];
    int last_px, last_py, ptr_x, ptr_y;
    bool pointer_seen;
    int mouse_count_x, mouse_count_y;
    // SCC: WR2 / WR9 for the chip, WR1 / WR15 per channel, Ext/Status pending, DCD
    uint8_t scc_ptr[2], scc_wr1[2], scc_wr2, scc_wr9, scc_wr15[2];
    bool scc_ext_pending[2], scc_dcd[2];
    // time
    uint64_t total;                     // CPU cycles run
    uint64_t synced;                    // the chips' time
    bool in_cpu;
    int debt;
    int via_acc, kbd_acc;
    int64_t rtc_acc;
    bool rtc_ca2;
    // keyboard
    uint8_t kbd_cmd, kbd_reply, kbd_shift, instant;
    int kbd_bits;
    uint8_t kq[16];
    int kq_head, kq_tail;
    bool held[MK_COUNT];
    // .Sony for SuperDrive images, compressed resources
    uint32_t sony_prime, sony_ctl, sony_stat;
    uint32_t dcmp_ret[DCMP_STACK];
    int dcmp_sp;
    uint32_t read_ret[DCMP_STACK], read_pb[DCMP_STACK];
    int read_sp;
    // sound and picture
    uint32_t last_pc;                   // the instruction the last hook let run
    int16_t audio[VTOTAL];
    int naudio;
    bool turbo;
} P;

// ---------------------------------------------------------------------------
static inline uint8_t rom_byte(uint32_t a)
{
    a &= ROM_SIZE - 1;
    return (a & 1) ? P.rom_odd[a >> 1] : P.rom_even[a >> 1];
}

static inline uint32_t ram_index(uint32_t address)
{
    address &= 0xffffff;
    // $600000-$7FFFFF is the overlay-time window onto the first 2 MB; below
    // $400000 all 4 MB are unique (the ROM's memory test finds MemTop $400000)
    if (address >= 0x600000 && address < 0x800000) address -= 0x600000;
    return address & (MACPLUS_RAM - 1);
}

static inline uint8_t ram_at(uint32_t a) { return P.ram[ram_index(a)]; }
static inline void ram_set(uint32_t a, uint8_t v) { P.ram[ram_index(a)] = v; }

static void update_irqs(void)
{
    P.ipl = P.scc_irq ? 2 : P.via_irq ? 1 : 0;
}

static void apply_ipl(void)
{
    if (P.ipl != P.cur_ipl) {
        P.cur_ipl = P.ipl;
        m68k_set_irq((unsigned)P.ipl);
    }
}

// ---------------------------------------------------------------------------
// SCC (the mouse's quadrature on DCD A / B)
static uint8_t scc_read(uint32_t address)
{
    const int which = (address >> 1) & 3, ch = (which & 1) ? 0 : 1;     // 0 = A, 1 = B
    if (which & 2) return 0;
    const uint8_t rr = P.scc_ptr[ch];
    P.scc_ptr[ch] = 0;
    if (rr == 0) {
        // Rx empty, Tx empty (bit 2), CTS (bit 5); DCD (bit 3) is the mouse
        uint8_t v = 0x24;
        if (P.scc_dcd[ch]) v |= 0x08;
        return v;
    }
    if (rr == 2) {
        // channel B's RR2: the vector with the pending source (status low):
        // the ROM's mouse handler dispatches on it (one entry a mouse axis)
        if (ch == 1) {
            uint8_t src = 0;
            if (P.scc_ext_pending[0]) src = 0x5;
            else if (P.scc_ext_pending[1]) src = 0x1;
            return (uint8_t)((P.scc_wr2 & 0xf1) | (uint8_t)(src << 1));
        }
        return P.scc_wr2;
    }
    if (rr == 15) return P.scc_wr15[ch];
    return 0;
}

static void scc_write(uint32_t address, uint8_t value)
{
    const int which = (address >> 1) & 3, ch = (which & 1) ? 0 : 1;
    if (which & 2) return;
    if (P.scc_ptr[ch] == 0) {
        // WR0: the next register (command 1, Point High, adds 8), and the
        // reset commands: 2 (Ext/Status), 7 (highest IUS) for this channel
        const uint8_t cmd = (uint8_t)((value >> 3) & 7), reg = (uint8_t)(value & 7);
        P.scc_ptr[ch] = cmd == 1 ? (uint8_t)(reg + 8) : reg;
        if (cmd == 2 || cmd == 7) {
            P.scc_ext_pending[ch] = false;
            P.scc_irq = P.scc_ext_pending[0] || P.scc_ext_pending[1];
            update_irqs();
        }
        return;
    }
    const uint8_t reg = P.scc_ptr[ch];
    if (reg == 1) P.scc_wr1[ch] = value;
    else if (reg == 2) P.scc_wr2 = value;
    else if (reg == 9) P.scc_wr9 = value;
    else if (reg == 15) P.scc_wr15[ch] = value;
    P.scc_ptr[ch] = 0;
}

// ---------------------------------------------------------------------------
// VIA ports
static uint8_t via_pa_in(void *user) { (void)user; return 0x81; }   // PA7 SCC W/REQ idle high

static uint8_t via_pb_in(void *user)
{
    (void)user;
    uint8_t v = 0x40;                   // PB6 pulled up, mouse Y2 / X2, button, clock data
    v |= (uint8_t)(P.mouse_bit[1] << 5);
    v |= (uint8_t)(P.mouse_bit[0] << 4);
    if (!P.mouse_button) v |= 0x08;
    if (mac_rtc_data_r(&P.rtc)) v |= 0x01;
    return v;
}

static void via_pa_out(void *user, uint8_t data)
{
    (void)user;
    // PA6 screen buffer, PA5 head select, PA4 overlay, PA3 sound buffer,
    // PA2-0 volume: only the bits that are outputs (the DDR)
    const uint8_t ddr = P.via.ddr_a;
    if (ddr & 0x40) P.screen_buffer = (data & 0x40) ? 1 : 0;
    if (ddr & 0x20) iwm_set_hdsel(&P.iwm, (data & 0x20) != 0);
    if (ddr & 0x08) P.main_sound = (data & 0x08) != 0;
    if (ddr & 0x07) P.snd_vol = data & 7;
    if (ddr & 0x10) P.overlay = (data & 0x10) != 0;
}

static void via_pb_out(void *user, uint8_t data)
{
    (void)user;
    P.snd_enable = (data & 0x80) == 0;  // PB7 /SNDENB, PB2-0 the clock
    mac_rtc_ce(&P.rtc, (data & 0x04) != 0);
    mac_rtc_clk(&P.rtc, (data & 0x02) != 0);
    mac_rtc_data_w(&P.rtc, (data & 0x01) != 0);
}

static void via_irq(void *user, bool on)
{
    (void)user;
    P.via_irq = on;
    update_irqs();
}

// ---------------------------------------------------------------------------
// Keyboard (M0110A): commands shifted in on the VIA's SR, replies out
static void kbd_enqueue(uint8_t code)
{
    if (((P.kq_tail - P.kq_head) & 31) >= 16) return;
    P.kq[P.kq_tail & 15] = code;
    P.kq_tail++;
}

static uint8_t keyboard_reply(uint8_t command)
{
    switch (command) {
    case 0x10:                          // Inquiry
        if (P.kq_head != P.kq_tail) {
            const uint8_t ev = P.kq[P.kq_head & 15];
            P.kq_head++;
            const int code = ev & 0x7f;
            const uint8_t up = ev & 0x80;
            if (code < 64) return (uint8_t)((code << 1) | up);
            P.instant = (uint8_t)(((code - 64) << 1) | up);     // the keypad: $79, then the key
            return 0x79;
        }
        return 0x7b;                    // nothing
    case 0x14: {                        // Instant
        const uint8_t v = P.instant;
        P.instant = 0x7b;
        return v;
    }
    case 0x16: return 0x0b;             // model: M0110A
    case 0x36: return 0x7d;             // test: fine
    default: return 0x7b;
    }
}

static void clock_keyboard(void)
{
    const uint8_t sr_mode = (uint8_t)((P.via.acr >> 2) & 7);
    if (sr_mode == 3) {
        via6522_set_cb2_data(&P.via, (P.kbd_shift & 0x80) != 0);
        P.kbd_shift = (uint8_t)(P.kbd_shift << 1);
    }
    via6522_write_cb1(&P.via, true);
    via6522_write_cb1(&P.via, false);
    if (sr_mode == 7) {
        P.kbd_cmd = (uint8_t)((P.kbd_cmd << 1) | (via6522_cb2(&P.via) ? 1 : 0));
        if (++P.kbd_bits >= 8 || !via6522_sr_busy(&P.via)) {
            P.kbd_reply = keyboard_reply(P.kbd_cmd);
            P.kbd_shift = P.kbd_reply;
            P.kbd_cmd = 0;
            P.kbd_bits = 0;
        }
    } else if (sr_mode == 3 && !via6522_sr_busy(&P.via)) {
        P.kbd_shift = P.kbd_reply;
    }
}

// The chips up to the CPU's time
static void sync_to(uint64_t now)
{
    if (now <= P.synced) return;
    const int delta = (int)(now - P.synced);
    P.synced = now;
    iwm_tick(&P.iwm, delta);
    P.via_acc += delta;
    if (P.via_acc >= 10) {
        const int n = P.via_acc / 10;
        P.via_acc -= n * 10;
        via6522_tick(&P.via, n);
    }
    const uint8_t sr_mode = (uint8_t)((P.via.acr >> 2) & 7);
    if ((sr_mode == 3 || sr_mode == 7) && via6522_sr_busy(&P.via)) {
        P.kbd_acc += delta;
        if (P.kbd_acc >= 80) {
            P.kbd_acc = 0;
            clock_keyboard();
        }
    } else {
        P.kbd_acc = 0;
    }
    P.rtc_acc += delta;
    if (P.rtc_acc >= MACPLUS_CLOCK) {
        P.rtc_acc -= MACPLUS_CLOCK;
        mac_rtc_tick_second(&P.rtc);
        P.rtc_ca2 = !P.rtc_ca2;
        via6522_write_ca2(&P.via, P.rtc_ca2);
    }
}

static inline uint64_t cpu_now(void) { return P.total + (P.in_cpu ? (uint64_t)m68k_cycles_run() : 0); }

// ---------------------------------------------------------------------------
// Memory map (MAME macplus_map / mac128.cpp):
//   $000000-$3FFFFF RAM, or the ROM while the VIA's PA4 overlay is set
//   $400000-$4FFFFF ROM (A17 to /OE: $42xxxx is open bus)
//   $580000-$5FFFFF NCR 5380       $600000-$7FFFFF RAM window
//   $800000-$9FFFFF SCC read       $A00000-$BFFFFF SCC write
//   $C00000-$DFFFFF IWM            $E80000-$EFFFFF VIA
static uint8_t read_byte(uint32_t address)
{
    address &= 0xffffff;
    if (address < 0x400000) return P.overlay ? rom_byte(address) : ram_at(address);
    if (address < 0x500000) {
        // the 128K ROM decodes A17 onto /OE: $420000 is open bus (the ROM's
        // SCSI test compares it), $440000 is the ROM again
        if (address & 0x20000) return 0xff;
        return rom_byte(address);
    }
    if (address >= 0x580000 && address < 0x600000) return ncr5380_read(&P.scsi, address);
    if (address >= 0x600000 && address < 0x800000) return ram_at(address);
    if (address >= 0x800000 && address < 0xa00000) return scc_read(address);
    if (address >= 0xc00000 && address < 0xe00000) {
        sync_to(cpu_now());
        return iwm_read(&P.iwm, (uint8_t)((address >> 9) & 0x0f));
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        sync_to(cpu_now());
        return via6522_read(&P.via, (uint8_t)((address >> 9) & 0x0f));
    }
    return 0xff;
}

static void write_byte(uint32_t address, uint8_t value)
{
    address &= 0xffffff;
    // (with the overlay on, the writes below $400000 still reach the RAM)
    if (address < 0x400000) ram_set(address, value);
    else if (address >= 0x580000 && address < 0x600000) ncr5380_write(&P.scsi, address, value);
    else if (address >= 0x600000 && address < 0x800000) ram_set(address, value);
    else if (address >= 0xa00000 && address < 0xc00000) scc_write(address, value);
    else if (address >= 0xc00000 && address < 0xe00000) {
        sync_to(cpu_now());
        iwm_write(&P.iwm, (uint8_t)((address >> 9) & 0x0f), value);
    } else if (address >= 0xe80000 && address < 0xf00000) {
        sync_to(cpu_now());
        via6522_write(&P.via, (uint8_t)((address >> 9) & 0x0f), value);
    }
}

static uint16_t read_word(uint32_t address)
{
    address &= 0xfffffe;
    if (address < 0x400000 && !P.overlay) {
        const uint32_t i = ram_index(address);
        return (uint16_t)((P.ram[i] << 8) | P.ram[i + 1]);
    }
    if (address >= 0x580000 && address < 0x600000) {
        const uint8_t v = ncr5380_read(&P.scsi, address);
        return (uint16_t)((v << 8) | v);
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        sync_to(cpu_now());
        const uint8_t v = iwm_read(&P.iwm, (uint8_t)((address >> 9) & 0x0f));
        return (uint16_t)((v << 8) | v);
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        sync_to(cpu_now());
        const uint8_t v = via6522_read(&P.via, (uint8_t)((address >> 9) & 0x0f));
        return (uint16_t)((v << 8) | v);
    }
    if (address >= 0x800000 && address < 0xa00000) return (uint16_t)(scc_read(address) << 8);
    return (uint16_t)((read_byte(address) << 8) | read_byte(address + 1));
}

static void write_word(uint32_t address, uint16_t value)
{
    address &= 0xfffffe;
    if (address < 0x400000) {
        const uint32_t i = ram_index(address);
        P.ram[i] = (uint8_t)(value >> 8);
        P.ram[i + 1] = (uint8_t)value;
        return;
    }
    if (address >= 0x580000 && address < 0x600000) {
        ncr5380_write(&P.scsi, address | 1u, (uint8_t)value);
        return;
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        sync_to(cpu_now());
        iwm_write(&P.iwm, (uint8_t)((address >> 9) & 0x0f), (uint8_t)value);
        return;
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        sync_to(cpu_now());
        via6522_write(&P.via, (uint8_t)((address >> 9) & 0x0f), (uint8_t)(value >> 8));
        return;
    }
    if (address >= 0xa00000 && address < 0xc00000) {
        scc_write(address, (uint8_t)value);
        return;
    }
    write_byte(address, (uint8_t)(value >> 8));
    write_byte(address + 1, (uint8_t)value);
}

static uint32_t read_long(uint32_t a) { return ((uint32_t)read_word(a) << 16) | read_word(a + 2); }
static void write_long(uint32_t a, uint32_t v)
{
    write_word(a, (uint16_t)(v >> 16));
    write_word(a + 2, (uint16_t)v);
}
// (byte-wise, as dsp-cpp's helpers: odd addresses too)
static uint32_t peek_long(uint32_t a)
{
    return ((uint32_t)read_byte(a) << 24) | ((uint32_t)read_byte(a + 1) << 16) | ((uint32_t)read_byte(a + 2) << 8) | read_byte(a + 3);
}
static void poke_long(uint32_t a, uint32_t v)
{
    write_byte(a, (uint8_t)(v >> 24));
    write_byte(a + 1, (uint8_t)(v >> 16));
    write_byte(a + 2, (uint8_t)(v >> 8));
    write_byte(a + 3, (uint8_t)v);
}
static uint16_t peek_word(uint32_t a) { return (uint16_t)((read_byte(a) << 8) | read_byte(a + 1)); }
static void poke_word(uint32_t a, uint16_t v)
{
    write_byte(a, (uint8_t)(v >> 8));
    write_byte(a + 1, (uint8_t)v);
}

unsigned int cpu_read_byte(unsigned int a) { return read_byte(a); }
unsigned int cpu_read_word(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long(unsigned int a) { return read_long(a); }
unsigned int cpu_read_word_dasm(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return read_long(a); }
void cpu_write_byte(unsigned int a, unsigned int v) { write_byte(a, (uint8_t)v); }
void cpu_write_word(unsigned int a, unsigned int v) { write_word(a, (uint16_t)v); }
void cpu_write_long(unsigned int a, unsigned int v) { write_long(a, v); }
void cpu_write_long_pd(unsigned int a, unsigned int v)
{
    write_word(a + 2, (uint16_t)v);
    write_word(a, (uint16_t)(v >> 16));
}
int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
void cpu_set_fc(int fc) { (void)fc; }
void cpu_pulse_reset(void) {}

// ---------------------------------------------------------------------------
// .Sony for 1.44 MB images (PCE's sony.c): the driver's Prime / Control /
// Status served by LBA after their first instruction
#define REG(r) m68k_get_reg(NULL, r)
#define SETREG(r, v) m68k_set_reg(r, v)

static void find_sony_driver(void)
{
    // DRVR header $4F00, name ".Sony" at +18; entries at +8/+10/+12/+14
    // (not patched: the Plus checksums its ROM)
    P.sony_prime = P.sony_ctl = P.sony_stat = 0;
    for (uint32_t i = 1; i + 24 < ROM_SIZE; ++i) {
        if (rom_byte(i - 1) != 0x05 || rom_byte(i) != 0x2e) continue;
        if (rom_byte(i + 1) != 'S' || rom_byte(i + 2) != 'o' || rom_byte(i + 3) != 'n' || rom_byte(i + 4) != 'y') continue;
        const uint32_t drv = i - 19;
        if (drv >= ROM_SIZE || rom_byte(drv) != 0x4f || rom_byte(drv + 1) != 0x00) continue;
#define ENTRY(w) (0x400000u + drv + (uint32_t)((rom_byte(drv + 8 + (w) * 2) << 8) | rom_byte(drv + 9 + (w) * 2)))
        P.sony_prime = ENTRY(1);
        P.sony_ctl = ENTRY(2);
        P.sony_stat = ENTRY(3);
#undef ENTRY
        break;
    }
}

static void mark_sony_inserted(void)
{
    const uint32_t vars = peek_long(0x0134) & 0xffffffu;
    if (vars < 0x100 || vars + 8 + 66 + 22 >= MACPLUS_RAM) return;
    const uint32_t d1 = vars + 8 + 66;
    ram_set(d1 + 3, 0x02);
    ram_set(d1 + 5, 0xff);
    ram_set(d1 + 18, 0xff);
    ram_set(d1 + 19, 0xff);
    // drive 2: the empty external Sony
    if (vars + 8 + 132 + 4 < MACPLUS_RAM) ram_set(vars + 8 + 132 + 3, 0);
}

static void sony_return(int16_t result)
{
    const uint32_t pb = REG(M68K_REG_A0) & 0xffffffu;
    if (pb + 18 < MACPLUS_RAM) poke_word(pb + 16, (uint16_t)result);
    SETREG(M68K_REG_D0, result < 0 ? (uint32_t)(int32_t)result : 0);
    const uint16_t trap = pb + 8 < MACPLUS_RAM ? peek_word(pb + 6) : 0;
    const uint32_t jiodone = peek_long(0x08fc) & 0xffffffu;
    if ((trap & 0x0200) == 0 && jiodone >= 0x100 && jiodone < 0x420000) {
        SETREG(M68K_REG_PC, jiodone);
        return;
    }
    const uint32_t sp = REG(M68K_REG_A7) & 0xffffffu;
    SETREG(M68K_REG_PC, peek_long(sp));
    SETREG(M68K_REG_A7, sp + 4);
}

static void sony_prime(void)
{
    mark_sony_inserted();
    const uint32_t pb = REG(M68K_REG_A0) & 0xffffffu, dce = REG(M68K_REG_A1) & 0xffffffu;
    if (pb + 50 >= MACPLUS_RAM) { sony_return(-50); return; }
    const uint16_t trap = peek_word(pb + 6), vref = peek_word(pb + 22);
    if (vref != 1 && vref != 0) { sony_return(-56); return; }
    const uint16_t posmode = peek_word(pb + 44);
    uint32_t ofs = 0;
    switch (posmode & 0x0f) {
    case 1: ofs = peek_long(pb + 46); break;
    case 3:
        ofs = peek_long(pb + 46);
        if (dce + 20 < MACPLUS_RAM) ofs += peek_long(dce + 16);
        break;
    default:
        if (dce + 20 < MACPLUS_RAM) ofs = peek_long(dce + 16);
        break;
    }
    const uint32_t cnt = peek_long(pb + 36), addr = peek_long(pb + 32) & 0xffffffu;
    if (posmode & 0x40) {
        poke_long(pb + 40, cnt);
        if (dce + 20 < MACPLUS_RAM) poke_long(dce + 16, ofs + cnt);
        sony_return(0);
        return;
    }
    if ((ofs & 511) || (cnt & 511) || cnt == 0) { sony_return(-50); return; }
    const bool writing = (trap & 0xff) == 3;
    uint8_t buf[512];
    for (uint32_t i = 0; i < cnt / 512; ++i) {
        const uint32_t lba = ofs / 512 + i;
        if (addr + i * 512 + 512 > MACPLUS_RAM) { sony_return(writing ? -20 : -19); return; }
        if (writing) {
            for (int b = 0; b < 512; b++) buf[b] = read_byte(addr + i * 512 + (uint32_t)b);
            if (!mac_dsk_write_lba(&P.iwm.disk, lba, buf)) { sony_return(-20); return; }
        } else {
            if (!mac_dsk_read_lba(&P.iwm.disk, lba, buf)) { sony_return(-19); return; }
            for (int b = 0; b < 512; b++) write_byte(addr + i * 512 + (uint32_t)b, buf[b]);
        }
    }
    poke_long(pb + 40, cnt);
    if (dce + 20 < MACPLUS_RAM) poke_long(dce + 16, ofs + cnt);
    sony_return(0);
}

static void sony_control(void)
{
    const uint32_t pb = REG(M68K_REG_A0) & 0xffffffu;
    if (pb + 28 >= MACPLUS_RAM) { sony_return(-17); return; }
    const uint16_t vref = peek_word(pb + 22), cs = peek_word(pb + 26);
    if (vref != 1 && vref != 0 && cs != 7) { sony_return(-56); return; }
    switch (cs) {
    case 1: sony_return(-27); return;
    case 5: case 7: case 8: sony_return(0); return;
    case 6: sony_return(-50); return;
    case 9: sony_return((int16_t)0xffc8); return;
    case 23: poke_long(pb + 28, 0x00000400); sony_return(0); return;   // a SuperDrive-capable drive
    default: sony_return(-17); return;
    }
}

static void sony_status(void)
{
    const uint32_t pb = REG(M68K_REG_A0) & 0xffffffu;
    if (pb + 34 >= MACPLUS_RAM) { sony_return(-18); return; }
    const uint16_t vref = peek_word(pb + 22), cs = peek_word(pb + 26);
    if (vref != 1 && vref != 0) { sony_return(-64); return; }
    mark_sony_inserted();
    if (cs == 8) {
        const uint32_t vars = peek_long(0x0134) & 0xffffffu, d1 = vars + 8 + 66;
        if (vars >= 0x100 && d1 + 22 < MACPLUS_RAM)
            for (int i = 0; i < 11; i++) poke_word(pb + 28 + (uint32_t)i * 2, peek_word(d1 + (uint32_t)i * 2));
        sony_return(0);
        return;
    }
    if (cs == 6) {
        // PCE's format list for a 1.44 MB disk: 2880 blocks, HD MFM
        const uint16_t maxfmt = peek_word(pb + 28);
        const uint32_t ptr = peek_long(pb + 30) & 0xffffffu;
        if (maxfmt && ptr + 8 < MACPLUS_RAM) {
            poke_long(ptr, 2880);
            poke_long(ptr + 4, 0xd2120050);
            poke_word(pb + 28, 1);
        }
        sony_return(0);
        return;
    }
    sony_return(-18);
}

// ---------------------------------------------------------------------------
// System 7's compressed resources, expanded after the traps that load them
static bool maybe_decompress_ptr(uint32_t ptr, uint32_t handle)
{
    ptr &= 0xffffffu;
    if (ptr < 0x100 || ptr + 18 > MACPLUS_RAM) return false;
    if (peek_long(ptr) != 0xa89f6572u) return false;
    const uint32_t expect = peek_long(ptr + 8);
    if (expect < 2 || expect > 0x100000) return false;
    uint32_t data_len = expect + 0x100;
    if (data_len < 0x20000) data_len = 0x20000;
    if (ptr + data_len > MACPLUS_RAM) data_len = MACPLUS_RAM - ptr;
    uint8_t *src = (uint8_t *)malloc(data_len);
    if (!src) return false;
    for (uint32_t i = 0; i < data_len; i++) src[i] = ram_at(ptr + i);
    size_t n = 0;
    uint8_t *out = mac_decompress_resource(src, data_len, &n);
    free(src);
    if (!out) return false;
    uint32_t buf = peek_long(0x010c) & 0xffffffu;
    const uint32_t need = (uint32_t)((n + 1) & ~(size_t)1);
    if (buf < need + 0x2008) {
        free(out);
        return false;
    }
    buf = (buf - (need + 8)) & ~1u;
    poke_long(buf, 0x80000000u | (need + 8));
    poke_long(buf + 4, handle);
    const uint32_t data = buf + 8;
    for (size_t i = 0; i < n; i++) ram_set(data + (uint32_t)i, out[i]);
    if (need > n) ram_set(data + (uint32_t)n, 0);
    free(out);
    if (handle >= 0x100 && handle + 4 <= MACPLUS_RAM) poke_long(handle, data);
    poke_long(0x010c, buf);
    const uint32_t limit = buf > 0x400 ? buf - 0x400 : buf;
    if ((peek_long(0x0130) & 0xffffffu) > limit) poke_long(0x0130, limit);
    static const uint32_t zaddr[3] = {0x02aa, 0x02a6, 0x0118};
    for (int z = 0; z < 3; z++) {
        const uint32_t zone = peek_long(zaddr[z]) & 0xffffffu;
        if (zone >= 0x1000 && zone + 4 < MACPLUS_RAM && (peek_long(zone) & 0xffffffu) > buf) poke_long(zone, buf);
    }
    return true;
}

static void maybe_decompress_handle(uint32_t handle)
{
    handle &= 0xffffffu;
    if (handle < 0x100 || handle + 4 > MACPLUS_RAM) return;
    maybe_decompress_ptr(peek_long(handle) & 0xffffffu, handle);
}

static void sweep_compressed_handles(void)
{
    // later ROMs expand before _LoadSeg / _Launch; a nested GetResource on
    // the 128K's Resource Manager can miss a compressed CODE handle
    uint32_t hi = peek_long(0x010c) & 0xffffffu;
    if (hi < 0x8000 || hi > MACPLUS_RAM) hi = 0x40000;
    uint32_t start = peek_long(0x02a6) & 0xffffffu;
    if (start < 0x1000 || start >= hi) start = 0x1400;
    for (uint32_t h = start; h + 8 < hi; h += 4) {
        const uint32_t p = peek_long(h) & 0xffffffu;
        if (p < 0x1008 || p + 18 >= MACPLUS_RAM) continue;
        if (peek_long(p) != 0xa89f6572u) continue;
        maybe_decompress_handle(h);
    }
}

static void sanitize_mountvol_pb(void)
{
    const uint32_t pb = REG(M68K_REG_A0) & 0xffffffu;
    if (pb + 0x16u >= MACPLUS_RAM) return;
    for (int k = 0; k < 4; k++) ram_set(pb + 0x0c + (uint32_t)k, 0), ram_set(pb + 0x12 + (uint32_t)k, 0);
}

// After every instruction (before the next: see the top)
void cpu_instr_callback(unsigned int pc)
{
    // (REG_PPC is the current PC at the start of a slice: keep our own)
    const uint32_t ppc = P.last_pc;
    if (P.iwm.disk.hd && P.sony_prime) {
        if (ppc == P.sony_prime) {
            if (peek_word(P.sony_prime) == 0x2f38) SETREG(M68K_REG_A7, REG(M68K_REG_A7) + 4);
            sony_prime();
        } else if (ppc == P.sony_ctl) {
            if (peek_word(P.sony_ctl) == 0x2f38) SETREG(M68K_REG_A7, REG(M68K_REG_A7) + 4);
            sony_control();
        } else if (ppc == P.sony_stat) {
            sony_status();
        }
        pc = REG(M68K_REG_PC);
    }
    if (P.dcmp_sp > 0 && pc == P.dcmp_ret[P.dcmp_sp - 1]) {
        P.dcmp_sp--;
        maybe_decompress_handle(REG(M68K_REG_A0));
        maybe_decompress_handle(REG(M68K_REG_D0));
    }
    if (P.read_sp > 0 && pc == P.read_ret[P.read_sp - 1]) {
        P.read_sp--;
        const uint32_t pb = P.read_pb[P.read_sp] & 0xffffffu;
        if (pb + 0x24u < MACPLUS_RAM) {
            const uint32_t buf = peek_long(pb + 0x20) & 0xffffffu;
            if (buf >= 0x1008 && buf + 22 < MACPLUS_RAM) {
                uint32_t src = 0;
                if (peek_long(buf) == 0xa89f6572u) src = buf;
                else if (peek_long(buf + 4) == 0xa89f6572u) src = buf + 4;
                if (src) {
                    const uint32_t hint = peek_long(buf - 4) & 0xffffffu;
                    if (maybe_decompress_ptr(src, hint) || maybe_decompress_ptr(src, 0))
                        poke_long(pb + 0x20, peek_long(0x010c));
                }
            }
        }
    }
    const uint16_t op = (uint16_t)REG(M68K_REG_IR);
    if ((op & 0xf000) == 0xa000) {
        if (op == 0xa829 || op == 0xa9a0 || op == 0xa81a || op == 0xa9a2 || op == 0xa1a0 || op == 0xa11a ||
            op == 0xa80c || op == 0xa81f || op == 0xa895) {
            if (P.dcmp_sp < DCMP_STACK) P.dcmp_ret[P.dcmp_sp++] = (ppc + 2) & 0xffffffu;
        }
        if (op == 0xa00f) sanitize_mountvol_pb();
        if (op == 0xa9f0 || op == 0xa9f2) sweep_compressed_handles();
        if (op == 0xa002 && P.read_sp < DCMP_STACK) {
            P.read_ret[P.read_sp] = (ppc + 2) & 0xffffffu;
            P.read_pb[P.read_sp] = REG(M68K_REG_A0);
            P.read_sp++;
        }
    }
    sync_to(cpu_now());
    apply_ipl();
    P.last_pc = REG(M68K_REG_PC) & 0xffffffu;
}

// ---------------------------------------------------------------------------
// The mouse
static void mouse_pulse(int ch, int dir)
{
    if (!dir) return;
    P.scc_dcd[ch] = !P.scc_dcd[ch];
    const uint8_t last = (uint8_t)P.scc_dcd[ch];
    P.mouse_bit[ch] = dir < 0 ? last : (uint8_t)(last ? 0 : 1);
    if (P.scc_wr1[ch] & 0x01) {
        P.scc_ext_pending[ch] = true;
        P.scc_irq = true;
        update_irqs();
    }
}

static void mouse_tick(void)
{
    // mac128.cpp's mouse_callback: a step a axis towards the host's pointer
    // (Y counted the other way round)
    int dx = 0, dy = 0;
    if (P.mouse_count_x < 0) P.mouse_count_x++, dx = -1;
    else if (P.mouse_count_x > 0) P.mouse_count_x--, dx = 1;
    if (P.mouse_count_y < 0) P.mouse_count_y++, dy = 1;
    else if (P.mouse_count_y > 0) P.mouse_count_y--, dy = -1;
    if (dx) mouse_pulse(0, dx);
    if (dy) mouse_pulse(1, dy);
}

void macplus_pointer(int x, int y, bool button)
{
    P.mouse_button = button;
    P.ptr_x = x;
    P.ptr_y = y;
    if (!P.pointer_seen) {
        P.last_px = x;
        P.last_py = y;
        P.pointer_seen = true;
        return;
    }
    // once the ROM runs the SCC mouse interrupts, the host pointer goes
    // straight into the cursor globals (as Mini vMac does): MTemp, RawMouse,
    // CrsrNew = CrsrCouple; no acceleration to drift it away
    if ((P.scc_wr9 & 0x08) && !P.overlay) {
        const int cx = x < 0 ? 0 : x > MACPLUS_W - 1 ? MACPLUS_W - 1 : x;
        const int cy = y < 0 ? 0 : y > MACPLUS_H - 1 ? MACPLUS_H - 1 : y;
        const uint8_t pos[4] = {(uint8_t)(cy >> 8), (uint8_t)cy, (uint8_t)(cx >> 8), (uint8_t)cx};
        bool same = true;
        for (int i = 0; i < 4; i++) same = same && ram_at(0x828 + (uint32_t)i) == pos[i] && ram_at(0x82c + (uint32_t)i) == pos[i];
        if (!same) {
            for (int i = 0; i < 4; i++) {
                ram_set(0x828 + (uint32_t)i, pos[i]);
                ram_set(0x82c + (uint32_t)i, pos[i]);
            }
            ram_set(0x8ce, ram_at(0x8cf));
        }
        P.mouse_count_x = P.mouse_count_y = 0;
    } else {
        P.mouse_count_x += x - P.last_px;
        P.mouse_count_y += y - P.last_py;
    }
    P.last_px = x;
    P.last_py = y;
}

void macplus_mouse(int dx, int dy, bool button)
{
    int x = P.ptr_x + dx, y = P.ptr_y + dy;
    x = x < 0 ? 0 : x > MACPLUS_W - 1 ? MACPLUS_W - 1 : x;
    y = y < 0 ? 0 : y > MACPLUS_H - 1 ? MACPLUS_H - 1 : y;
    macplus_pointer(x, y, button);
}

void macplus_key(int code, bool down)
{
    if (code < 0 || code >= MK_COUNT || P.held[code] == down) return;
    P.held[code] = down;
    kbd_enqueue((uint8_t)(code | (down ? 0 : 0x80)));
}

void macplus_keys_clear(void)
{
    for (int c = 0; c < MK_COUNT; c++)
        if (P.held[c]) macplus_key(c, false);
}

// ---------------------------------------------------------------------------
bool macplus_init(const uint8_t *rom_even, const uint8_t *rom_odd)
{
    if (!rom_even || !rom_odd) return false;
    if (!P.ram) P.ram = (uint8_t *)malloc(MACPLUS_RAM);
    if (!P.ram) return false;
    P.rom_even = rom_even;
    P.rom_odd = rom_odd;
    iwm_init(&P.iwm);
    ncr5380_init(&P.scsi, true, true);
    P.via.pa_in = via_pa_in;
    P.via.pa_out = via_pa_out;
    P.via.pb_in = via_pb_in;
    P.via.pb_out = via_pb_out;
    P.via.irq = via_irq;
    find_sony_driver();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    macplus_reset();
    return true;
}

void macplus_reset(void)
{
    memset(P.ram, 0, MACPLUS_RAM);
    P.overlay = true;
    P.screen_buffer = 1;
    P.main_sound = true;
    P.snd_enable = false;
    P.snd_vol = 3;
    P.via_irq = P.scc_irq = false;
    P.ipl = P.cur_ipl = 0;
    P.mouse_button = false;
    P.mouse_bit[0] = P.mouse_bit[1] = 0;
    P.pointer_seen = false;
    P.mouse_count_x = P.mouse_count_y = 0;
    memset(P.scc_ptr, 0, sizeof P.scc_ptr);
    memset(P.scc_wr1, 0, sizeof P.scc_wr1);
    P.scc_wr2 = P.scc_wr9 = 0;
    P.scc_wr15[0] = P.scc_wr15[1] = 0xf8;
    P.scc_ext_pending[0] = P.scc_ext_pending[1] = false;
    P.scc_dcd[0] = P.scc_dcd[1] = false;
    P.via_acc = P.kbd_acc = 0;
    P.rtc_acc = 0;
    P.rtc_ca2 = false;
    P.kbd_cmd = P.kbd_bits = 0;
    P.kbd_reply = P.kbd_shift = P.instant = 0x7b;
    P.kq_head = P.kq_tail = 0;
    memset(P.held, 0, sizeof P.held);
    P.dcmp_sp = P.read_sp = 0;
    P.last_pc = 0xffffffffu;
    P.naudio = 0;
    P.debt = 0;
    P.total = P.synced = 0;
    P.in_cpu = false;
    via6522_reset(&P.via);
    iwm_reset(&P.iwm);
    mac_rtc_reset(&P.rtc);
    ncr5380_reset(&P.scsi);
    m68k_set_irq(0);
    m68k_pulse_reset();
}

void macplus_set_turbo(bool on) { P.turbo = on; }

static void run_cpu(int cycles)
{
    int left = cycles - P.debt;
    while (left > 0) {
        apply_ipl();
        P.in_cpu = true;
        const int used = m68k_execute(left);
        P.in_cpu = false;
        P.total += (uint64_t)used;
        left -= used;
        sync_to(P.total);
    }
    P.debt = -left;
}

int macplus_run_frame(const int16_t **audio)
{
    P.naudio = 0;
    for (int line = 0; line < VTOTAL; line++) {
        const bool vblank = line < VBLANK_LINES;
        via6522_write_ca1(&P.via, vblank);
        via6522_set_pb_line(&P.via, 6, !vblank);
        if (line % 10 == 0) mouse_tick();
        run_cpu(CYCLES_PER_LINE);
        // the sound buffer: a word a line (its high byte), 370 a frame
        int16_t s = 0;
        if (P.snd_enable && !P.turbo) {
            const uint32_t off = MACPLUS_RAM - (P.main_sound ? 0x0300u : 0x5F00u);
            const int32_t v = ((int32_t)P.ram[(off + (uint32_t)line * 2) & (MACPLUS_RAM - 1)] - 128) * (P.snd_vol + 1) * 32;
            s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        P.audio[P.naudio++] = s;
    }
    if (audio) *audio = P.audio;
    return P.naudio;
}

// ---------------------------------------------------------------------------
const uint8_t *macplus_screen(void)
{
    return P.ram + MACPLUS_RAM - (P.screen_buffer ? 0x5900u : 0xD900u);
}

void macplus_render_row(int y, uint32_t *out)
{
    const uint8_t *v = macplus_screen() + y * (MACPLUS_W / 8);
    for (int x = 0; x < MACPLUS_W / 8; x++)
        for (int b = 0; b < 8; b++) out[x * 8 + b] = (v[x] & (0x80 >> b)) ? 0x000000 : 0xffffff;
}

void macplus_render_row565(int y, uint16_t *out)
{
    const uint8_t *v = macplus_screen() + y * (MACPLUS_W / 8);
    for (int x = 0; x < MACPLUS_W / 8; x++)
        for (int b = 0; b < 8; b++) out[x * 8 + b] = (v[x] & (0x80 >> b)) ? 0x0000 : 0xffff;
}

// ---------------------------------------------------------------------------
const char *macplus_insert_floppy(uint8_t *data, size_t size)
{
    const char *why = "not a Macintosh 400K / 800K / 1.44MB disk";
    if (!mac_dsk_load(&P.iwm.disk, data, size, &why)) return why;
    iwm_disk_changed(&P.iwm);
    return NULL;
}

const char *macplus_attach_scsi(blkfile *f) { return ncr5380_attach(&P.scsi, f); }

const char *macplus_load_file(const char *path)
{
    blkfile f;
    if (!blkfile_open(&f, path)) return "cannot open the file";
    uint8_t hdr[0x54];
    const bool small = f.size <= 0x54 + 1474560 + 0x10000;
    const bool floppy = small && f.size >= sizeof hdr && f.read(f.ctx, 0, hdr, sizeof hdr) &&
                        mac_dsk_looks_like_header(hdr, sizeof hdr, f.size);
    if (floppy) {
        uint8_t *data = (uint8_t *)malloc(f.size);
        const bool ok = data && f.read(f.ctx, 0, data, f.size);
        const uint32_t n = f.size;
        blkfile_close(&f);
        if (!ok) {
            free(data);
            return "cannot read the file";
        }
        const char *err = macplus_insert_floppy(data, n);
        if (err) free(data);
        return err;
    }
    // (a Disk Copy floppy not of those sizes is no SCSI disk either)
    return macplus_attach_scsi(&f);
}

void macplus_eject_floppy(void)
{
    mac_dsk_free(&P.iwm.disk);
    iwm_disk_changed(&P.iwm);
}

void macplus_detach_scsi(void) { ncr5380_detach(&P.scsi); }
bool macplus_floppy_loaded(void) { return P.iwm.disk.loaded; }
bool macplus_scsi_loaded(void) { return P.scsi.loaded; }
bool macplus_drive_led(void) { return iwm_motor_on(&P.iwm); }
