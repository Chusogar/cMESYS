// a2600_core.c - Atari 2600 / VCS: a C11 migration of dsp-cpp's
// drivers/consoles/a2600 (see a2600_core.h).
#include "a2600_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m65xx.h"
#include "mos6532.h"
#include "tia.h"

// Flat 2K/4K; Atari F8/F6/F4 (+Superchip RAM); Parker Bros E0;
// M Network E7; Activision FE; Tigervision 3F; Atari F0 (Megaboy).
enum mapper { M_FLAT, M_F8, M_F6, M_F4, M_F0, M_E0, M_E7, M_FE, M_T3F };

struct a2600 {
    m65xx cpu;
    tia tia;
    mos6532 riot;

    uint8_t *rom;               // NULL: no cartridge
    size_t rom_size, rom_mask;
    enum mapper mapper;
    int bank, bank_count;
    bool superchip;
    uint8_t superchip_ram[128];
    int e0_slice[4];            // E0: three switchable 1K slices, the fourth the last 1K
    int e7_ram_page;            // E7: 2K ROM bank (or 1K RAM when bank 7), 256-byte RAM page
    uint8_t e7_ram[2048];
    bool fe_pending, fe_jsr;    // FE: bank chosen by the byte after a stack access at $01FE

    // TIA register writes wait for the end of the CPU instruction: the
    // 6502 core reports an instruction's cycles only after it has executed.
    struct { uint8_t reg, value; } pending[4];
    int pending_count;

    unsigned inputs;
    uint8_t dips;               // colour, amateur difficulty

    int line_in_frame, first_visible, last_row;
    bool prev_vsync, frame_done;
    uint8_t frame[A2600_W * A2600_H];   // TIA colour indices

    int audio_rate;
    bool turbo;
    int16_t audio[A2600_AUDIO_MAX];
    int naudio;
    uint16_t pal565[128];
    const char *error;
};

static void end_line(a2600 *m);

// ---------------------------------------------------------------------------
// Cartridge
// ---------------------------------------------------------------------------

static void lower_copy(const char *s, char *out, size_t n)
{
    size_t i = 0;
    for (; s && s[i] && i + 1 < n; i++) out[i] = (char)tolower((unsigned char)s[i]);
    out[i] = 0;
}

static bool name_says_superchip(const char *name)
{
    char l[512];
    lower_copy(name, l, sizeof l);
    return strstr(l, "f8sc") || strstr(l, "f6sc") || strstr(l, "f4sc") || strstr(l, ".sc.");
}

static bool contains(const uint8_t *data, size_t size, const uint8_t *pattern, size_t len, int needed)
{
    int found = 0;
    for (size_t i = 0; i + len <= size; i++)
        if (memcmp(&data[i], pattern, len) == 0 && ++found >= needed) return true;
    return false;
}

// Superchip carts leave the RAM window of every 4K bank as a repeated
// 128-byte block (the write port and the read port show the same filler).
static bool probably_superchip(const uint8_t *data, size_t size)
{
    if (size < 8192 || size % 4096 != 0) return false;
    for (size_t bank = 0; bank < size; bank += 4096)
        if (memcmp(&data[bank], &data[bank + 128], 128) != 0) return false;
    return true;
}

// Signatures after Stella's CartDetector.
static bool probably_e0(const uint8_t *data, size_t size)
{
    static const uint8_t sig[][3] = {
        { 0x8D, 0xE0, 0x1F }, { 0x8D, 0xE0, 0x5F }, { 0x8D, 0xE9, 0xFF }, { 0x0C, 0xE0, 0x1F },
        { 0xAD, 0xE0, 0x1F }, { 0xAD, 0xE9, 0xFF }, { 0xAD, 0xED, 0xFF }, { 0xAD, 0xF3, 0xBF },
    };
    for (size_t i = 0; i < sizeof sig / sizeof sig[0]; i++)
        if (contains(data, size, sig[i], 3, 1)) return true;
    return false;
}

static bool probably_e7(const uint8_t *data, size_t size)
{
    static const uint8_t sig[][3] = {
        { 0xAD, 0xE2, 0xFF }, { 0xAD, 0xE5, 0xFF }, { 0xAD, 0xE5, 0x1F }, { 0xAD, 0xE7, 0x1F },
        { 0x0C, 0xE7, 0x1F }, { 0x8D, 0xE7, 0xFF }, { 0x8D, 0xE7, 0x1F },
    };
    for (size_t i = 0; i < sizeof sig / sizeof sig[0]; i++)
        if (contains(data, size, sig[i], 3, 1)) return true;
    return false;
}

static bool probably_fe(const uint8_t *data, size_t size)
{
    static const uint8_t sig[][5] = {
        { 0x20, 0x00, 0xD0, 0xC6, 0xC5 },
        { 0x20, 0xC3, 0xF8, 0xA5, 0x82 },
        { 0xD0, 0xFB, 0x20, 0x73, 0xFE },
        { 0x20, 0x00, 0xF0, 0x84, 0xD6 },
    };
    for (size_t i = 0; i < sizeof sig / sizeof sig[0]; i++)
        if (contains(data, size, sig[i], 5, 1)) return true;
    return false;
}

static bool probably_3f(const uint8_t *data, size_t size)
{
    static const uint8_t sig[] = { 0x85, 0x3F };
    return contains(data, size, sig, sizeof sig, 2);
}

const char *a2600_mapper_name(const a2600 *m)
{
    if (!m->rom) return "-";
    switch (m->mapper) {
    case M_FLAT: return "4K";
    case M_F8: return m->superchip ? "F8SC" : "F8";
    case M_F6: return m->superchip ? "F6SC" : "F6";
    case M_F4: return m->superchip ? "F4SC" : "F4";
    case M_F0: return "F0";
    case M_E0: return "E0";
    case M_E7: return "E7";
    case M_FE: return "FE";
    case M_T3F: return "3F";
    }
    return "?";
}

static void detect_mapper(a2600 *m, const uint8_t *data, size_t size)
{
    m->mapper = M_FLAT;
    if (size <= 4096) return;
    if (size <= 8192) {
        if (probably_superchip(data, size)) m->superchip = true, m->mapper = M_F8;
        else if (probably_e0(data, size)) m->mapper = M_E0;
        else if (probably_3f(data, size)) m->mapper = M_T3F;
        else if (probably_fe(data, size)) m->mapper = M_FE;
        else m->mapper = M_F8;
    } else if (size <= 16384) {
        if (probably_superchip(data, size)) m->superchip = true, m->mapper = M_F6;
        else if (probably_e7(data, size)) m->mapper = M_E7;
        else if (probably_3f(data, size)) m->mapper = M_T3F;
        else m->mapper = M_F6;
    } else if (size <= 32768) {
        if (probably_superchip(data, size)) m->superchip = true, m->mapper = M_F4;
        else if (probably_3f(data, size)) m->mapper = M_T3F;
        else m->mapper = M_F4;
    } else {
        m->mapper = probably_3f(data, size) ? M_T3F : M_F0;
    }
}

bool a2600_load_mem(a2600 *m, const uint8_t *data, size_t size, const char *name)
{
    m->error = NULL;
    if (!data || size == 0 || size > A2600_MAX_CART) {
        m->error = "cartridge is empty or larger than 64 KiB";
        return false;
    }
    bool superchip = false;
    if (size > 128 && (size % 1024 == 128)) {
        superchip = true;
        size -= 128;
    }
    // Pad odd dumps up to the next power-of-two bank window.
    size_t padded = 1;
    while (padded < size) padded <<= 1;
    if (padded < 2048) padded = 2048;
    uint8_t *rom = (uint8_t *)malloc(padded);
    if (!rom) {
        m->error = "out of memory for the cartridge";
        return false;
    }
    memcpy(rom, data, size);
    for (size_t i = size; i < padded; i++) rom[i] = rom[i % size];

    free(m->rom);
    m->rom = rom;
    m->rom_size = padded;
    m->rom_mask = padded - 1;
    m->superchip = superchip;
    detect_mapper(m, rom, padded);
    switch (m->mapper) {
    case M_FLAT: m->bank_count = 1; break;
    case M_E0:
    case M_E7: m->bank_count = 8; break;
    case M_T3F: m->bank_count = (int)(padded / 2048); break;
    default: m->bank_count = (int)(padded / 4096) > 1 ? (int)(padded / 4096) : 1; break;
    }
    memset(m->superchip_ram, 0, sizeof m->superchip_ram);
    memset(m->e7_ram, 0, sizeof m->e7_ram);
    if (name && name_says_superchip(name)) m->superchip = true;
    a2600_reset(m);
    return true;
}

bool a2600_load_pbuf(a2600 *m, zx_pbuf *pb, const char *name)
{
    m->error = NULL;
    if (pb->size == 0 || pb->size > A2600_MAX_CART) {
        m->error = "cartridge is empty or larger than 64 KiB";
        zx_pb_free(pb);
        return false;
    }
    uint8_t *flat = zx_pb_flatten(pb, 0, pb->size);
    const size_t size = pb->size;
    zx_pb_free(pb);
    if (!flat) {
        m->error = "out of memory for the cartridge";
        return false;
    }
    const bool ok = a2600_load_mem(m, flat, size, name);
    free(flat);
    return ok;
}

bool a2600_load_file(a2600 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->error = "cannot open the file";
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc(A2600_MAX_CART + 1);
    if (!buf) {
        fclose(f);
        m->error = "out of memory";
        return false;
    }
    const size_t n = fread(buf, 1, A2600_MAX_CART + 1, f);
    fclose(f);
    if (n >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 3 && buf[3] == 4) {
        free(buf);
        m->error = "zipped cartridge: unzip it first";
        return false;
    }
    const bool ok = a2600_load_mem(m, buf, n, path);
    free(buf);
    return ok;
}

bool a2600_has_cart(const a2600 *m) { return m->rom != NULL; }

void a2600_eject(a2600 *m)
{
    free(m->rom);
    m->rom = NULL;
    m->rom_size = 0;
    a2600_reset(m);
}

const char *a2600_last_error(const a2600 *m) { return m->error ? m->error : ""; }

static void touch_hotspot(a2600 *m, uint16_t offset)
{
    int next = m->bank;
    switch (m->mapper) {
    case M_F8:
        if (offset == 0xff8) next = 0;
        else if (offset == 0xff9) next = 1;
        break;
    case M_F6:
        if (offset >= 0xff6 && offset <= 0xff9) next = (int)(offset - 0xff6);
        break;
    case M_F4:
        if (offset >= 0xff4 && offset <= 0xffb) next = (int)(offset - 0xff4);
        break;
    case M_F0:
        if (offset == 0xff0) next = (m->bank + 1) % m->bank_count;
        break;
    case M_E0:
        if (offset >= 0xfe0 && offset <= 0xff7) m->e0_slice[(offset - 0xfe0) >> 3] = offset & 7;
        break;
    case M_E7:
        if (offset >= 0xfe0 && offset <= 0xfe7) next = offset & 7;
        else if (offset >= 0xfe8 && offset <= 0xfeb) m->e7_ram_page = offset & 3;
        break;
    default:
        break;
    }
    if (next >= 0 && next < m->bank_count) m->bank = next;
}

static uint8_t read_cartridge(a2600 *m, uint16_t offset)
{
    offset &= 0x0fff;
    if (!m->rom) return 0xff;
    touch_hotspot(m, offset);
    switch (m->mapper) {
    case M_FLAT:
        return m->rom[offset & m->rom_mask];
    case M_E0:
        return m->rom[(((size_t)m->e0_slice[offset >> 10] << 10) | (offset & 0x3ff)) & m->rom_mask];
    case M_E7:
        if (offset < 0x800) {
            if (m->bank == 7) return offset >= 0x400 ? m->e7_ram[offset & 0x3ff] : 0xff;
            return m->rom[(((size_t)m->bank << 11) | offset) & m->rom_mask];
        }
        if (offset < 0xa00)
            return offset >= 0x900 ? m->e7_ram[0x400 + m->e7_ram_page * 256 + (offset & 0xff)] : 0xff;
        return m->rom[(((size_t)7 << 11) | (offset & 0x7ff)) & m->rom_mask];
    case M_T3F:
        if (offset < 0x800) return m->rom[(((size_t)m->bank << 11) | offset) & m->rom_mask];
        return m->rom[(m->rom_size - 0x800 + (offset & 0x7ff)) & m->rom_mask];
    default:
        break;
    }
    if (m->superchip) {
        if (offset < 0x80) return 0xff;
        if (offset < 0x100) return m->superchip_ram[offset - 0x80];
    }
    return m->rom[((size_t)m->bank * 0x1000 + offset) & m->rom_mask];
}

static void write_cartridge(a2600 *m, uint16_t offset, uint8_t value)
{
    offset &= 0x0fff;
    if (!m->rom) return;
    touch_hotspot(m, offset);
    if (m->mapper == M_E7) {
        if (offset < 0x400 && m->bank == 7) m->e7_ram[offset] = value;
        else if (offset >= 0x800 && offset < 0x900) m->e7_ram[0x400 + m->e7_ram_page * 256 + (offset & 0xff)] = value;
        return;
    }
    if (m->superchip && offset < 0x80) m->superchip_ram[offset] = value;
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------

static uint8_t read_byte(void *user, uint16_t full_address)
{
    a2600 *m = (a2600 *)user;
    const uint16_t address = full_address & 0x1fff;
    uint8_t value;
    if (address & 0x1000) value = read_cartridge(m, address);
    else if (address & 0x80) {
        if (address & 0x200) value = mos6532_io_read(&m->riot, (uint8_t)(address & 0x1f));
        else value = mos6532_ram_read(&m->riot, (uint8_t)(address & 0x7f));
    } else {
        value = tia_read(&m->tia, (uint8_t)(address & 0x0f));
    }
    if (m->mapper == M_FE && m->fe_jsr) {
        m->fe_jsr = false;
        m->bank = (full_address & 0x2000) ? 0 : 1;
        if (address & 0x1000) value = read_cartridge(m, address);
    } else if (m->mapper == M_FE) {
        // RTS pulls the return address: the byte after the $01FE access is
        // its high byte, whose bit 5 (address bit 13) picks the bank.
        if (m->fe_pending) {
            m->bank = (value & 0x20) ? 0 : 1;
            m->fe_pending = false;
        } else if (full_address == 0x01fe) {
            m->fe_pending = true;
        }
    }
    return value;
}

static void write_byte(void *user, uint16_t full_address, uint8_t value)
{
    a2600 *m = (a2600 *)user;
    const uint16_t address = full_address & 0x1fff;
    if (m->mapper == M_FE) {
        // JSR pushes the return address to $01FF/$01FE and then jumps: the
        // target's address bit 13 picks the bank.
        m->fe_pending = false;
        m->fe_jsr = full_address == 0x01fe;
    }
    if (address & 0x1000) {
        write_cartridge(m, address, value);
        return;
    }
    if (address & 0x80) {
        if (address & 0x200) mos6532_io_write(&m->riot, (uint8_t)(address & 0x1f), value);
        else mos6532_ram_write(&m->riot, (uint8_t)(address & 0x7f), value);
        return;
    }
    // Tigervision: any write to $00-$3F selects the 2K bank at $1000.
    if (m->mapper == M_T3F && (address & 0x1fc0) == 0)
        m->bank = value % (m->bank_count > 1 ? m->bank_count : 1);
    if (m->pending_count < 4) {
        m->pending[m->pending_count].reg = (uint8_t)(address & 0x3f);
        m->pending[m->pending_count].value = value;
        m->pending_count++;
    } else {
        tia_write(&m->tia, (uint8_t)(address & 0x3f), value);
    }
}

// Advance the TIA (and the frame/line bookkeeping) by colour clocks.
static void advance(a2600 *m, int clocks)
{
    while (clocks > 0) {
        int step = TIA_CLOCKS_PER_LINE - m->tia.hclock;
        if (step > clocks) step = clocks;
        tia_run(&m->tia, step);
        clocks -= step;
        if (tia_line_done(&m->tia)) end_line(m);
    }
}

static void on_cpu_cycles(void *user, int cycles)
{
    a2600 *m = (a2600 *)user;
    mos6532_tick(&m->riot, cycles);
    if (m->pending_count == 0) {
        advance(m, cycles * 3);
        return;
    }
    // A store's write cycle is the instruction's last one. HMOVE is judged
    // at the start of that cycle (an HMOVE ending a line must not count as
    // the next line's early HMOVE); the other registers take the new value
    // at its end.
    advance(m, (cycles - 1) * 3);
    bool wsync = false;
    for (int i = 0; i < m->pending_count; i++)
        if (m->pending[i].reg == 0x2a) tia_write(&m->tia, 0x2a, m->pending[i].value);
    advance(m, 3);
    for (int i = 0; i < m->pending_count; i++) {
        const uint8_t reg = m->pending[i].reg, value = m->pending[i].value;
        if (reg == 0x02) wsync = true;
        else if (reg == 0x03) end_line(m);      // RSYNC (rarely used)
        else if (reg != 0x2a) tia_write(&m->tia, reg, value);
    }
    m->pending_count = 0;
    // WSYNC: RDY holds the CPU until the next line starts.
    if (wsync && m->tia.hclock != 0) {
        const int wait = TIA_CLOCKS_PER_LINE - m->tia.hclock;
        advance(m, wait);
        mos6532_tick(&m->riot, wait / 3);
    }
}

static void end_line(a2600 *m)
{
    // Two ticks of the 31.4 kHz audio clock per scanline.
    const int room = m->turbo ? 0 : A2600_AUDIO_MAX - m->naudio;
    tia_clock_audio(&m->tia);
    m->naudio += tia_emit_audio(&m->tia, TIA_CPU_PER_LINE / 2, A2600_CPU_CLOCK, m->audio_rate,
                                m->audio + m->naudio, room);
    tia_clock_audio(&m->tia);
    const int room2 = m->turbo ? 0 : A2600_AUDIO_MAX - m->naudio;
    m->naudio += tia_emit_audio(&m->tia, TIA_CPU_PER_LINE / 2, A2600_CPU_CLOCK, m->audio_rate,
                                m->audio + m->naudio, room2);

    // Show the picture from the first line of the frame with VBLANK off, so
    // blanked lines inside the picture stay black instead of shifting it.
    const bool vsync = m->tia.vsync;
    if (!tia_blanked(&m->tia) && m->first_visible < 0) m->first_visible = m->line_in_frame;
    if (m->first_visible >= 0) {
        const int row = m->line_in_frame - m->first_visible;
        if (row >= 0 && row < A2600_H) {
            memcpy(&m->frame[row * A2600_W], m->tia.line, A2600_W);
            m->last_row = row;
        }
    }
    m->line_in_frame++;
    tia_begin_line(&m->tia);

    // A new frame starts with VSYNC; runaway frames are cut at 320 lines.
    if ((vsync && !m->prev_vsync && m->line_in_frame > 1) || m->line_in_frame >= 320) {
        for (int row = m->last_row + 1; row < A2600_H; row++) memset(&m->frame[row * A2600_W], 0, A2600_W);
        m->line_in_frame = 0;
        m->first_visible = -1;
        m->last_row = -1;
        m->frame_done = true;
    }
    m->prev_vsync = vsync;
}

// Active low: left joystick in D7-D4 (right, left, down, up), right
// joystick in D3-D0.
static uint8_t read_swcha(void *user)
{
    const a2600 *m = (const a2600 *)user;
    uint8_t value = 0xff;
    for (int p = 0; p < 2; p++) {
        const unsigned b = m->inputs >> (p * A2600_P2);
        const int shift = p == 0 ? 4 : 0;
        if (b & A2600_UP) value &= (uint8_t)~(1u << shift);
        if (b & A2600_DOWN) value &= (uint8_t)~(1u << (shift + 1));
        if (b & A2600_LEFT) value &= (uint8_t)~(1u << (shift + 2));
        if (b & A2600_RIGHT) value &= (uint8_t)~(1u << (shift + 3));
    }
    return value;
}

// D0 reset, D1 select (0 = pressed). D3 colour (1 = colour). D6/D7 difficulty.
static uint8_t read_swchb(void *user)
{
    const a2600 *m = (const a2600 *)user;
    uint8_t value = 0x34;                       // unused bits read high
    if (!(m->inputs & A2600_RESET)) value |= 0x01;
    if (!(m->inputs & A2600_SELECT)) value |= 0x02;
    if (m->dips & 0x08) value |= 0x08;
    if (m->dips & 0x40) value |= 0x40;
    if (m->dips & 0x80) value |= 0x80;
    return value;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

static uint16_t rgb565_default(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void a2600_set_rgb565(a2600 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b))
{
    if (!fn) fn = rgb565_default;
    for (int i = 0; i < 128; i++) {
        const uint32_t c = tia_ntsc[i];
        m->pal565[i] = fn((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
}

a2600 *a2600_create(int audio_rate)
{
    a2600 *m = (a2600 *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->audio_rate = audio_rate > 0 ? audio_rate : A2600_AUDIO_RATE;
    m->dips = 0x08;
    m->cpu.read = read_byte;
    m->cpu.write = write_byte;
    m->cpu.cycles = on_cpu_cycles;
    m->cpu.user = m;
    m->riot.pa_in = read_swcha;
    m->riot.pb_in = read_swchb;
    m->riot.user = m;
    a2600_set_rgb565(m, NULL);
    a2600_reset(m);
    return m;
}

void a2600_destroy(a2600 *m)
{
    if (!m) return;
    free(m->rom);
    free(m);
}

void a2600_reset(a2600 *m)
{
    tia_reset(&m->tia);
    mos6532_reset(&m->riot);
    m->pending_count = 0;
    m->line_in_frame = 0;
    m->first_visible = -1;
    m->last_row = -1;
    m->prev_vsync = false;
    m->naudio = 0;
    memset(m->frame, 0, sizeof m->frame);
    switch (m->mapper) {
    case M_E0:
        m->e0_slice[0] = 4, m->e0_slice[1] = 5, m->e0_slice[2] = 6, m->e0_slice[3] = 7;
        break;
    case M_E7: m->bank = 0; m->e7_ram_page = 0; break;
    case M_FE: m->bank = 0; m->fe_pending = false; break;
    case M_T3F: m->bank = 0; break;
    default: m->bank = m->bank_count - 1; break;
    }
    m->cpu.halted = false;
    m65xx_reset(&m->cpu);
}

int a2600_run_frame(a2600 *m, const int16_t **audio)
{
    m->naudio = 0;
    m->frame_done = false;
    if (m->rom) {
        // One frame is everything up to the next VSYNC; cap the work in case
        // a cartridge never strobes it.
        const int limit = TIA_CPU_PER_LINE * 330;
        int executed = 0;
        while (!m->frame_done && executed < limit) executed += m65xx_run(&m->cpu, 1);
    } else {
        // No cartridge: a black screen and silence, at the frame rate
        memset(m->frame, 0, sizeof m->frame);
        const int n = m->turbo ? 0 : (int)(m->audio_rate / A2600_FPS);
        memset(m->audio, 0, (size_t)n * sizeof m->audio[0]);
        m->naudio = n;
    }
    if (audio) *audio = m->audio;
    return m->naudio;
}

void a2600_input(a2600 *m, unsigned bits)
{
    m->inputs = bits;
    tia_set_inpt4(&m->tia, (bits & A2600_FIRE) != 0);
    tia_set_inpt5(&m->tia, ((bits >> A2600_P2) & A2600_FIRE) != 0);
}

void a2600_set_switches(a2600 *m, uint8_t switches) { m->dips = switches; }
uint8_t a2600_switches(const a2600 *m) { return m->dips; }
void a2600_set_turbo(a2600 *m, bool on) { m->turbo = on; }

void a2600_render_row(const a2600 *m, int y, uint32_t *out)
{
    const uint8_t *s = &m->frame[y * A2600_W];
    for (int x = 0; x < A2600_W; x++) out[x] = tia_ntsc[s[x]] & 0xffffff;
}

void a2600_render_row565(const a2600 *m, int y, uint16_t *out)
{
    const uint8_t *s = &m->frame[y * A2600_W];
    for (int x = 0; x < A2600_W; x++) out[x] = m->pal565[s[x]];
}
