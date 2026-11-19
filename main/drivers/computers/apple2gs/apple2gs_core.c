// apple2gs_core.c - the Apple IIGS (see apple2gs_core.h): C11 migration of
// dsp-cpp's drivers/computers/apple2gs.cpp.
//
// The memory map: dsp-cpp keeps a read / write pointer and a flag for every
// 256-byte page of the 16 MB; here only the four banks that move ($00, $01,
// $E0, $E1) have page tables (rebuilt by the same rules), the rest is fixed:
// fast RAM, the ROM in $FC-$FF, nothing elsewhere. The SmartPort disks are
// read and written a block at a time from their files.
#include "apple2gs_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "w65c816.h"
#include "es5503.h"

#define MASTER_CLOCK 28636363u
#define FAST_CLOCK (MASTER_CLOCK / 10)
#define TICKS_FAST 5                    // 14.318 MHz ticks a fast cycle
#define TICKS_SLOW 14                   // ... a 1 MHz one
#define TICKS_PER_LINE (65 * TICKS_SLOW)
#define LINES_PER_FRAME 262
#define TICK_RATE 14318181u

// statereg ($C068) bits, and latches above bit 7
#define ALTZP 0x80u
#define PAGE2 0x40u
#define RAMRD 0x20u
#define RAMWRT 0x10u
#define RDROM 0x08u
#define LCBANK2 0x04u
#define INTCX 0x01u
#define PREWRITE 0x100u
#define WRDEFRAM 0x200u
#define INTC8 0x400u

#define WR_SHADOW_E0 0x01
#define WR_SHADOW_E1 0x02
#define WR_IGNORE 0x04

// ADB $C027
#define C027_MOUSE_DATA 0x80
#define C027_MOUSE_INT 0x40
#define C027_DATA_VALID 0x20
#define C027_DATA_INT 0x10
#define C027_KBD_VALID 0x08
#define C027_MOUSE_COORD 0x02
#define C027_CMD_FULL 0x01
#define C027_NEG_MASK ((uint8_t)~(C027_MOUSE_DATA | C027_DATA_VALID | C027_KBD_VALID | C027_MOUSE_COORD | C027_CMD_FULL))

enum {
    IRQ_SCAN = 1u << 0, IRQ_1SEC = 1u << 1, IRQ_QTRSEC = 1u << 2, IRQ_VBL = 1u << 3,
    IRQ_ADB_SRQ = 1u << 4, IRQ_ADB_DATA = 1u << 5, IRQ_ADB_MOUSE = 1u << 6, IRQ_DOC = 1u << 7,
};
enum { CLK_IDLE, CLK_TIME, CLK_INTERNAL, CLK_BRAM1, CLK_BRAM2 };
enum { ADB_IDLE, ADB_INCMD, ADB_SENDING };

typedef struct {
    a2gs_file f;
    bool in;
    uint32_t offset, blocks;
    bool dos_order;                     // a 140K DOS-order image, read in ProDOS order
    bool write_protect;
} gsdisk;

struct apple2gs {
    w65c816 cpu;
    int ram_banks;
    uint8_t *fast_ram;
    uint8_t slow_ram[0x20000];
    const uint8_t *rom_bank[4];         // $FC .. $FF
    const uint8_t *chr_rom;
    uint8_t slot7_rom[0x100];
    // page tables of banks $00, $01, $E0, $E1
    uint8_t *rd_page[4][256];
    uint8_t *wr_page[4][256];
    uint8_t wr_flags[4][256];

    uint32_t statereg;
    uint8_t shadow, speed, slotromsel, newvideo, c023, c041, c046, text_color, border, c034, c02b;
    bool st80, vid80, altchar, text, mixed, hires, an3, mono;

    uint64_t ticks, frame_start_tick, frame_count;
    int line, quarter_sec_counter, one_sec_counter;
    uint32_t irq_pending;

    // clock chip / battery RAM
    int clk_mode;
    bool clk_read;
    int clk_reg1;
    uint8_t c033;
    uint32_t clk_time;
    uint8_t bram[256];
    int64_t host_time;                  // set_time (0: the host's clock)

    // ADB GLU
    int adb_state;
    uint8_t adb_cmd;
    int adb_cmd_len, adb_cmd_so_far;
    uint8_t adb_cmd_data[16], adb_data[16];
    int adb_data_pending;
    uint8_t adb_interrupt_byte, c027, c025, adb_mode, adb_memory[256];
    int kbd_dev_addr, mouse_dev_addr;
    uint16_t kbd_reg3;
    uint8_t kbd_reg0[32];
    int kbd_reg0_n;
    uint8_t kbd_buf[8];
    int kbd_chars_buffered, kbd_read_no_update;
    bool key_down, hard_key_down;
    int a2code_down;
    uint64_t repeat_frame;
    int repeat_delay, repeat_rate;
    uint32_t virtual_key_up[4];
    bool held[GSK_COUNT];
    int mouse_target_x, mouse_target_y, mouse_a2_x, mouse_a2_y;
    bool mouse_button, mouse_a2_button, mouse_valid, mouse_coord;
    int ptr_x, ptr_y;                   // the pointer for apple2gs_mouse

    // sound
    es5503 doc;
    uint8_t doc_ram[ES5503_RAM];
    uint8_t doc_ctl, doc_saved;
    uint16_t doc_ptr;
    uint32_t doc_phase;
    uint64_t out_acc;
    int32_t doc_last;
    bool speaker;
    float hp_in, hp_out;
    int audio_rate;
    int16_t audio[A2GS_AUDIO_MAX];
    int naudio;
    bool turbo;

    // SCC (idle), IWM (empty drives)
    uint8_t scc_ptr[2], scc_wr2;
    uint8_t iwm_state, iwm_mode, c031;

    gsdisk disk[A2GS_DISKS];
    int disk_busy;

    uint32_t palette16[16];
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
};

// The IIGS's 16 colours (lo-res, text, border), 4 bits a gun
static const uint16_t iigs_colors[16] = {0x000, 0xd03, 0x009, 0xd2d, 0x072, 0x555, 0x22f, 0x6af,
                                         0x850, 0xf60, 0xaaa, 0xf98, 0x1d0, 0xff0, 0x4f9, 0xfff};

static inline uint32_t rgb12(uint16_t c)
{
    const uint32_t r = (c >> 8) & 0xf, g = (c >> 4) & 0xf, b = c & 0xf;
    return ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
}

static inline uint16_t text_row_offset(int row) { return (uint16_t)(((row & 7) << 7) + ((row >> 3) * 40)); }

// ---------------------------------------------------------------------------
// ADB keycode -> {unshifted, shifted, control} ASCII; values >= 0x100 are
// modifier bits for $C025 (shifted by 8); -1: no character
typedef struct { int16_t normal, shifted, ctrl; } key_ascii;
static const key_ascii k_ascii[128] = {
    {'a', 'A', 0x01}, {'s', 'S', 0x13}, {'d', 'D', 0x04}, {'f', 'F', 0x06},
    {'h', 'H', 0x08}, {'g', 'G', 0x07}, {'z', 'Z', 0x1a}, {'x', 'X', 0x18},
    {'c', 'C', 0x03}, {'v', 'V', 0x16}, {-1, -1, -1},     {'b', 'B', 0x02},
    {'q', 'Q', 0x11}, {'w', 'W', 0x17}, {'e', 'E', 0x05}, {'r', 'R', 0x12},
    {'y', 'Y', 0x19}, {'t', 'T', 0x14}, {'1', '!', -1},   {'2', '@', 0x00},
    {'3', '#', -1},   {'4', '$', -1},   {'6', '^', 0x1e}, {'5', '%', -1},
    {'=', '+', -1},   {'9', '(', -1},   {'7', '&', -1},   {'-', '_', 0x1f},
    {'8', '*', -1},   {'0', ')', -1},   {']', '}', 0x1d}, {'o', 'O', 0x0f},
    {'u', 'U', 0x15}, {'[', '{', 0x1b}, {'i', 'I', 0x09}, {'p', 'P', 0x10},
    {0x0d, 0x0d, -1}, {'l', 'L', 0x0c}, {'j', 'J', 0x0a}, {0x27, '"', -1},
    {'k', 'K', 0x0b}, {';', ':', -1},   {0x5c, '|', 0x1c}, {',', '<', -1},
    {'/', '?', 0x7f}, {'n', 'N', 0x0e}, {'m', 'M', 0x0d}, {'.', '>', -1},
    {0x09, 0x09, -1}, {' ', ' ', -1},   {'`', '~', -1},   {0x7f, 0x7f, -1},
    {-1, -1, -1},     {0x1b, 0x1b, -1}, {0x0200, 0x0200, -1}, {0x8000, 0x8000, -1},
    {0x0100, 0x0100, -1}, {0x0400, 0x0400, -1}, {0x4000, 0x4000, -1}, {0x08, 0x08, -1},
    {0x15, 0x15, -1}, {0x0a, 0x0a, -1}, {0x0b, 0x0b, -1}, {-1, -1, -1},
    // 0x40-0x7f: keypad and function keys
    {-1, -1, -1}, {0x102e, 0x102e, -1}, {-1, -1, -1}, {0x102a, 0x102a, -1},
    {-1, -1, -1}, {0x102b, 0x102b, -1}, {-1, -1, -1}, {0x1018, 0x1018, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {0x102f, 0x102f, -1},
    {0x100d, 0x100d, -1}, {-1, -1, -1}, {0x102d, 0x102d, -1}, {-1, -1, -1},
    {-1, -1, -1}, {0x103d, 0x103d, -1}, {0x1030, 0x1030, -1}, {0x1031, 0x1031, -1},
    {0x1032, 0x1032, -1}, {0x1033, 0x1033, -1}, {0x1034, 0x1034, -1}, {0x1035, 0x1035, -1},
    {0x1036, 0x1036, -1}, {0x1037, 0x1037, -1}, {-1, -1, -1}, {0x1038, 0x1038, -1},
    {0x1039, 0x1039, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {0x1072, 0x1072, -1}, {0x1073, 0x1073, -1},
    {0x1074, 0x1074, -1}, {0x1075, 0x1075, -1}, {-1, -1, -1}, {0x1077, 0x1077, -1},
    {-1, -1, -1}, {0x1079, 0x1079, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
};

// The SmartPort card's ROM in slot 7: a ProDOS block device whose boot,
// ProDOS ($C70A) and SmartPort ($C70D) entries all end in "WDM $C7"
static void build_slot7_rom(uint8_t *rom)
{
    static const uint8_t code[] = {
        0xa2, 0x20, 0xa0, 0x00, 0xa2, 0x03, 0xc9, 0x00, 0x80, 0x0c,   // C700: LDX #$20 LDY #0 LDX #3 CMP #0 BRA
        0x18, 0xb8, 0x70, 0x38, 0xb8, 0x42, 0xc7, 0x60,               // C70A: CLC CLV (BVS; $C70D SEC) CLV WDM $C7 RTS
        0xea, 0xea, 0xea, 0xea,
        0xe2, 0x41, 0x70, 0xf5,                                       // C716: SEP #$41 (boot: V=1) BVS $C70F
    };
    memset(rom, 0, 0x100);
    memcpy(rom, code, sizeof code);
    rom[0xfb] = 0x80;       // SmartPort ID: extended calls
    rom[0xfc] = 0x00;       // block count: from STATUS
    rom[0xfd] = 0x00;
    rom[0xfe] = 0xbf;       // status byte
    rom[0xff] = 0x0a;       // the ProDOS entry
}

// ---------------------------------------------------------------------------
// Interrupts
static void update_irq(apple2gs *m) { w65c816_set_irq(&m->cpu, m->irq_pending ? IRQ_ASSERT : IRQ_CLEAR); }
static void add_irq(apple2gs *m, uint32_t mask) { m->irq_pending |= mask; update_irq(m); }
static void remove_irq(apple2gs *m, uint32_t mask) { m->irq_pending &= ~mask; update_irq(m); }

static void doc_irq(void *user, bool on)
{
    apple2gs *m = (apple2gs *)user;
    if (on) add_irq(m, IRQ_DOC);
    else remove_irq(m, IRQ_DOC);
}

// ---------------------------------------------------------------------------
// Memory map
static inline int special_bank(uint32_t bank)
{
    return bank <= 1 ? (int)bank : bank == 0xe0 ? 2 : bank == 0xe1 ? 3 : -1;
}

static void map_pages(apple2gs *m, int k, int first, int count, uint8_t *rd, uint8_t *wr, uint8_t flags)
{
    for (int i = 0; i < count; i++) {
        m->rd_page[k][first + i] = rd ? rd + (size_t)i * 256 : NULL;
        m->wr_page[k][first + i] = wr ? wr + (size_t)i * 256 : NULL;
        m->wr_flags[k][first + i] = flags;
    }
}

static uint8_t shadow_flag(const apple2gs *m, int page, bool aux)
{
    const uint8_t sh = m->shadow, s = aux ? WR_SHADOW_E1 : WR_SHADOW_E0;
    if (page >= 0x04 && page < 0x08) return (sh & 0x01) ? 0 : s;
    if (page >= 0x08 && page < 0x0c) return (sh & 0x20) ? 0 : s;
    if (page >= 0x20 && page < 0x40) {
        if (!aux) return (sh & 0x02) ? 0 : s;
        return ((sh & 0x12) == 0 || (sh & 0x08) == 0) ? s : 0;
    }
    if (page >= 0x40 && page < 0x60) {
        if (!aux) return (sh & 0x04) ? 0 : s;
        return ((sh & 0x14) == 0 || (sh & 0x08) == 0) ? s : 0;
    }
    if (page >= 0x60 && page < 0xa0) return (aux && !(sh & 0x08)) ? s : 0;
    return 0;
}

static void rebuild_map(apple2gs *m)
{
    const uint32_t st = m->statereg;
    const bool altzp = st & ALTZP, page2 = st & PAGE2, ramrd = st & RAMRD, ramwrt = st & RAMWRT;
    const bool rdrom = st & RDROM, lcbank2 = st & LCBANK2, wrdefram = st & WRDEFRAM;
    const bool iolc_off = m->shadow & 0x40;
    // (bank $FF's ROM as one block: the language card reads it from $D000)
    uint8_t *romff = (uint8_t *)m->rom_bank[3];

    for (int k = 0; k < 4; k++) {
        const bool slow = k >= 2, is_aux = (k & 1) != 0;
        uint8_t *main = slow ? &m->slow_ram[0] : &m->fast_ram[0];
        uint8_t *aux = slow ? &m->slow_ram[0x10000] : &m->fast_ram[0x10000];
        uint8_t *self = is_aux ? aux : main;
        for (int page = 0; page < 0xc0; page++) {
            bool rd_aux = is_aux, wr_aux = is_aux;
            if (!is_aux) {
                if (page < 2) {
                    rd_aux = wr_aux = altzp;
                } else {
                    rd_aux = ramrd;
                    wr_aux = ramwrt;
                    if (m->st80 && page >= 0x04 && page < 0x08) rd_aux = wr_aux = page2;
                    if (m->st80 && m->hires && page >= 0x20 && page < 0x40) rd_aux = wr_aux = page2;
                }
            }
            m->rd_page[k][page] = (rd_aux ? aux : main) + (size_t)page * 256;
            m->wr_page[k][page] = (wr_aux ? aux : main) + (size_t)page * 256;
            m->wr_flags[k][page] = slow ? 0 : shadow_flag(m, page, wr_aux);
        }
        if (!slow && iolc_off) {
            // I/O and language card off: banks $00 / $01 are plain RAM
            uint8_t *mem = self;
            map_pages(m, k, 0xc0, 0x10, mem + 0xd000, mem + 0xd000, 0);
            if (!is_aux && altzp) mem = aux;
            map_pages(m, k, 0xd0, 0x10, mem + 0xc000, mem + 0xc000, 0);
            map_pages(m, k, 0xe0, 0x20, mem + 0xe000, mem + 0xe000, 0);
            continue;
        }
        map_pages(m, k, 0xc0, 0x10, NULL, NULL, 0);           // I/O and the slots' ROM
        // the language card ($D000-$FFFF); its bank 1 is at $C000
        uint8_t *mem = self;
        if (!is_aux && altzp) mem = aux;
        uint8_t *rd_mem = mem;
        const bool wr_ignore = !slow && !wrdefram, use_rom = !slow && rdrom;
        if (use_rom) rd_mem = romff;
        map_pages(m, k, 0xe0, 0x20, rd_mem + 0xe000, wr_ignore ? NULL : mem + 0xe000, wr_ignore ? WR_IGNORE : 0);
        uint32_t rd_off = 0xd000, wr_off = 0xd000;
        if (!lcbank2) {
            if (!rdrom) rd_off = 0xc000;
            wr_off = 0xc000;
        }
        if (use_rom) rd_off = 0xd000;
        map_pages(m, k, 0xd0, 0x10, rd_mem + rd_off, wr_ignore ? NULL : mem + wr_off, wr_ignore ? WR_IGNORE : 0);
    }
}

static uint32_t vector_address(void *user, uint32_t vector)
{
    // the interrupt and reset vectors come from the ROM unless I/O shadowing is off
    apple2gs *m = (apple2gs *)user;
    if (m->shadow & 0x40) return vector & 0xffff;
    return 0xff0000 | (vector & 0xffff);
}

static uint8_t io_read(apple2gs *m, uint16_t address);
static void io_write(apple2gs *m, uint16_t address, uint8_t value);

static uint8_t mem_read(void *user, uint32_t address)
{
    apple2gs *m = (apple2gs *)user;
    const uint32_t bank = address >> 16;
    const int k = special_bank(bank);
    if (k >= 0) {
        const uint8_t *p = m->rd_page[k][(address >> 8) & 0xff];
        if (p) return p[address & 0xff];
        const uint16_t off = (uint16_t)address;
        if (off >= 0xc000 && off < 0xd000) return io_read(m, off);
        return 0;
    }
    if (bank < (uint32_t)m->ram_banks) return m->fast_ram[address];
    if (bank >= 0xfc) return m->rom_bank[bank - 0xfc][address & 0xffff];
    return 0;
}

static void mem_write(void *user, uint32_t address, uint8_t value)
{
    apple2gs *m = (apple2gs *)user;
    const uint32_t bank = address >> 16;
    const int k = special_bank(bank);
    if (k < 0) {
        if (bank < (uint32_t)m->ram_banks) m->fast_ram[address] = value;
        return;
    }
    const int page = (address >> 8) & 0xff;
    uint8_t *p = m->wr_page[k][page];
    if (p) {
        p[address & 0xff] = value;
        const uint8_t f = m->wr_flags[k][page];
        if (f & (WR_SHADOW_E0 | WR_SHADOW_E1))
            m->slow_ram[((f & WR_SHADOW_E1) ? 0x10000u : 0u) | (address & 0xffff)] = value;
        return;
    }
    if (m->wr_flags[k][page] & WR_IGNORE) return;
    const uint16_t off = (uint16_t)address;
    if (off >= 0xc000 && off < 0xd000) io_write(m, off, value);
}

static inline uint8_t romff(const apple2gs *m, uint32_t off) { return m->rom_bank[3][off & 0xffff]; }

// ---------------------------------------------------------------------------
// Timing
static void sound_advance(apple2gs *m, int ticks);

static void on_cpu_cycles(void *user, int cycles)
{
    apple2gs *m = (apple2gs *)user;
    const int ticks = cycles * ((m->speed & 0x80) ? TICKS_FAST : TICKS_SLOW);
    m->ticks += (uint64_t)ticks;
    sound_advance(m, ticks);
}

static uint32_t lines_since_vbl_counter(const apple2gs *m)
{
    const uint64_t t = m->ticks - m->frame_start_tick;
    uint32_t line = (uint32_t)(t / TICKS_PER_LINE);
    const uint32_t col = (uint32_t)((t % TICKS_PER_LINE) / TICKS_SLOW);
    if (line >= LINES_PER_FRAME) line = LINES_PER_FRAME - 1;
    return (line << 8) | col;
}

static bool in_vbl(const apple2gs *m) { return (lines_since_vbl_counter(m) >> 8) >= 192; }

static uint8_t read_vid_counter(const apple2gs *m, bool vertical)
{
    const uint32_t v = lines_since_vbl_counter(m), line = v >> 8, col = v & 0xff;
    const uint32_t vcount = line < 256 ? 0x100 + line : 0xfa + (line - 256);
    if (vertical) return (uint8_t)(vcount >> 1);
    const uint32_t h = col == 0 ? 0 : 0x3f + col;
    return (uint8_t)(((vcount & 1) << 7) | (h & 0x7f));
}

static void update_clock_time(apple2gs *m)
{
    time_t now = m->host_time ? (time_t)(m->host_time + (int64_t)(m->frame_count / 60)) : time(NULL);
    const struct tm *lt = localtime(&now);
    if (!lt) return;
    // seconds since 1 January 1904, local time
    int64_t days = 0;
    for (int y = 1904; y < lt->tm_year + 1900; y++) days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    days += lt->tm_yday;
    m->clk_time = (uint32_t)(days * 86400 + lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
}

static void start_of_line(apple2gs *m, int line)
{
    m->line = line;
    if (line == 192) {
        if (m->c041 & 0x08) {
            m->c046 |= 0x08;
            add_irq(m, IRQ_VBL);
        }
        if (++m->quarter_sec_counter >= 16) {
            m->quarter_sec_counter = 0;
            if (m->c041 & 0x10) {
                m->c046 |= 0x10;
                add_irq(m, IRQ_QTRSEC);
            }
        }
        if (++m->one_sec_counter >= 60) {
            m->one_sec_counter = 0;
            m->c023 |= 0x40;
            if (m->c023 & 0x04) {
                m->c023 |= 0x80;
                add_irq(m, IRQ_1SEC);
            }
            update_clock_time(m);
        }
        if (m->mouse_valid && (m->c027 & C027_MOUSE_INT)) add_irq(m, IRQ_ADB_MOUSE);
    }
    // Super Hi-Res scan-line interrupts (SCB bit 6), as the line is drawn
    if (line < 200 && (m->newvideo & 0x80) && (m->slow_ram[0x19d00 + line] & 0x40)) {
        m->c023 |= 0x20;
        if (m->c023 & 0x02) {
            m->c023 |= 0x80;
            add_irq(m, IRQ_SCAN);
        }
    }
}

// ---------------------------------------------------------------------------
// Clock chip / battery RAM ($C033 / $C034)
static void do_clock_data(apple2gs *m)
{
    const bool read = (m->c034 & 0x40) != 0;
    switch (m->clk_mode) {
    case CLK_IDLE: {
        m->clk_read = (m->c033 >> 7) & 1;
        m->clk_reg1 = (m->c033 >> 2) & 3;
        const int op = (m->c033 >> 4) & 7;
        if (read) {
            m->clk_mode = CLK_IDLE;
            break;
        }
        switch (op) {
        case 0: m->clk_mode = CLK_TIME; break;
        case 3:
            m->clk_mode = CLK_INTERNAL;
            if (m->clk_reg1 & 2) {
                m->clk_mode = CLK_BRAM2;
                m->clk_reg1 = (m->c033 & 7) << 5;
            }
            break;
        case 2: m->clk_mode = CLK_BRAM1; m->clk_reg1 += 0x10; break;
        case 4: case 5: case 6: case 7:
            m->clk_mode = CLK_BRAM1;
            m->clk_reg1 = (m->c033 >> 2) & 0xf;
            break;
        default: break;
        }
        break;
    }
    case CLK_BRAM2:
        if (!read && (m->c033 & 0x83) == 0) {
            m->clk_reg1 |= (m->c033 >> 2) & 0x1f;
            m->clk_mode = CLK_BRAM1;
        } else {
            m->clk_mode = CLK_IDLE;
        }
        break;
    case CLK_BRAM1:
        if (read) {
            if (m->clk_read) m->c033 = m->bram[m->clk_reg1 & 0xff];
        } else if (!m->clk_read) {
            m->bram[m->clk_reg1 & 0xff] = m->c033;
        }
        m->clk_mode = CLK_IDLE;
        break;
    case CLK_TIME:
        if (read) {
            if (m->clk_read) m->c033 = (uint8_t)(m->clk_time >> (m->clk_reg1 * 8));
        } else if (!m->clk_read) {
            const uint32_t mask = 0xffu << (8 * m->clk_reg1);
            m->clk_time = (m->clk_time & ~mask) | ((uint32_t)m->c033 << (8 * m->clk_reg1));
        }
        m->clk_mode = CLK_IDLE;
        break;
    default:
        m->clk_mode = CLK_IDLE;
        break;
    }
}

static void clock_write_c034(apple2gs *m, uint8_t value)
{
    m->c034 = value & 0x7f;
    if (value & 0x80) do_clock_data(m);
}

// ---------------------------------------------------------------------------
// ADB GLU: the keyboard microcontroller, high level
static void warm_reset(apple2gs *m);

static void adb_reset(apple2gs *m)
{
    m->c027 = 0;
    m->key_down = m->hard_key_down = false;
    m->kbd_chars_buffered = 0;
    memset(m->kbd_buf, 0, sizeof m->kbd_buf);
    m->kbd_dev_addr = 2;
    m->mouse_dev_addr = 3;
    m->adb_data_pending = 0;
    m->adb_interrupt_byte = 0;
    m->adb_state = ADB_IDLE;
    m->mouse_coord = m->mouse_valid = false;
    m->kbd_reg0_n = 0;
    m->kbd_reg3 = 0x602;
    remove_irq(m, IRQ_ADB_DATA | IRQ_ADB_MOUSE | IRQ_ADB_SRQ);
}

static void update_adb_data_irq(apple2gs *m)
{
    if ((m->c027 & C027_DATA_INT) && (m->adb_data_pending > 0 || m->adb_interrupt_byte != 0)) add_irq(m, IRQ_ADB_DATA);
}

static void adb_send_bytes(apple2gs *m, int n, const uint8_t *bytes)
{
    m->adb_state = ADB_SENDING;
    m->adb_data_pending = n;
    for (int i = 0; i < n; i++) m->adb_data[i] = bytes[i];
    if (m->c027 & C027_DATA_INT) add_irq(m, IRQ_ADB_DATA);
}

static void adb_response_packet(apple2gs *m, int n, uint32_t value)
{
    m->adb_state = ADB_IDLE;
    m->adb_data_pending = n;
    for (int i = 0; i < 4; i++) m->adb_data[i] = (uint8_t)(value >> (8 * i));
    m->adb_interrupt_byte |= (uint8_t)(n ? 0x80 + n - 1 : 0x80);
    if (m->c027 & C027_DATA_INT) add_irq(m, IRQ_ADB_DATA);
}

static void adb_kbd_talk_reg0(apple2gs *m)
{
    int num_bytes = 0, num = 0;
    uint32_t val0 = m->kbd_reg0_n > 0 ? m->kbd_reg0[0] : 0;
    uint32_t val1 = m->kbd_reg0_n > 1 ? m->kbd_reg0[1] : 0xff;
    if (m->kbd_reg0_n > 0) {
        num_bytes = 2;
        num = 1;
        if ((val0 & 0x7f) == 0x7f) {
            val1 = val0;
        } else if (m->kbd_reg0_n > 1) {
            num = 2;
            if ((val1 & 0x7f) == 0x7f) {
                num = 1;
                val1 = 0xff;
            }
        } else {
            val1 = 0xff;
        }
    }
    memmove(m->kbd_reg0, m->kbd_reg0 + num, (size_t)(m->kbd_reg0_n - num));
    m->kbd_reg0_n -= num;
    adb_response_packet(m, num_bytes, (val0 << 8) | val1);
    if (m->kbd_reg0_n == 0) {
        m->adb_interrupt_byte &= (uint8_t)~0x08;
        remove_irq(m, IRQ_ADB_SRQ);
    }
}

static uint8_t adb_read_c026(apple2gs *m)
{
    uint8_t ret = 0;
    switch (m->adb_state) {
    case ADB_IDLE:
        ret = m->adb_interrupt_byte;
        m->adb_interrupt_byte = 0;
        if (m->irq_pending & IRQ_ADB_SRQ) m->adb_interrupt_byte |= 0x08;
        if (m->adb_data_pending == 0) remove_irq(m, IRQ_ADB_DATA);
        else m->adb_state = ADB_SENDING;
        break;
    case ADB_INCMD:
        ret = 0;
        break;
    case ADB_SENDING:
        ret = m->adb_data[0];
        for (int i = 1; i < m->adb_data_pending; i++) m->adb_data[i - 1] = m->adb_data[i];
        m->adb_data_pending--;
        if (m->adb_data_pending <= 0) {
            m->adb_data_pending = 0;
            m->adb_state = ADB_IDLE;
            remove_irq(m, IRQ_ADB_DATA);
        }
        break;
    }
    return ret;
}

static void adb_key_update(apple2gs *m, int a2code, bool is_up);

static void adb_do_cmd(apple2gs *m)
{
    m->adb_state = ADB_IDLE;
    const int dev = m->adb_cmd & 0xf;
    switch (m->adb_cmd) {
    case 0x04: m->adb_mode = (uint8_t)(m->adb_mode | m->adb_cmd_data[0]); break;
    case 0x05: m->adb_mode = (uint8_t)(m->adb_mode & ~m->adb_cmd_data[0]); break;
    case 0x06: break;
    case 0x07: m->adb_mode = m->adb_cmd_data[0]; break;
    case 0x08: m->adb_memory[m->adb_cmd_data[0]] = m->adb_cmd_data[1]; break;
    case 0x09: {
        const uint32_t addr = (uint32_t)(m->adb_cmd_data[1] << 8) | m->adb_cmd_data[0];
        uint8_t v = 0;
        if (addr < 0x100) {
            v = m->adb_memory[addr];
            if (addr == 0x0c) v = m->c025 & 0xc7;
        } else if (addr == 0x1400) {
            v = 0x72;           // the ROM's self-test checksum
        } else if (addr == 0x1401) {
            v = 0xf7;
        }
        adb_send_bytes(m, 1, &v);
        break;
    }
    case 0x11: adb_key_update(m, m->adb_cmd_data[0] & 0x7f, (m->adb_cmd_data[0] >> 7) != 0); break;
    default:
        if (m->adb_cmd >= 0xb0 && m->adb_cmd <= 0xbf && dev == m->kbd_dev_addr) {
            if (m->adb_cmd_data[1] == 0xfe) m->kbd_dev_addr = m->adb_cmd_data[0] & 0xf;
            m->kbd_reg3 = (uint16_t)(((m->adb_cmd_data[0] & 0xf) << 12) | (m->kbd_reg3 & 0x0fff));
        }
        break;
    }
}

static void adb_write_c026(apple2gs *m, uint8_t val)
{
    if (m->adb_state == ADB_INCMD) {
        m->adb_cmd_data[m->adb_cmd_so_far++ & 15] = val;
        if (m->adb_cmd_so_far >= m->adb_cmd_len) adb_do_cmd(m);
        return;
    }
    if (m->adb_state == ADB_SENDING) {
        // a new command drops what was pending
        m->adb_data_pending = 0;
        m->adb_state = ADB_IDLE;
    }
    m->adb_cmd = val;
    m->adb_cmd_so_far = 0;
    m->adb_cmd_len = 0;
    const int dev = val & 0xf;
#define IN_CMD(len) (m->adb_state = ADB_INCMD, m->adb_cmd_len = (len))
    switch (val) {
    case 0x01: case 0x03: case 0x73: break;            // abort, flush, mouse SRQ off
    case 0x04: case 0x05: IN_CMD(1); break;            // set / clear modes
    case 0x06: IN_CMD(3); break;                       // configuration
    case 0x07: IN_CMD(8); break;                       // sync (ROM 03: 8 bytes)
    case 0x08: case 0x09: IN_CMD(2); break;            // write / read memory
    case 0x0a: { uint8_t b = m->adb_mode; adb_send_bytes(m, 1, &b); break; }
    case 0x0b: {
        uint8_t b[4] = {0x82, (uint8_t)((m->mouse_dev_addr << 4) | m->kbd_dev_addr), 0x00, 0x23};
        adb_send_bytes(m, 4, b);
        break;
    }
    case 0x0d: { uint8_t b = 6; adb_send_bytes(m, 1, &b); break; }   // version (ROM 03)
    case 0x0e: { uint8_t b[2] = {0x08, 0x00}; adb_send_bytes(m, 2, b); break; }
    case 0x0f: { uint8_t b[2] = {0x0a, 0x00}; adb_send_bytes(m, 2, b); break; }
    case 0x10: warm_reset(m); break;
    case 0x11: IN_CMD(1); break;
    case 0x12: case 0x13: IN_CMD(2); break;
    default:
        if (val >= 0xb0 && val <= 0xbf) IN_CMD(2);     // listen, register 3
        else if (val >= 0xc0 && val <= 0xcf) {
            if (dev == m->kbd_dev_addr) adb_kbd_talk_reg0(m);
        } else if (val >= 0xf0) {
            if (dev == m->kbd_dev_addr) adb_response_packet(m, 2, m->kbd_reg3);
        }
        break;
    }
#undef IN_CMD
}

static uint8_t adb_read_c027(apple2gs *m)
{
    uint8_t ret = (uint8_t)(m->c027 & C027_NEG_MASK);
    if (m->mouse_valid) ret |= C027_MOUSE_DATA;
    if (m->adb_interrupt_byte != 0) ret |= C027_DATA_VALID;
    else if (m->adb_data_pending > 0 && m->adb_state != ADB_INCMD) ret |= C027_DATA_VALID;
    if (m->mouse_coord) ret |= C027_MOUSE_COORD;
    return ret;
}

static void adb_write_c027(apple2gs *m, uint8_t val)
{
    const uint8_t old = m->c027;
    m->c027 = (uint8_t)(val & C027_NEG_MASK);
    if (!(m->c027 & C027_MOUSE_INT) && (old & C027_MOUSE_INT)) remove_irq(m, IRQ_ADB_MOUSE);
    if (!(m->c027 & C027_DATA_INT) && (old & C027_DATA_INT)) remove_irq(m, IRQ_ADB_DATA);
    if ((m->c027 & C027_MOUSE_INT) && m->mouse_valid) add_irq(m, IRQ_ADB_MOUSE);
    update_adb_data_irq(m);
}

static void adb_key_event(apple2gs *m, int a2code, bool is_up)
{
    const key_ascii *k = &k_ascii[a2code];
    const bool hard_key = k->normal >= 0 && !(k->normal & 0xef00);
    const bool shift = m->c025 & 0x01, ctrl = m->c025 & 0x02, caps = m->c025 & 0x04;
    int ascii = k->normal;
    if (caps && ascii >= 'a' && ascii <= 'z') {
        ascii = k->shifted;
        if (shift && (m->adb_mode & 0x40)) ascii = k->normal;
    } else if (shift) {
        ascii = k->shifted;
    }
    if (ctrl && k->ctrl >= 0) ascii = k->ctrl;
    if (!is_up && a2code == 0x35 && ctrl && (m->c025 & 0x80)) {
        // Control-Command-Escape: the desk manager
        m->adb_interrupt_byte |= 0x20;
        if (m->c027 & C027_DATA_INT) add_irq(m, IRQ_ADB_DATA);
    }
    const uint8_t special = ascii >= 0 ? (uint8_t)((ascii >> 8) & 0xff) : 0;
    const uint8_t key = (uint8_t)(((ascii < 0 ? 0 : ascii) & 0x7f) | 0x80);
    if (!is_up) {
        if (hard_key) {
            m->kbd_buf[m->kbd_chars_buffered] = key;
            if (m->kbd_chars_buffered < 7) m->kbd_chars_buffered++;
            m->key_down = true;
            m->a2code_down = a2code;
            m->repeat_frame = m->frame_count + (uint64_t)m->repeat_delay;
            m->hard_key_down = true;
        }
        m->c025 = (uint8_t)(m->c025 | special);
    } else {
        if (hard_key && a2code == m->a2code_down) {
            m->hard_key_down = false;
            m->key_down = false;
        }
        m->c025 = (uint8_t)(m->c025 & ~special);
    }
    if (m->key_down) m->c025 &= (uint8_t)~0x20;
    else m->c025 |= 0x20;
}

static void adb_key_update(apple2gs *m, int a2code, bool is_up)
{
    if (a2code < 0 || a2code > 0x7f) return;
    if (!is_up && a2code == 0x7f && (m->c025 & 0x02)) {    // Control-Reset
        warm_reset(m);
        return;
    }
    const bool autopoll = !(m->adb_mode & 1) && m->kbd_dev_addr == 2;
    if (!autopoll) {
        if (m->kbd_reg0_n < (int)sizeof m->kbd_reg0) m->kbd_reg0[m->kbd_reg0_n++] = (uint8_t)(a2code | (is_up ? 0x80 : 0));
        if (m->kbd_reg3 & 0x200) {
            m->adb_interrupt_byte |= 0x08;
            add_irq(m, IRQ_ADB_SRQ);
        }
        return;
    }
    const int i = (a2code >> 5) & 3;
    const uint32_t mask = 1u << (a2code & 0x1f);
    if (is_up) {
        if (!(m->virtual_key_up[i] & mask)) {
            m->virtual_key_up[i] |= mask;
            adb_key_event(m, a2code, true);
        }
    } else if (m->virtual_key_up[i] & mask) {
        m->virtual_key_up[i] &= ~mask;
        adb_key_event(m, a2code, false);
    }
}

static uint8_t adb_access_c010(apple2gs *m)
{
    m->kbd_read_no_update = 0;
    uint8_t tmp = (uint8_t)(m->kbd_buf[0] & 0x7f);
    m->kbd_buf[0] = tmp;
    tmp = (uint8_t)(tmp | (m->hard_key_down ? 0x80 : 0));
    if (m->kbd_chars_buffered) {
        for (int i = 1; i < m->kbd_chars_buffered; i++) m->kbd_buf[i - 1] = m->kbd_buf[i];
        m->kbd_chars_buffered--;
    }
    m->c025 &= (uint8_t)~0x08;
    return tmp;
}

static uint8_t adb_read_c000(apple2gs *m)
{
    if (!(m->kbd_buf[0] & 0x80) && !m->key_down) return m->kbd_buf[0];
    if (m->kbd_buf[0] & 0x80) {
        if (m->kbd_read_no_update++ > 5 && m->kbd_chars_buffered > 1) adb_access_c010(m);
    } else if (m->key_down && m->frame_count >= m->repeat_frame) {
        m->c025 |= 0x08;
        adb_key_event(m, m->a2code_down, false);
        m->repeat_frame = m->frame_count + (uint64_t)m->repeat_rate;
    }
    return m->kbd_buf[0];
}

static uint8_t mouse_read_c024(apple2gs *m)
{
    if ((m->adb_mode & 0x02) || m->mouse_dev_addr != 3) {
        m->mouse_valid = false;
        remove_irq(m, IRQ_ADB_MOUSE);
        return 0;
    }
    int dx = m->mouse_target_x - m->mouse_a2_x, dy = m->mouse_target_y - m->mouse_a2_y;
    dx = dx < -0x3f ? -0x3f : dx > 0x3f ? 0x3f : dx;
    dy = dy < -0x3f ? -0x3f : dy > 0x3f ? 0x3f : dy;
    // With the Event Manager running its cursor is kept on ours, so the
    // IIGS pointer follows the host's exactly
    const uint32_t tools = (uint32_t)m->slow_ram[0x103c8] | ((uint32_t)m->slow_ram[0x103c9] << 8) |
                           ((uint32_t)m->slow_ram[0x103ca] << 16);
    bool em_active = false;
    if (tools >= 0x20000 && tools + 28 < ((uint32_t)m->ram_banks << 16))
        em_active = (m->fast_ram[tools + 24] | m->fast_ram[tools + 25]) != 0;
    if (em_active) {
        if (!m->mouse_coord) {
            m->slow_ram[0x47c] = m->fast_ram[0x47c] = (uint8_t)m->mouse_a2_x;
            m->slow_ram[0x57c] = m->fast_ram[0x57c] = (uint8_t)(m->mouse_a2_x >> 8);
            m->slow_ram[0x10190] = (uint8_t)m->mouse_a2_x;
            m->slow_ram[0x10192] = (uint8_t)(m->mouse_a2_x >> 8);
        } else {
            m->slow_ram[0x4fc] = m->fast_ram[0x4fc] = (uint8_t)m->mouse_a2_y;
            m->slow_ram[0x5fc] = m->fast_ram[0x5fc] = (uint8_t)(m->mouse_a2_y >> 8);
            m->slow_ram[0x10191] = (uint8_t)m->mouse_a2_y;
            m->slow_ram[0x10193] = (uint8_t)(m->mouse_a2_y >> 8);
        }
    }
    uint8_t ret;
    if (m->mouse_coord) {
        m->mouse_a2_y += dy;
        m->mouse_a2_button = m->mouse_button;
        ret = (uint8_t)((!m->mouse_button ? 0x80 : 0) | (dy & 0x7f));
    } else {
        m->mouse_a2_x += dx;
        ret = (uint8_t)((!m->mouse_button ? 0x80 : 0) | (dx & 0x7f));
    }
    if (m->mouse_coord && m->mouse_a2_x == m->mouse_target_x && m->mouse_a2_y == m->mouse_target_y &&
        m->mouse_a2_button == m->mouse_button) {
        m->mouse_valid = false;
        remove_irq(m, IRQ_ADB_MOUSE);
    }
    m->mouse_coord = !m->mouse_coord;
    return ret;
}

// ---------------------------------------------------------------------------
// The IWM, minimal: the 3.5" status line says "no drive / no disk" and the
// data register gives no nibbles, so the firmware sees empty ports
static uint8_t iwm_access(apple2gs *m, uint16_t offset, bool write, uint8_t value)
{
    const int sw = offset >> 1;
    if (offset & 1) m->iwm_state |= (uint8_t)(1 << sw);
    else m->iwm_state &= (uint8_t)~(1 << sw);
    const bool motor = m->iwm_state & 0x10, q6 = m->iwm_state & 0x40, q7 = m->iwm_state & 0x80;
    if (write) {
        if (q6 && q7 && (offset & 1) && !motor) m->iwm_mode = value & 0x1f;
        return 0;
    }
    if (offset & 1) return 0;
    if (!q7 && !q6) return motor ? 0x00 : 0xff;
    if (!q7 && q6) return (uint8_t)(0x80 | (motor ? 0x20 : 0) | (m->iwm_mode & 0x1f));
    if (q7 && !q6) return 0x80;
    return 0;
}

// ---------------------------------------------------------------------------
// I/O ($C000-$CFFF)
static void lc_switch(apple2gs *m, uint8_t loc, bool write)
{
    const uint32_t lcbank2 = (loc & 0x08) ? 0 : LCBANK2;
    const uint32_t rdrom = (((loc >> 1) ^ loc) & 1) ? RDROM : 0;
    uint32_t prewrite = 0, wrdefram;
    if (!write) {
        prewrite = (loc & 1) ? PREWRITE : 0;
        wrdefram = m->statereg & WRDEFRAM;
        if ((loc & 1) == 0) wrdefram = 0;
        else if (m->statereg & PREWRITE) wrdefram = WRDEFRAM;
    } else {
        wrdefram = (loc & 1) ? (m->statereg & WRDEFRAM) : 0;
    }
    m->statereg = (m->statereg & ~(PREWRITE | WRDEFRAM | RDROM | LCBANK2)) | lcbank2 | rdrom | prewrite | wrdefram;
    rebuild_map(m);
}

static uint8_t io_read(apple2gs *m, uint16_t address)
{
    const uint8_t loc = (uint8_t)address;
#define IOR(b) ((b) ? 0x80 : 0x00)
    if (address >= 0xc100) {
        const int page = (address >> 8) & 0xf;
        const bool intcx = m->statereg & INTCX;
        if (page == 3) {
            if (!(m->slotromsel & 0x08) && !(m->statereg & INTC8)) m->statereg |= INTC8;
            if (!(m->slotromsel & 0x08) || intcx) return romff(m, address);
            return 0;
        }
        if (page < 8) {
            if (!(m->slotromsel & (1 << page)) || intcx) return romff(m, address);
            if (page == 7) return m->slot7_rom[loc];
            return 0;
        }
        uint8_t v = 0;
        if (intcx || (m->statereg & INTC8)) v = romff(m, address);
        if (address == 0xcfff) m->statereg &= ~INTC8;
        return v;
    }
    if (loc < 0x10) return adb_read_c000(m);
    switch (loc) {
    case 0x10: return adb_access_c010(m);
    case 0x11: return IOR(m->statereg & LCBANK2);
    case 0x12: return IOR(!(m->statereg & RDROM));
    case 0x13: return IOR(m->statereg & RAMRD);
    case 0x14: return IOR(m->statereg & RAMWRT);
    case 0x15: return IOR(m->statereg & INTCX);
    case 0x16: return IOR(m->statereg & ALTZP);
    case 0x17: return IOR(m->slotromsel & 0x08);
    case 0x18: return IOR(m->st80);
    case 0x19: return IOR(in_vbl(m));
    case 0x1a: return IOR(m->text);
    case 0x1b: return IOR(m->mixed);
    case 0x1c: return IOR(m->statereg & PAGE2);
    case 0x1d: return IOR(m->hires);
    case 0x1e: return IOR(m->altchar);
    case 0x1f: return IOR(m->vid80);
    case 0x20: return 0;
    case 0x21: return IOR(m->mono);
    case 0x22: return m->text_color;
    case 0x23: return m->c023;
    case 0x24: return mouse_read_c024(m);
    case 0x25: return m->c025;
    case 0x26: return adb_read_c026(m);
    case 0x27: return adb_read_c027(m);
    case 0x28: return 0;
    case 0x29: return m->newvideo;
    case 0x2a: return 0;
    case 0x2b: return m->c02b;
    case 0x2c: return 0;
    case 0x2d: return m->slotromsel;
    case 0x2e: return read_vid_counter(m, true);
    case 0x2f: return read_vid_counter(m, false);
    case 0x30: m->speaker = !m->speaker; return 0;
    case 0x31: return m->c031;
    case 0x32: return 0;
    case 0x33: return m->c033;
    case 0x34: return m->c034;
    case 0x35: return m->shadow;
    case 0x36: return m->speed;
    case 0x37: return 0;
    case 0x38: case 0x39: {              // the SCC's command registers (B, A)
        const int ch = loc == 0x38 ? 1 : 0;
        const int reg = m->scc_ptr[ch];
        m->scc_ptr[ch] = 0;
        switch (reg) {
        case 0: return 0x2c;            // Tx empty, DCD, CTS
        case 1: return 0x01;            // all sent
        case 2: return ch == 1 ? 0x06 : m->scc_wr2;
        default: return 0x00;
        }
    }
    case 0x3a: case 0x3b: return 0;
    case 0x3c: return m->doc_ctl;
    case 0x3d: {
        const uint8_t ret = m->doc_saved;
        if (m->doc_ctl & 0x40) m->doc_saved = m->doc_ram[m->doc_ptr];
        else m->doc_saved = es5503_read(&m->doc, (uint8_t)m->doc_ptr);
        if (m->doc_ctl & 0x20) m->doc_ptr = (uint16_t)(m->doc_ptr + 1);
        return ret;
    }
    case 0x3e: return (uint8_t)m->doc_ptr;
    case 0x3f: return (uint8_t)(m->doc_ptr >> 8);
    case 0x41: return m->c041;
    case 0x46: {
        const uint8_t tmp = m->c046;
        m->c046 = (uint8_t)((tmp & 0xbf) | ((tmp & 0x80) >> 1));
        return tmp;
    }
    case 0x47:
        remove_irq(m, IRQ_VBL | IRQ_QTRSEC);
        m->c046 &= 0xe7;
        return 0;
    case 0x50: m->text = false; return 0;
    case 0x51: m->text = true; return 0;
    case 0x52: m->mixed = false; return 0;
    case 0x53: m->mixed = true; return 0;
    case 0x54: m->statereg &= ~PAGE2; rebuild_map(m); return 0;
    case 0x55: m->statereg |= PAGE2; rebuild_map(m); return 0;
    case 0x56: m->hires = false; rebuild_map(m); return 0;
    case 0x57: m->hires = true; rebuild_map(m); return 0;
    case 0x5e: m->an3 = false; return 0;
    case 0x5f: m->an3 = true; return 0;
    case 0x60: return 0;
    case 0x61: return IOR(m->c025 & 0x80);    // Open Apple (Command)
    case 0x62: return IOR(m->c025 & 0x40);    // Solid Apple (Option)
    case 0x63: return 0;
    case 0x64: case 0x65: case 0x66: case 0x67: return 0;   // no joystick
    case 0x68: return (uint8_t)(m->statereg & 0xff);
    case 0x70: return 0;
    default: break;
    }
#undef IOR
    if (loc >= 0x71 && loc <= 0x7f) return romff(m, 0xc000 + loc);
    if (loc >= 0x80 && loc <= 0x8f) {
        lc_switch(m, loc, false);
        return 0;
    }
    if (loc >= 0xe0 && loc <= 0xef) return iwm_access(m, loc & 0xf, false, 0);
    return 0;
}

static void io_write(apple2gs *m, uint16_t address, uint8_t value)
{
    const uint8_t loc = (uint8_t)address;
    if (address >= 0xc100) {
        const int page = (address >> 8) & 0xf;
        if (page == 3 && !(m->slotromsel & 0x08)) m->statereg |= INTC8;
        if (address == 0xcfff) m->statereg &= ~INTC8;
        return;
    }
    switch (loc) {
    case 0x00: m->st80 = false; rebuild_map(m); return;
    case 0x01: m->st80 = true; rebuild_map(m); return;
    case 0x02: m->statereg &= ~RAMRD; rebuild_map(m); return;
    case 0x03: m->statereg |= RAMRD; rebuild_map(m); return;
    case 0x04: m->statereg &= ~RAMWRT; rebuild_map(m); return;
    case 0x05: m->statereg |= RAMWRT; rebuild_map(m); return;
    case 0x06: m->statereg &= ~INTCX; return;
    case 0x07: m->statereg |= INTCX; return;
    case 0x08: m->statereg &= ~ALTZP; rebuild_map(m); return;
    case 0x09: m->statereg |= ALTZP; rebuild_map(m); return;
    case 0x0a: m->slotromsel &= (uint8_t)~0x08; return;
    case 0x0b: m->slotromsel |= 0x08; return;
    case 0x0c: m->vid80 = false; return;
    case 0x0d: m->vid80 = true; return;
    case 0x0e: m->altchar = false; return;
    case 0x0f: m->altchar = true; return;
    case 0x21: m->mono = (value & 0x80) != 0; return;
    case 0x22: m->text_color = value; return;
    case 0x23: {
        uint8_t tmp = (uint8_t)((m->c023 & 0x70) | (value & 0x0f));
        if ((tmp & 0x22) == 0x22) m->irq_pending |= IRQ_SCAN;
        if (!(tmp & 0x02)) m->irq_pending &= ~IRQ_SCAN;
        if ((tmp & 0x44) == 0x44) m->irq_pending |= IRQ_1SEC;
        if (!(tmp & 0x04)) m->irq_pending &= ~IRQ_1SEC;
        if (m->irq_pending & (IRQ_SCAN | IRQ_1SEC)) tmp |= 0x80;
        m->c023 = tmp;
        update_irq(m);
        return;
    }
    case 0x26: adb_write_c026(m, value); return;
    case 0x27: adb_write_c027(m, value); return;
    case 0x29: m->newvideo = value; return;
    case 0x2b: m->c02b = value; return;
    case 0x2d: m->slotromsel = value; return;
    case 0x30: m->speaker = !m->speaker; return;
    case 0x31: m->c031 = (uint8_t)(value & 0xc0); return;
    case 0x32: {
        uint8_t tmp = (uint8_t)(m->c023 & 0x7f);
        if (!(value & 0x40) && (tmp & 0x40)) {
            m->irq_pending &= ~IRQ_1SEC;
            tmp &= 0xbf;
        }
        if (!(value & 0x20) && (tmp & 0x20)) {
            m->irq_pending &= ~IRQ_SCAN;
            tmp &= 0xdf;
        }
        if (m->irq_pending & (IRQ_1SEC | IRQ_SCAN)) tmp |= 0x80;
        m->c023 = tmp;
        update_irq(m);
        return;
    }
    case 0x33: m->c033 = value; return;
    case 0x34:
        m->border = value & 0x0f;
        clock_write_c034(m, value);
        return;
    case 0x35:
        if (m->shadow != value) {
            m->shadow = value;
            rebuild_map(m);
        }
        return;
    case 0x36: m->speed = (uint8_t)(value & ~0x20); return;
    case 0x38: case 0x39: {
        const int ch = loc == 0x38 ? 1 : 0;
        if (m->scc_ptr[ch] == 0) {
            m->scc_ptr[ch] = (uint8_t)((value & 0x07) | ((value & 0x38) == 0x08 ? 0x08 : 0));
        } else {
            if (m->scc_ptr[ch] == 2) m->scc_wr2 = value;
            m->scc_ptr[ch] = 0;
        }
        return;
    }
    case 0x3c: m->doc_ctl = value; return;
    case 0x3d:
        if (m->doc_ctl & 0x40) m->doc_ram[m->doc_ptr] = value;
        else es5503_write(&m->doc, (uint8_t)m->doc_ptr, value);
        if (m->doc_ctl & 0x20) m->doc_ptr = (uint16_t)(m->doc_ptr + 1);
        return;
    case 0x3e: m->doc_ptr = (uint16_t)((m->doc_ptr & 0xff00) | value); return;
    case 0x3f: m->doc_ptr = (uint16_t)((m->doc_ptr & 0x00ff) | (value << 8)); return;
    case 0x41:
        m->c041 = value & 0x1f;
        if (!(value & 0x08)) remove_irq(m, IRQ_VBL);
        if (!(value & 0x10)) remove_irq(m, IRQ_QTRSEC);
        return;
    case 0x47:
        remove_irq(m, IRQ_VBL | IRQ_QTRSEC);
        m->c046 &= 0xe7;
        return;
    case 0x50: m->text = false; return;
    case 0x51: m->text = true; return;
    case 0x52: m->mixed = false; return;
    case 0x53: m->mixed = true; return;
    case 0x54: m->statereg &= ~PAGE2; rebuild_map(m); return;
    case 0x55: m->statereg |= PAGE2; rebuild_map(m); return;
    case 0x56: m->hires = false; rebuild_map(m); return;
    case 0x57: m->hires = true; rebuild_map(m); return;
    case 0x5e: m->an3 = false; return;
    case 0x5f: m->an3 = true; return;
    case 0x68:
        m->statereg = (m->statereg & ~0xffu) | value;
        rebuild_map(m);
        return;
    default: break;
    }
    if (loc >= 0x10 && loc <= 0x1f) {
        adb_access_c010(m);
        return;
    }
    if (loc >= 0x80 && loc <= 0x8f) {
        lc_switch(m, loc, true);
        return;
    }
    if (loc >= 0xe0 && loc <= 0xef) iwm_access(m, loc & 0xf, true, value);
}

// ---------------------------------------------------------------------------
// SmartPort (slot 7, high level)
static bool disk_block(apple2gs *m, int unit, uint32_t block, uint8_t *buf, bool write)
{
    gsdisk *d = &m->disk[unit];
    m->disk_busy = 8;
    if (!d->dos_order) {
        const uint32_t off = d->offset + block * 512;
        return write ? (d->f.write && d->f.write(d->f.ctx, off, buf, 512)) : d->f.read(d->f.ctx, off, buf, 512);
    }
    // a DOS-order 140K image: the two sectors of the block, by their physical
    // sector (DOS 3.3 and ProDOS number them differently)
    static const int dos[16] = {0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};
    static const int pro[16] = {0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};
    const uint32_t track = block / 8;
    for (int h = 0; h < 2; h++) {
        const int pro_logical = (int)(block % 8) * 2 + h, phys = pro[pro_logical];
        int dos_logical = 0;
        for (int i = 0; i < 16; i++)
            if (dos[i] == phys) dos_logical = i;
        const uint32_t off = d->offset + (track * 16 + (uint32_t)dos_logical) * 256;
        const bool ok = write ? (d->f.write && d->f.write(d->f.ctx, off, buf + h * 256, 256))
                              : d->f.read(d->f.ctx, off, buf + h * 256, 256);
        if (!ok) return false;
    }
    return true;
}

static int nunits(const apple2gs *m)
{
    int n = 0;
    while (n < A2GS_DISKS && m->disk[n].in) n++;
    return n;
}

static int disk_read(apple2gs *m, int unit, uint32_t buf, uint32_t block)
{
    if (unit < 0 || unit >= nunits(m)) return 0x28;         // no device
    if (block >= m->disk[unit].blocks) return 0x2d;         // bad block
    uint8_t b[512];
    if (!disk_block(m, unit, block, b, false)) return 0x27;  // I/O error
    for (uint32_t i = 0; i < 512; i++) mem_write(m, (buf + i) & 0xffffff, b[i]);
    return 0;
}

static int disk_write(apple2gs *m, int unit, uint32_t buf, uint32_t block)
{
    if (unit < 0 || unit >= nunits(m)) return 0x28;
    gsdisk *d = &m->disk[unit];
    if (d->write_protect) return 0x2b;
    if (block >= d->blocks) return 0x2d;
    uint8_t b[512];
    for (uint32_t i = 0; i < 512; i++) b[i] = mem_read(m, (buf + i) & 0xffffff);
    return disk_block(m, unit, block, b, true) ? 0 : 0x27;
}

static inline void set_pc(w65c816 *c, uint32_t v)
{
    c->pbr = (uint8_t)(v >> 16);
    c->pc_ = (uint16_t)v;
}

static void smartport_boot(apple2gs *m)
{
    const int ret = disk_read(m, 0, 0x800, 0);
    mem_write(m, 0x7f8, 0xc7);
    mem_write(m, 0x42, 0x01);           // ProDOS driver parameters: READ, unit $70
    mem_write(m, 0x43, 0x70);
    mem_write(m, 0x44, 0x00);
    mem_write(m, 0x45, 0x08);
    mem_write(m, 0x46, 0x00);
    mem_write(m, 0x47, 0x00);
    m->cpu.x = 0x70;
    if (ret == 0 && mem_read(m, 0x801) != 0) set_pc(&m->cpu, 0x000801);
    else set_pc(&m->cpu, 0x00c600);     // nothing to boot here: the ROM's scan goes on with slot 6
}

static void smartport_prodos(apple2gs *m)
{
    mem_write(m, 0x7f8, 0xc7);
    const uint16_t dp = m->cpu.d;
#define DPB(o) mem_read(m, (uint16_t)(dp + (o)))
    const uint8_t cmd = DPB(0x42), unit_num = DPB(0x43);
    const uint32_t buf = (uint32_t)(DPB(0x44) | (DPB(0x45) << 8));
    const uint32_t blk = (uint32_t)(DPB(0x46) | (DPB(0x47) << 8));
#undef DPB
    int unit = unit_num >> 7;
    if ((unit_num & 0x70) != 0x70) unit += 2;
    int ret;
    switch (cmd) {
    case 0x00:
        if (unit < nunits(m)) {
            const uint32_t blocks = m->disk[unit].blocks;
            m->cpu.x = (uint16_t)(blocks & 0xff);
            m->cpu.y = (uint16_t)((blocks >> 8) & 0xff);
            ret = 0;
        } else {
            ret = 0x28;
        }
        break;
    case 0x01: ret = disk_read(m, unit, buf, blk); break;
    case 0x02: ret = disk_write(m, unit, buf, blk); break;
    case 0x03: ret = unit < nunits(m) ? 0 : 0x28; break;
    default: ret = 0x01; break;
    }
    m->cpu.a = (uint16_t)((m->cpu.a & 0xff00) | (uint8_t)ret);
    m->cpu.p.c = ret != 0;
    m->cpu.p.z = ret == 0;
}

static void smartport_call(apple2gs *m)
{
    w65c816 *c = &m->cpu;
    mem_write(m, 0x7f8, 0xc7);
    // the JSR's return address points at the inline command block
    uint8_t lo, hi;
    if (c->e_) {
        c->sp = (uint16_t)(0x100 | (uint8_t)(c->sp + 1));
        lo = mem_read(m, c->sp);
        c->sp = (uint16_t)(0x100 | (uint8_t)(c->sp + 1));
        hi = mem_read(m, c->sp);
    } else {
        c->sp = (uint16_t)(c->sp + 1);
        lo = mem_read(m, c->sp);
        c->sp = (uint16_t)(c->sp + 1);
        hi = mem_read(m, c->sp);
    }
    const uint32_t bank = (uint32_t)c->pbr << 16;
    const uint16_t rts = (uint16_t)((lo | (hi << 8)) + 1);
#define RB(a) mem_read(m, (a) & 0xffffff)
    const uint8_t cmd = RB(bank | rts);
    const bool ext = (cmd & 0x40) != 0;
    uint32_t list = (uint32_t)(RB(bank | (uint16_t)(rts + 1)) | (RB(bank | (uint16_t)(rts + 2)) << 8));
    if (ext) list |= (uint32_t)RB(bank | (uint16_t)(rts + 3)) << 16;
    else list |= bank;
#define LB(o) RB(list + (o))
    const uint8_t param_cnt = LB(0), unit = LB(1);
    int ret = 0;
    const int n = nunits(m);
    switch (cmd & 0x3f) {
    case 0x00: {                        // STATUS
        if (param_cnt != 3) { ret = 0x04; break; }
        uint32_t sp = (uint32_t)(LB(2) | (LB(3) << 8));
        const uint8_t code = ext ? LB(6) : LB(4);
        if (ext) sp |= (uint32_t)LB(4) << 16;
        else sp |= bank;
#define WB(o, v) mem_write(m, (sp + (o)) & 0xffffff, (uint8_t)(v))
        if (unit == 0 && code == 0) {
            WB(0, n);
            WB(1, 0xff);                // no interrupts
            WB(2, 0x4b); WB(3, 0x00);   // vendor
            WB(4, 0x00); WB(5, 0x10);   // version
            WB(6, 0x00); WB(7, 0x00);
            c->x = 8;
            c->y = 0;
        } else if (unit > 0 && (code == 0 || code == 3)) {
            uint8_t stat = 0x80;
            uint32_t blocks = 0;
            if (unit <= n) {
                stat = 0xf8;
                blocks = m->disk[unit - 1].blocks;
                if (m->disk[unit - 1].write_protect) stat |= 0x04;
            }
            WB(0, stat);
            WB(1, blocks);
            WB(2, blocks >> 8);
            WB(3, blocks >> 16);
            uint32_t o = 4;
            if (ext) { WB(o, blocks >> 24); o++; }
            if (code == 0) {
                c->x = (uint16_t)o;
            } else {
                static const char name[] = "CMESYS DISK     ";
                WB(o, 12);
                for (int i = 0; i < 16; i++) WB(o + 1 + (uint32_t)i, name[i]);
                WB(o + 17, 0x02);       // a hard disk
                WB(o + 18, 0xc0);       // extended, removable
                WB(o + 19, 0x00);
                WB(o + 20, 0x00);       // firmware version
                c->x = (uint16_t)(o + 21);
                if (unit > n) ret = 0x28;
            }
            c->y = 0;
        } else {
            ret = 0x21;                 // bad status code
        }
#undef WB
        break;
    }
    case 0x01:                          // READ BLOCK
    case 0x02: {                        // WRITE BLOCK
        if (param_cnt != 3) { ret = 0x04; break; }
        uint32_t buf = (uint32_t)(LB(2) | (LB(3) << 8)), o = 4;
        if (ext) {
            buf |= (uint32_t)LB(4) << 16;
            o = 6;
        } else {
            buf |= bank;
        }
        uint32_t block = (uint32_t)(LB(o) | (LB(o + 1) << 8) | (LB(o + 2) << 16));
        if (ext) block |= (uint32_t)LB(o + 3) << 24;
        if (unit < 1 || unit > n) { ret = 0x28; break; }
        ret = (cmd & 0x3f) == 1 ? disk_read(m, unit - 1, buf, block) : disk_write(m, unit - 1, buf, block);
        c->x = 0;
        c->y = 2;
        break;
    }
    case 0x03: ret = (unit >= 1 && unit <= n) ? 0 : 0x28; break;   // FORMAT
    case 0x04: ret = 0; break;          // CONTROL
    case 0x05: ret = 0; break;          // INIT
    default: ret = 0x01; break;
    }
#undef LB
#undef RB
    c->a = (uint16_t)((c->a & 0xff00) | (uint8_t)ret);
    c->p.c = ret != 0;
    c->p.z = ret == 0;
    set_pc(c, bank | (uint16_t)(rts + 3 + (ext ? 2 : 0)));
}

static void on_wdm(void *user, uint8_t signature)
{
    apple2gs *m = (apple2gs *)user;
    if (signature != 0xc7) return;
    if (m->cpu.p.v) smartport_boot(m);
    else if (m->cpu.p.c) smartport_call(m);
    else smartport_prodos(m);
}

// ---------------------------------------------------------------------------
// Sound: the DOC at 894.886 kHz / (oscillators + 2), and the speaker
static void sound_advance(apple2gs *m, int ticks)
{
    // a DOC sample takes 16 * (oscillators + 2) ticks exactly
    m->doc_phase += (uint32_t)ticks;
    const uint32_t doc_ticks = 16u * (uint32_t)(m->doc.oscs_enabled + 2);
    while (m->doc_phase >= doc_ticks) {
        m->doc_phase -= doc_ticks;
        m->doc_last = es5503_generate(&m->doc);
    }
    if (m->turbo) return;
    m->out_acc += (uint64_t)ticks * (uint64_t)m->audio_rate;
    while (m->out_acc >= TICK_RATE) {
        m->out_acc -= TICK_RATE;
        // the DOC by the sound GLU's volume, with the speaker, through a DC blocker
        const int32_t vol = m->doc_ctl & 0x0f;
        const float in = (float)((m->doc_last * vol) >> 6) + (m->speaker ? 4000.0f : -4000.0f);
        const float out = in - m->hp_in + 0.995f * m->hp_out;
        m->hp_in = in;
        m->hp_out = out;
        int32_t s = (int32_t)out;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (m->naudio < A2GS_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)s;
    }
}

// ---------------------------------------------------------------------------
apple2gs *apple2gs_create(int ram_banks, int audio_rate)
{
    apple2gs *m = (apple2gs *)calloc(1, sizeof *m);
    if (!m) return NULL;
    if (ram_banks < 2) ram_banks = 2;
    if (ram_banks > 64) ram_banks = 64;
    m->ram_banks = ram_banks;
    m->fast_ram = (uint8_t *)calloc((size_t)ram_banks, 0x10000);
    if (!m->fast_ram) {
        free(m);
        return NULL;
    }
    m->audio_rate = audio_rate > 0 ? audio_rate : A2GS_AUDIO_RATE;
    for (int i = 0; i < 16; i++) m->palette16[i] = rgb12(iigs_colors[i]);
    w65c816_init(&m->cpu, FAST_CLOCK);
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.cycles = on_cpu_cycles;
    m->cpu.vector_handler = vector_address;
    m->cpu.wdm = on_wdm;
    m->cpu.user = m;
    es5503_init(&m->doc, MASTER_CLOCK / 4, m->doc_ram);
    m->doc.irq = doc_irq;
    m->doc.user = m;
    m->speed = 0x80;
    m->repeat_delay = 45;
    m->repeat_rate = 3;
    for (int i = 0; i < 4; i++) m->virtual_key_up[i] = 0xffffffffu;
    build_slot7_rom(m->slot7_rom);
    return m;
}

void apple2gs_destroy(apple2gs *m)
{
    if (!m) return;
    for (int u = 0; u < A2GS_DISKS; u++) apple2gs_eject(m, u);
    free(m->fast_ram);
    free(m);
}

void apple2gs_set_roms(apple2gs *m, const uint8_t *rom_fc, const uint8_t *rom_fe, const uint8_t *chr)
{
    // 341-0748's first half is bank $FF, its second $FE
    m->rom_bank[0] = rom_fc;
    m->rom_bank[1] = rom_fc + 0x10000;
    m->rom_bank[2] = rom_fe + 0x10000;
    m->rom_bank[3] = rom_fe;
    m->chr_rom = chr;
}

void apple2gs_set_time(apple2gs *m, int64_t unix_time) { m->host_time = unix_time; }
void apple2gs_set_turbo(apple2gs *m, bool on) { m->turbo = on; }

// The RESET line (Control-Reset): the soft switches, the interrupt enables
// and the chips go back to power on; the memory stays
static void warm_reset(apple2gs *m)
{
    m->shadow = 0;
    m->statereg = WRDEFRAM | RDROM | LCBANK2 | INTCX;
    m->slotromsel = 0;
    m->newvideo = 0x01;
    m->c023 = m->c041 = m->c046 = 0;
    m->c02b = 0x08;
    m->text_color = 0xf0;
    m->border = 0;
    m->c034 = 0;
    m->st80 = m->vid80 = m->altchar = m->mixed = m->hires = false;
    m->text = true;
    m->an3 = true;
    m->mono = false;
    m->irq_pending = 0;
    m->clk_mode = CLK_IDLE;
    es5503_reset(&m->doc);
    m->doc_ctl = 0;
    m->doc_ptr = 0;
    m->doc_saved = 0;
    m->iwm_state = m->iwm_mode = m->c031 = 0;
    adb_reset(m);
    rebuild_map(m);
    w65c816_set_irq(&m->cpu, IRQ_CLEAR);
    w65c816_reset(&m->cpu);
}

void apple2gs_reset(apple2gs *m)
{
    memset(m->fast_ram, 0, (size_t)m->ram_banks << 16);
    memset(m->slow_ram, 0, sizeof m->slow_ram);
    m->speed = 0x80;
    m->naudio = 0;
    update_clock_time(m);
    warm_reset(m);
}

void apple2gs_reset_key(apple2gs *m) { warm_reset(m); }

int apple2gs_run_frame(apple2gs *m, const int16_t **audio)
{
    m->naudio = 0;
    for (int line = 0; line < LINES_PER_FRAME; line++) {
        start_of_line(m, line);
        const uint64_t end = m->frame_start_tick + (uint64_t)(line + 1) * TICKS_PER_LINE;
        while (m->ticks < end) w65c816_step(&m->cpu);
    }
    m->frame_start_tick += (uint64_t)LINES_PER_FRAME * TICKS_PER_LINE;
    m->frame_count++;
    if (m->disk_busy) m->disk_busy--;
    if (audio) *audio = m->audio;
    return m->naudio;
}

// ---------------------------------------------------------------------------
// Input
void apple2gs_key(apple2gs *m, int code, bool down)
{
    if (code < 0 || code >= GSK_COUNT || m->held[code] == down) return;
    m->held[code] = down;
    adb_key_update(m, code, !down);
}

void apple2gs_keys_clear(apple2gs *m)
{
    for (int c = 0; c < GSK_COUNT; c++)
        if (m->held[c]) apple2gs_key(m, c, false);
}

void apple2gs_pointer(apple2gs *m, int x, int y, bool button)
{
    // the host's pointer is absolute; the ADB mouse reports moves towards it
    // (in 640x200 or 320x200 Super Hi-Res dots)
    x = x < 0 ? 0 : x > A2GS_W - 1 ? A2GS_W - 1 : x;
    y = y < 0 ? 0 : y > A2GS_H * 2 - 1 ? A2GS_H * 2 - 1 : y;
    m->ptr_x = x;
    m->ptr_y = y;
    const bool shr320 = (m->newvideo & 0x80) && !(m->slow_ram[0x19d00] & 0x80);
    m->mouse_target_x = shr320 ? x / 2 : x;
    m->mouse_target_y = y / 2;
    const bool moved = m->mouse_target_x != m->mouse_a2_x || m->mouse_target_y != m->mouse_a2_y;
    m->mouse_button = button;
    if ((moved || button != m->mouse_a2_button) && !(m->adb_mode & 0x02)) {
        m->mouse_valid = true;
        if (m->c027 & C027_MOUSE_INT) add_irq(m, IRQ_ADB_MOUSE);
    }
}

void apple2gs_mouse(apple2gs *m, int dx, int dy, bool button)
{
    apple2gs_pointer(m, m->ptr_x + dx, m->ptr_y + dy, button);
}

// ---------------------------------------------------------------------------
// Video, a line (of 200) at a time: Super Hi-Res, or the Apple II modes in
// 560x192 inside a border (dsp-cpp's 640x400 with its lines shown twice)
#define A2X 40
#define A2Y 4

static inline bool flash_on(const apple2gs *m) { return (m->frame_count / 16) & 1; }

static void fill(uint32_t *out, int x, int w, uint32_t c)
{
    for (int i = 0; i < w; i++) out[x + i] = c;
}

static void text_line(const apple2gs *m, int l, uint32_t *out)
{
    const int row = l >> 3, gy = l & 7;
    const uint32_t fg = m->palette16[(m->text_color >> 4) & 0xf], bg = m->palette16[m->text_color & 0xf];
    const bool page2 = (m->statereg & PAGE2) && !m->st80;
    const uint32_t base = (page2 ? 0x800u : 0x400u) + text_row_offset(row);
    const bool flash = flash_on(m), col80 = m->vid80;
    const int cols = col80 ? 80 : 40, px_w = col80 ? 1 : 2;
    for (int c = 0; c < cols; c++) {
        uint8_t ch;
        if (col80) ch = (c & 1) ? m->slow_ram[base + (uint32_t)(c >> 1)] : m->slow_ram[0x10000 + base + (uint32_t)(c >> 1)];
        else ch = m->slow_ram[base + (uint32_t)c];
        // the video ROM is active low (a 0 bit lights the dot), indexed by
        // the screen code; its second 2 KB is the alternate (MouseText) set
        const bool invert_flash = !m->altchar && ch >= 0x40 && ch < 0x80 && flash;
        uint8_t bits = (uint8_t)~m->chr_rom[(m->altchar ? 0x800u : 0u) + (uint32_t)ch * 8 + (uint32_t)gy];
        if (invert_flash) bits = (uint8_t)~bits;
        for (int x = 0; x < 7; x++) {
            const uint32_t color = (bits & (1 << x)) ? fg : bg;
            const int px = A2X + (c * 7 + x) * px_w;
            for (int k = 0; k < px_w; k++) out[px + k] = color;
        }
    }
}

static void lores_line(const apple2gs *m, int l, uint32_t *out)
{
    const bool page2 = (m->statereg & PAGE2) && !m->st80, dbl = m->vid80 && !m->an3;
    const int row = l >> 3, half = (l & 7) >= 4;
    const uint32_t base = (page2 ? 0x800u : 0x400u) + text_row_offset(row);
    for (int c = 0; c < 40; c++) {
        const uint8_t mm = m->slow_ram[base + (uint32_t)c];
        const uint8_t cm = (uint8_t)(half ? (mm >> 4) : (mm & 0xf));
        if (dbl) {
            const uint8_t a = m->slow_ram[0x10000 + base + (uint32_t)c];
            uint8_t ca = (uint8_t)(half ? (a >> 4) : (a & 0xf));
            ca = (uint8_t)(((ca << 1) | (ca >> 3)) & 0xf);      // the aux colours are rotated
            fill(out, A2X + c * 14, 7, m->palette16[ca]);
            fill(out, A2X + c * 14 + 7, 7, m->palette16[cm]);
        } else {
            fill(out, A2X + c * 14, 14, m->palette16[cm]);
        }
    }
}

static uint32_t hires_addr(bool page2, int line)
{
    return (page2 ? 0x4000u : 0x2000u) + (uint32_t)((line & 7) << 10) + (uint32_t)(((line >> 3) & 7) << 7) +
           (uint32_t)((line >> 6) * 40);
}

static void hires_line(const apple2gs *m, int line, uint32_t *out)
{
    const bool page2 = (m->statereg & PAGE2) && !m->st80;
    const uint32_t addr = hires_addr(page2, line), white = m->palette16[15], black = m->palette16[0];
    bool bits[282] = {false}, shift[280];
    for (int c = 0; c < 40; c++) {
        const uint8_t b = m->slow_ram[addr + (uint32_t)c];
        for (int i = 0; i < 7; i++) {
            bits[1 + c * 7 + i] = (b >> i) & 1;
            shift[c * 7 + i] = (b & 0x80) != 0;
        }
    }
    uint32_t *o = out + A2X;
    for (int x = 0; x < 280; x++) {
        uint32_t color;
        const bool on = bits[x + 1], left = bits[x], right = bits[x + 2];
        if (m->mono || (m->newvideo & 0x20)) {
            color = on ? white : black;
        } else if (on && (left || right)) {
            color = white;
        } else if (on || (left && right)) {
            // a dot by itself (or a gap between two dots of a phase): colour
            const bool odd = (x & 1) != 0, pal = shift[x];
            int idx = pal ? (odd ? 9 : 6) : (odd ? 12 : 3);
            if (!on) idx = pal ? (odd ? 6 : 9) : (odd ? 3 : 12);
            color = m->palette16[idx];
            if (!on && !(left && right)) color = black;
        } else {
            color = black;
        }
        o[x * 2] = o[x * 2 + 1] = color;
    }
}

static void dhires_line(const apple2gs *m, int line, uint32_t *out)
{
    const bool page2 = (m->statereg & PAGE2) && !m->st80, mono = m->mono || (m->newvideo & 0x20);
    const uint32_t addr = hires_addr(page2, line);
    uint8_t dots[560];
    for (int c = 0; c < 40; c++) {
        const uint8_t a = m->slow_ram[0x10000 + addr + (uint32_t)c], mm = m->slow_ram[addr + (uint32_t)c];
        for (int i = 0; i < 7; i++) {
            dots[c * 14 + i] = (a >> i) & 1;
            dots[c * 14 + 7 + i] = (mm >> i) & 1;
        }
    }
    uint32_t *o = out + A2X;
    for (int x = 0; x < 560; x++) {
        if (mono) {
            o[x] = dots[x] ? m->palette16[15] : m->palette16[0];
        } else {
            const int g = x & ~3;
            const int v = dots[g] | (dots[g + 1] << 1) | (dots[g + 2] << 2) | (dots[g + 3] << 3);
            int idx = ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3);
            idx = ((idx >> 1) | (idx << 3)) & 0xf;
            o[x] = m->palette16[idx];
        }
    }
}

static void shr_line(const apple2gs *m, int line, uint32_t *out)
{
    const uint8_t *e1 = &m->slow_ram[0x10000];
    const uint8_t scb = e1[0x9d00 + line];
    const uint8_t *pal = &e1[0x9e00 + (scb & 0x0f) * 32];
    uint32_t colors[16];
    for (int i = 0; i < 16; i++) colors[i] = rgb12((uint16_t)(pal[i * 2] | ((pal[i * 2 + 1] & 0x0f) << 8)));
    const uint8_t *px = &e1[0x2000 + line * 160];
    if (scb & 0x80) {
        for (int b = 0; b < 160; b++) {
            const uint8_t v = px[b];
            out[b * 4 + 0] = colors[8 + ((v >> 6) & 3)];
            out[b * 4 + 1] = colors[12 + ((v >> 4) & 3)];
            out[b * 4 + 2] = colors[0 + ((v >> 2) & 3)];
            out[b * 4 + 3] = colors[4 + (v & 3)];
        }
    } else {
        const bool fillmode = (scb & 0x20) != 0;
        uint32_t last = colors[0];
        for (int b = 0; b < 160; b++) {
            const uint8_t v = px[b];
            for (int n = 0; n < 2; n++) {
                const int idx = n == 0 ? (v >> 4) : (v & 0xf);
                const uint32_t c = (fillmode && idx == 0) ? last : colors[idx];
                last = c;
                out[b * 4 + n * 2] = out[b * 4 + n * 2 + 1] = c;
            }
        }
    }
}

void apple2gs_render_row(const apple2gs *m, int y, uint32_t *out)
{
    if (y < 0 || y >= A2GS_H) return;
    if (m->newvideo & 0x80) {
        shr_line(m, y, out);
        return;
    }
    const uint32_t border = m->palette16[m->border & 0xf];
    if (y < A2Y || y >= A2Y + 192) {
        fill(out, 0, A2GS_W, border);
        return;
    }
    fill(out, 0, A2X, border);
    fill(out, A2GS_W - A2X, A2X, border);
    const int l = y - A2Y;
    if (!m->text && l < (m->mixed ? 160 : 192)) {
        if (m->hires) {
            if (m->vid80 && !m->an3) dhires_line(m, l, out);
            else hires_line(m, l, out);
        } else {
            lores_line(m, l, out);
        }
        return;
    }
    text_line(m, l, out);
}

void apple2gs_set_rgb565(apple2gs *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b)) { m->rgb565 = fn; }

void apple2gs_render_row565(const apple2gs *m, int y, uint16_t *out)
{
    uint32_t line[A2GS_W];
    apple2gs_render_row(m, y, line);
    for (int x = 0; x < A2GS_W; x++) {
        const uint32_t c = line[x];
        const uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
        out[x] = m->rgb565 ? m->rgb565(r, g, b) : (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// ---------------------------------------------------------------------------
// Disks
static bool ends_with_ci(const char *s, const char *ext)
{
    if (!s) return false;
    size_t n = strlen(s), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++) {
        char a = s[n - k + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return false;
    }
    return true;
}

const char *apple2gs_insert(apple2gs *m, int unit, a2gs_file *f, const char *name)
{
    if (unit < 0 || unit >= A2GS_DISKS) unit = 0;
    if (unit > nunits(m)) unit = nunits(m);         // (the units are kept in a row)
    if (unit >= A2GS_DISKS) unit = A2GS_DISKS - 1;
    gsdisk d;
    memset(&d, 0, sizeof d);
    d.f = *f;
    const uint32_t size = f->size;
    uint8_t h[64];
    const char *why = NULL;
    if (size >= 64 && f->read(f->ctx, 0, h, 64) && memcmp(h, "2IMG", 4) == 0) {
#define RD32(o) ((uint32_t)h[o] | ((uint32_t)h[(o) + 1] << 8) | ((uint32_t)h[(o) + 2] << 16) | ((uint32_t)h[(o) + 3] << 24))
        const uint32_t format = RD32(0x0c), offset = RD32(0x18), length = RD32(0x1c);
        if (format == 0 && length == 143360 && offset + length <= size) {
            d.offset = offset;
            d.blocks = 280;
            d.dos_order = true;
        } else if (format == 1 && offset + length <= size) {
            d.offset = offset;
            d.blocks = length / 512;
        } else {
            why = "unsupported 2IMG format (only ProDOS or DOS order)";
        }
        if (RD32(0x10) & 0x80000000u) d.write_protect = true;
#undef RD32
    } else if (size == 143360 && (ends_with_ci(name, ".dsk") || ends_with_ci(name, ".do"))) {
        d.blocks = 280;
        d.dos_order = true;
    } else if (size == 0 || (size % 512) != 0) {
        why = "disk image size must be a multiple of 512 bytes";
    } else {
        d.blocks = size / 512;
    }
    if (why) {
        if (f->close) f->close(f->ctx);
        return why;
    }
    if (!d.f.write) d.write_protect = true;
    if (m->disk[unit].in && m->disk[unit].f.close) m->disk[unit].f.close(m->disk[unit].f.ctx);
    d.in = true;
    m->disk[unit] = d;
    return NULL;
}

void apple2gs_eject(apple2gs *m, int unit)
{
    if (unit < 0 || unit >= A2GS_DISKS || !m->disk[unit].in) return;
    if (m->disk[unit].f.close) m->disk[unit].f.close(m->disk[unit].f.ctx);
    memset(&m->disk[unit], 0, sizeof m->disk[unit]);
    // (the units after it move down)
    for (int u = unit; u + 1 < A2GS_DISKS; u++) {
        m->disk[u] = m->disk[u + 1];
        memset(&m->disk[u + 1], 0, sizeof m->disk[u + 1]);
    }
}

bool apple2gs_has_disk(const apple2gs *m, int unit) { return unit >= 0 && unit < A2GS_DISKS && m->disk[unit].in; }
bool apple2gs_disk_busy(const apple2gs *m) { return m->disk_busy > 0; }

const char *apple2gs_load_file(apple2gs *m, int unit, const char *path)
{
    blkfile f;
    if (!blkfile_open(&f, path)) return "cannot open the disk image";
    return apple2gs_insert(m, unit, &f, path);
}
