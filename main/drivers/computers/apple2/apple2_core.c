// apple2_core.c - the Apple II family (see apple2_core.h): C11 migration of
// dsp-cpp's drivers/computers/apple2.cpp and video/apple2_video.cpp.
//
// Memory goes through the 6502's direct pages (m65xx page_r / page_w),
// rebuilt when a soft switch that moves RAM changes (language card, the
// //e's RAMRD / RAMWRT / ALTZP / 80STORE with PAGE2 and HIRES); $C000-$CFFF
// (I/O and the slots' ROM) goes through the handlers, as dsp-cpp does for all.
#include "apple2_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m65xx.h"
#include "diskii.h"

#define LINES 262
#define CYCLES_PER_LINE 65

struct apple2 {
    int model;
    bool iie;
    m65xx cpu;
    diskii disk;
    uint8_t main[0x10000];
    uint8_t *aux;                       // //e: 64 KB
    uint8_t lc_bank2[0x1000];
    uint8_t *aux_lc_bank2;              // //e: 4 KB
    // ROM pages $C0-$FF (NULL: none there)
    const uint8_t *rom[64];
    const uint8_t *chargen;
    int chargen_size;
    const uint8_t *disk_prom;

    bool text, mixed, page2, hires, store80, ramrd, ramwrt, intcxrom, altzp, slotc3rom;
    bool col80, altcharset, an3, c8rom;
    bool lc_read_ram, lc_write_ram, lc_use_bank2, lc_prewrite;
    bool caps_lock, speaker;
    bool joyb[2];                       // the joystick's buttons
    uint8_t paddle[4];
    uint32_t cycles;
    uint32_t paddle_start;

    // keyboard
    uint8_t keyboard;                   // $C000: the character and its strobe (bit 7)
    bool any_key;
    bool held[A2K_COUNT];
    int rep_code;                       // key repeating (-1: none; 0x100 + c: a typed character)
    int rep_frames;
    bool reset_held;

    int scanline;
    int flash_phase;
    bool turbo;

    // the speaker: a box filter at the output rate, then a DC blocker
    int audio_rate;
    int64_t audio_acc;
    int32_t level_sum;
    int32_t level_cycles;
    float hp_in, hp_out;
    int16_t audio[A2_AUDIO_MAX];
    int naudio;

    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
    uint16_t pal565[32];
};

static const uint8_t ff_page[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

const char *apple2_title(int model)
{
    switch (model) {
    case A2_II: return "Apple II";
    case A2_IIPLUS: return "Apple II+";
    case A2_IIE: return "Apple IIe";
    default: return "Apple IIe Enhanced";
    }
}

int apple2_model(const apple2 *m) { return m->model; }

static inline uint8_t rom_byte(const apple2 *m, uint16_t a)
{
    const uint8_t *p = m->rom[(a >> 8) - 0xC0];
    return p ? p[a & 0xFF] : 0xFF;
}

// ---------------------------------------------------------------------------
// The memory map in the CPU's direct pages
static uint8_t *lc_ptr(apple2 *m, uint16_t address, bool aux)
{
    uint8_t *ram = aux ? m->aux : m->main;
    if (address < 0xE000) {
        if (!m->lc_use_bank2) return ram + address;             // bank 1: the RAM under $D000
        return (aux ? m->aux_lc_bank2 : m->lc_bank2) + (address - 0xD000);
    }
    return ram + address;
}

static void rebuild_map(apple2 *m)
{
    m65xx *c = &m->cpu;
    const bool iie = m->iie;
    uint8_t *zp = iie && m->altzp ? m->aux : m->main;
    c->page_r[0] = c->page_w[0] = zp;
    c->page_r[1] = c->page_w[1] = zp + 0x100;
    for (int p = 0x02; p < 0xC0; p++) {
        bool ra = iie && m->ramrd, wa = iie && m->ramwrt;
        if (iie && m->store80) {
            if (p >= 0x04 && p < 0x08) ra = wa = m->page2;
            else if (m->hires && p >= 0x20 && p < 0x40) ra = wa = m->page2;
        }
        c->page_r[p] = (ra ? m->aux : m->main) + (p << 8);
        c->page_w[p] = (wa ? m->aux : m->main) + (p << 8);
    }
    for (int p = 0xC0; p < 0xD0; p++) c->page_r[p] = NULL, c->page_w[p] = NULL;
    const bool lcaux = iie && m->altzp;
    for (int p = 0xD0; p < 0x100; p++) {
        uint8_t *lc = lc_ptr(m, (uint16_t)(p << 8), lcaux);
        if (m->lc_read_ram) c->page_r[p] = lc;
        else {
            const uint8_t *r = m->rom[p - 0xC0];
            c->page_r[p] = r ? r : ff_page;
        }
        c->page_w[p] = m->lc_write_ram ? lc : NULL;
    }
}

static void access_language_card(apple2 *m, uint16_t address)
{
    const int n = address & 0x0F;
    m->lc_use_bank2 = (n & 0x08) == 0;
    const int mode = n & 3;
    m->lc_read_ram = mode == 0 || mode == 3;
    if (mode & 1) {
        if (m->lc_prewrite) m->lc_write_ram = true;
        m->lc_prewrite = true;
    } else {
        m->lc_write_ram = false;
        m->lc_prewrite = false;
    }
    rebuild_map(m);
}

static uint8_t read_cx(apple2 *m, uint16_t address)
{
    const bool iie = m->iie;
    if (address == 0xCFFF) {
        m->c8rom = false;
        return iie ? rom_byte(m, address) : 0x00;
    }
    if (iie && m->intcxrom) {
        if (address >= 0xC300 && address < 0xC400) m->c8rom = true;
        return rom_byte(m, address);
    }
    if (iie && address >= 0xC300 && address < 0xC400 && !m->slotc3rom) {
        m->c8rom = true;
        return rom_byte(m, address);
    }
    if (address >= 0xC800) {
        if (iie && (m->intcxrom || m->c8rom)) return rom_byte(m, address);
        return 0x00;
    }
    const int slot = (address >> 8) & 0x0F;
    if (slot == 6 && m->disk_prom) return m->disk_prom[address & 0xFF];
    return 0x00;
}

static uint8_t paddle_bit(const apple2 *m, int n)
{
    // A 558 timer per paddle: bit 7 high for about 11 cycles per unit of the
    // paddle (0-255) after $C070 (PREAD counts in 11-cycle loops)
    const uint32_t elapsed = m->cycles - m->paddle_start;
    return elapsed < (uint32_t)m->paddle[n & 3] * 11 ? 0x80 : 0x00;
}

static void set_switch(apple2 *m, bool *sw, bool v, bool remap)
{
    if (*sw == v) return;
    *sw = v;
    if (remap) rebuild_map(m);
}

static uint8_t read_io(apple2 *m, uint16_t address)
{
    const uint16_t a = address & 0x00FF;
    if (a >= 0xE0 && a <= 0xEF) return diskii_read(&m->disk, (uint8_t)a);
    if (a >= 0x80 && a <= 0x8F) {
        access_language_card(m, address);
        return 0x00;
    }
    switch (a) {
    case 0x00: return m->keyboard;
    case 0x10: {
        const uint8_t strobe = m->keyboard;
        m->keyboard &= 0x7F;
        if (m->iie) return (uint8_t)((m->any_key ? 0x80 : 0x00) | (strobe & 0x7F));
        return strobe;
    }
    case 0x11: return m->lc_use_bank2 ? 0x80 : 0x00;
    case 0x12: return m->lc_read_ram ? 0x80 : 0x00;
    case 0x13: return m->ramrd ? 0x80 : 0x00;
    case 0x14: return m->ramwrt ? 0x80 : 0x00;
    case 0x15: return m->intcxrom ? 0x80 : 0x00;
    case 0x16: return m->altzp ? 0x80 : 0x00;
    case 0x17: return m->slotc3rom ? 0x80 : 0x00;
    case 0x18: return m->store80 ? 0x80 : 0x00;
    case 0x19: return m->scanline < 192 ? 0x80 : 0x00;   // RDVBLBAR: low in the vertical blank
    case 0x1A: return m->text ? 0x80 : 0x00;
    case 0x1B: return m->mixed ? 0x80 : 0x00;
    case 0x1C: return m->page2 ? 0x80 : 0x00;
    case 0x1D: return m->hires ? 0x80 : 0x00;
    case 0x1E: return m->altcharset ? 0x80 : 0x00;
    case 0x1F: return m->col80 ? 0x80 : 0x00;
    case 0x30: m->speaker = !m->speaker; return 0x00;
    case 0x50: m->text = false; return 0x00;
    case 0x51: m->text = true; return 0x00;
    case 0x52: m->mixed = false; return 0x00;
    case 0x53: m->mixed = true; return 0x00;
    case 0x54: set_switch(m, &m->page2, false, m->iie && m->store80); return 0x00;
    case 0x55: set_switch(m, &m->page2, true, m->iie && m->store80); return 0x00;
    case 0x56: set_switch(m, &m->hires, false, m->iie && m->store80); return 0x00;
    case 0x57: set_switch(m, &m->hires, true, m->iie && m->store80); return 0x00;
    case 0x5E: m->an3 = false; return 0x00;
    case 0x5F: m->an3 = true; return 0x00;
    case 0x61: return m->joyb[0] || m->held[A2K_OPEN_APPLE] ? 0x80 : 0x00;
    case 0x62: return m->joyb[1] || m->held[A2K_CLOSED_APPLE] ? 0x80 : 0x00;
    case 0x63: return 0x00;
    case 0x64: case 0x65: case 0x66: case 0x67: return paddle_bit(m, a - 0x64);
    default: break;
    }
    if (a >= 0x70 && a <= 0x7F) m->paddle_start = m->cycles;     // PTRIG: the four 558s
    return 0x00;
}

static void write_io(apple2 *m, uint16_t address, uint8_t value)
{
    const uint16_t a = address & 0x00FF;
    if (a >= 0x70 && a <= 0x7F) {
        m->paddle_start = m->cycles;
        return;
    }
    if (a >= 0xE0 && a <= 0xEF) {
        diskii_write(&m->disk, (uint8_t)a, value);
        return;
    }
    if (a >= 0x80 && a <= 0x8F) {
        access_language_card(m, address);
        return;
    }
    if (m->iie && a <= 0x0F) {
        const bool on = a & 1;
        switch (a >> 1) {
        case 0: set_switch(m, &m->store80, on, true); return;
        case 1: set_switch(m, &m->ramrd, on, true); return;
        case 2: set_switch(m, &m->ramwrt, on, true); return;
        case 3: m->intcxrom = on; return;
        case 4: set_switch(m, &m->altzp, on, true); return;
        case 5: m->slotc3rom = on; return;
        case 6: m->col80 = on; return;
        default: m->altcharset = on; return;
        }
    }
    switch (a) {
    case 0x10: m->keyboard &= 0x7F; break;
    case 0x30: m->speaker = !m->speaker; break;
    case 0x50: m->text = false; break;
    case 0x51: m->text = true; break;
    case 0x52: m->mixed = false; break;
    case 0x53: m->mixed = true; break;
    case 0x54: set_switch(m, &m->page2, false, m->iie && m->store80); break;
    case 0x55: set_switch(m, &m->page2, true, m->iie && m->store80); break;
    case 0x56: set_switch(m, &m->hires, false, m->iie && m->store80); break;
    case 0x57: set_switch(m, &m->hires, true, m->iie && m->store80); break;
    case 0x5E: m->an3 = false; break;
    case 0x5F: m->an3 = true; break;
    default: break;
    }
}

// (only $C000-$CFFF and the write-protected language card come here)
static uint8_t cpu_read(void *user, uint16_t address)
{
    apple2 *m = (apple2 *)user;
    if (address < 0xC100) return read_io(m, address);
    if (address < 0xD000) return read_cx(m, address);
    return rom_byte(m, address);
}

static void cpu_write(void *user, uint16_t address, uint8_t value)
{
    apple2 *m = (apple2 *)user;
    if (address < 0xC100) write_io(m, address, value);
    else if (address == 0xCFFF) m->c8rom = false;
}

static void cpu_cycles(void *user, int cycles)
{
    apple2 *m = (apple2 *)user;
    m->cycles += (uint32_t)cycles;
    diskii_tick(&m->disk, cycles);
    if (m->turbo) return;
    // the speaker's level averaged over each output sample
    int left = cycles;
    const int32_t level = m->speaker ? 1 : -1;
    while (left > 0) {
        const int64_t need = ((int64_t)A2_CLOCK - m->audio_acc + m->audio_rate - 1) / m->audio_rate;
        const int step = need < left ? (int)need : left;
        m->level_sum += level * step;
        m->level_cycles += step;
        m->audio_acc += (int64_t)step * m->audio_rate;
        left -= step;
        if (m->audio_acc >= A2_CLOCK) {
            m->audio_acc -= A2_CLOCK;
            const float in = 5000.0f * (float)m->level_sum / (float)(m->level_cycles ? m->level_cycles : 1);
            const float out = in - m->hp_in + 0.995f * m->hp_out;
            m->hp_in = in;
            m->hp_out = out;
            m->level_sum = m->level_cycles = 0;
            if (m->naudio < A2_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)out;
        }
    }
}

// ---------------------------------------------------------------------------
// The keyboard's encoder
static uint8_t encode(const apple2 *m, int code)
{
    const bool shift = m->held[A2K_SHIFT] || m->held[A2K_RSHIFT], ctrl = m->held[A2K_CTRL];
    if (code >= A2K_A && code <= A2K_A + 25) {
        const int i = code - A2K_A;
        if (ctrl) return (uint8_t)(i + 1);
        if (!m->iie) {
            if (shift && code == 'N') return '^';
            if (shift && code == 'M') return ']';
            if (shift && code == 'P') return '@';
            return (uint8_t)('A' + i);
        }
        return (uint8_t)((m->caps_lock || shift ? 'A' : 'a') + i);
    }
    switch (code) {
    case A2K_RETURN: return 0x0D;
    case A2K_ESC: return 0x1B;
    case A2K_SPACE: return 0x20;
    case A2K_LEFT: return 0x08;
    case A2K_RIGHT: return 0x15;
    case A2K_UP: return 0x0B;
    case A2K_DOWN: return 0x0A;
    case A2K_TAB: return 0x09;
    case A2K_DELETE: return 0x7F;
    default: break;
    }
    if (!m->iie) {
        // the ][ / ][+ keyboard: its own symbols on the shifted keys
        static const char shifted_digit[10] = {'0', '!', '"', '#', '$', '%', '&', '\'', '(', ')'};
        if (code >= A2K_0 && code <= A2K_0 + 9) return (uint8_t)(shift ? shifted_digit[code - A2K_0] : code);
        switch (code) {
        case A2K_COLON: return shift ? '*' : ':';
        case A2K_MINUS: return shift ? '=' : '-';
        case A2K_SEMICOLON: return shift ? '+' : ';';
        case A2K_COMMA: return shift ? '<' : ',';
        case A2K_PERIOD: return shift ? '>' : '.';
        case A2K_SLASH: return shift ? '?' : '/';
        default: break;          // (keys the ][ has not: as on the //e)
        }
    }
    static const char shifted_us[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
    if (code >= A2K_0 && code <= A2K_0 + 9) {
        if (ctrl && code == '2') return 0x00;
        if (ctrl && code == '6') return 0x1E;
        return (uint8_t)(shift ? shifted_us[code - A2K_0] : code);
    }
    switch (code) {
    case A2K_MINUS: return ctrl ? 0x1F : shift ? '_' : '-';
    case A2K_EQUALS: return shift ? '+' : '=';
    case A2K_LBRACKET: return ctrl ? 0x1B : shift ? '{' : '[';
    case A2K_RBRACKET: return ctrl ? 0x1D : shift ? '}' : ']';
    case A2K_BACKSLASH: return ctrl ? 0x1C : shift ? '|' : '\\';
    case A2K_SEMICOLON: return shift ? ':' : ';';
    case A2K_COLON: return shift ? '*' : ':';
    case A2K_QUOTE: return shift ? '"' : '\'';
    case A2K_COMMA: return shift ? '<' : ',';
    case A2K_PERIOD: return shift ? '>' : '.';
    case A2K_SLASH: return shift ? '?' : '/';
    case A2K_GRAVE: return shift ? '~' : '`';
    default: return 0;
    }
}

static bool is_modifier(int code)
{
    return code == A2K_CTRL || code == A2K_SHIFT || code == A2K_RSHIFT || code == A2K_CAPS ||
           code == A2K_OPEN_APPLE || code == A2K_CLOSED_APPLE || code == A2K_RESET || code == A2K_REPT;
}

static void strobe(apple2 *m, uint8_t ch)
{
    uint8_t c = ch & 0x7F;
    if (!m->iie && c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);     // (no lower case)
    m->keyboard = (uint8_t)(c | 0x80);
}

static void update_any_key(apple2 *m)
{
    bool any = m->rep_code >= 0x100;
    for (int i = 0; i < A2K_COUNT && !any; i++)
        if (m->held[i] && !is_modifier(i)) any = true;
    m->any_key = any;
}

void apple2_key(apple2 *m, int code, bool down)
{
    if (code < 0 || code >= A2K_COUNT) return;
    const bool was = m->held[code];
    m->held[code] = down;
    if (down && !was) {
        if (code == A2K_CAPS) m->caps_lock = !m->caps_lock;
        else if (code == A2K_RESET) {
            if (!m->iie || m->held[A2K_CTRL]) apple2_reset_key(m);
        } else if (code == A2K_CTRL && m->held[A2K_RESET] && m->iie) {
            apple2_reset_key(m);
        } else if (!is_modifier(code)) {
            const uint8_t ch = encode(m, code);
            if (ch || code == '2') {
                strobe(m, ch);
                m->rep_code = code;
                m->rep_frames = 32;          // //e: about half a second, then 15 a second
            }
        }
    } else if (!down && was && code == m->rep_code) {
        m->rep_code = -1;
    }
    update_any_key(m);
}

void apple2_char(apple2 *m, uint8_t ascii)
{
    strobe(m, ascii);
    m->rep_code = 0x100 | ascii;
    m->rep_frames = 32;
    m->any_key = true;
}

void apple2_char_up(apple2 *m)
{
    if (m->rep_code >= 0x100) m->rep_code = -1;
    update_any_key(m);
}

void apple2_keys_clear(apple2 *m)
{
    memset(m->held, 0, sizeof m->held);
    m->rep_code = -1;
    m->any_key = false;
}

bool apple2_caps_lock(const apple2 *m) { return m->caps_lock; }

static void key_repeat(apple2 *m)
{
    if (m->rep_code < 0) return;
    // the //e repeats by itself; the ][s while REPT is held too
    if (!m->iie && !m->held[A2K_REPT]) {
        m->rep_frames = 6;
        return;
    }
    if (--m->rep_frames > 0) return;
    m->rep_frames = m->iie ? 4 : 6;
    if (m->rep_code >= 0x100) strobe(m, (uint8_t)m->rep_code);
    else strobe(m, encode(m, m->rep_code));
}

void apple2_joy(apple2 *m, uint8_t bits)
{
    m->paddle[0] = (bits & A2_JOY_LEFT) ? 0 : (bits & A2_JOY_RIGHT) ? 255 : 127;
    m->paddle[1] = (bits & A2_JOY_UP) ? 0 : (bits & A2_JOY_DOWN) ? 255 : 127;
    m->joyb[0] = (bits & A2_JOY_B0) != 0;
    m->joyb[1] = (bits & A2_JOY_B1) != 0;
}

void apple2_paddle(apple2 *m, int n, uint8_t value) { m->paddle[n & 3] = value; }
void apple2_set_turbo(apple2 *m, bool on) { m->turbo = on; }

// ---------------------------------------------------------------------------
apple2 *apple2_create(int model, int audio_rate)
{
    apple2 *m = (apple2 *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->model = model;
    m->iie = model == A2_IIE || model == A2_IIEE;
    if (m->iie) {
        m->aux = (uint8_t *)calloc(1, 0x10000);
        m->aux_lc_bank2 = (uint8_t *)calloc(1, 0x1000);
        if (!m->aux || !m->aux_lc_bank2) {
            apple2_destroy(m);
            return NULL;
        }
    }
    m->audio_rate = audio_rate > 0 ? audio_rate : A2_AUDIO_RATE;
    m->cpu.read = cpu_read;
    m->cpu.write = cpu_write;
    m->cpu.cycles = cpu_cycles;
    m->cpu.user = m;
    m->cpu.cmos = model == A2_IIEE;
    diskii_init(&m->disk);
    for (int i = 0; i < 4; i++) m->paddle[i] = 127;
    m->rep_code = -1;
    return m;
}

void apple2_destroy(apple2 *m)
{
    if (!m) return;
    diskii_free(&m->disk);
    free(m->aux);
    free(m->aux_lc_bank2);
    free(m);
}

void apple2_set_roms(apple2 *m, const uint8_t *main, uint32_t main_len, const uint8_t *chr, uint32_t chr_len,
                     const uint8_t *disk, const uint8_t *aid)
{
    for (int i = 0; i < 64; i++) m->rom[i] = NULL;
    if (m->iie) {
        // $C000-$FFFF (16 KB; a 12 KB image is $D000-$FFFF)
        const uint32_t base = main_len >= 0x4000 ? 0xC000 : 0x10000 - (main_len & ~0xFFu);
        for (uint32_t o = 0; o + 256 <= main_len && base + o < 0x10000; o += 256)
            m->rom[((base + o) >> 8) - 0xC0] = main + o;
    } else {
        const uint32_t n = main_len > 0x3000 ? 0x3000 : main_len;
        const uint32_t base = 0x10000 - (n & ~0xFFu);
        for (uint32_t o = 0; o + 256 <= n; o += 256) m->rom[((base + o) >> 8) - 0xC0] = main + o;
        if (aid)
            for (int i = 0; i < 8; i++) m->rom[0x10 + i] = aid + i * 256;
    }
    m->chargen = chr;
    m->chargen_size = chr ? (int)(chr_len > 0x1000 ? 0x1000 : chr_len) : 0;
    m->disk_prom = disk;
}

static void soft_switches_reset(apple2 *m)
{
    m->store80 = m->ramrd = m->ramwrt = m->intcxrom = m->altzp = m->slotc3rom = false;
    m->col80 = m->altcharset = false;
    m->c8rom = false;
    m->lc_read_ram = false;
    m->lc_write_ram = false;
    m->lc_use_bank2 = true;
    m->lc_prewrite = false;
}

void apple2_reset(apple2 *m)
{
    memset(m->main, 0, sizeof m->main);
    memset(m->lc_bank2, 0, sizeof m->lc_bank2);
    if (m->aux) memset(m->aux, 0, 0x10000);
    if (m->aux_lc_bank2) memset(m->aux_lc_bank2, 0, 0x1000);
    m->text = true;
    m->mixed = m->page2 = m->hires = false;
    m->an3 = true;
    soft_switches_reset(m);
    m->caps_lock = true;
    m->speaker = false;
    m->keyboard = 0;
    apple2_keys_clear(m);
    m->scanline = 0;
    m->naudio = 0;
    m->audio_acc = 0;
    m->level_sum = m->level_cycles = 0;
    m->hp_in = m->hp_out = 0;
    diskii_reset(&m->disk);
    m->cpu.cmos = m->model == A2_IIEE;
    rebuild_map(m);
    m65xx_set_irq(&m->cpu, M65_CLEAR);
    m65xx_set_nmi(&m->cpu, M65_CLEAR);
    m65xx_reset(&m->cpu);
}

void apple2_reset_key(apple2 *m)
{
    // RESET: the 6502's line; the //e's MMU and the language card go back
    // to their power-on state (as MAME's), the RAM stays
    if (m->iie) soft_switches_reset(m);
    else {
        m->lc_read_ram = false;
        m->lc_write_ram = false;
        m->lc_use_bank2 = true;
        m->lc_prewrite = false;
    }
    rebuild_map(m);
    m65xx_reset(&m->cpu);
}

int apple2_run_frame(apple2 *m, const int16_t **audio)
{
    m->naudio = 0;
    key_repeat(m);
    for (int line = 0; line < LINES; line++) {
        m->scanline = line;
        m65xx_run(&m->cpu, CYCLES_PER_LINE);
    }
    m->flash_phase++;
    if (audio) *audio = m->audio;
    return m->naudio;
}

// ---------------------------------------------------------------------------
// Video (dsp-cpp's Apple2Video, a line at a time)
static const uint32_t lores_rgb[16] = {
    0x000000, 0x9D09AA, 0x2B2BE3, 0xDB42FF, 0x119D00, 0x808080, 0x0EA1FF, 0xAAAAFF,
    0x885500, 0xFF6600, 0xAAAAAA, 0xFF99FF, 0x22FF00, 0xFFFF00, 0x44FF99, 0xFFFFFF,
};
static const uint32_t dhgr_rgb[16] = {
    0x000000, 0xDD0033, 0x000099, 0xDD22DD, 0x007722, 0x555555, 0x2222FF, 0x66AAFF,
    0x885500, 0xFF6600, 0xAAAAAA, 0xFF99AA, 0x00DD00, 0xFFFF00, 0x44FF99, 0xFFFFFF,
};
#define C_BLACK 0x000000
#define C_WHITE 0xFFFFFF
#define C_GREEN 0x20C000
#define C_PURPLE 0xC000FF
#define C_ORANGE 0xF06000
#define C_BLUE 0x2020FF

static inline uint16_t text_address(int row, int col, bool page2)
{
    return (uint16_t)((page2 ? 0x0800 : 0x0400) + ((row & 7) << 7) + ((row >> 3) * 40) + col);
}

static inline uint16_t hires_address(int y, int col, bool page2)
{
    return (uint16_t)((page2 ? 0x4000 : 0x2000) + ((y & 7) << 10) + (((y >> 3) & 7) << 7) + ((y >> 6) * 40) + col);
}

typedef struct {
    bool text, mixed, page2, hires, col80, altcharset, dhires, iie, flash;
    const uint8_t *main, *aux, *chargen;
    int chargen_size;
} vstate;

static vstate video_state(const apple2 *m)
{
    vstate v;
    v.text = m->text;
    v.mixed = m->mixed;
    // With 80STORE off PAGE2 picks the page shown, in 80 columns and double
    // hi-res too (with 80STORE on it banks main / aux instead)
    v.page2 = m->page2 && !m->store80;
    v.hires = m->hires;
    v.col80 = m->col80;
    v.altcharset = m->altcharset;
    v.iie = m->iie;
    v.dhires = m->iie && !m->an3 && m->col80;
    v.flash = ((m->flash_phase / 15) & 1) != 0;
    v.main = m->main;
    v.aux = m->aux;
    v.chargen = m->chargen;
    v.chargen_size = m->chargen_size;
    return v;
}

static uint8_t glyph_row(const vstate *v, uint8_t code, int row)
{
    if (!v->chargen || v->chargen_size <= 0) return 0x7F;
    unsigned index = code, invert_mask = v->iie ? 0x7Fu : 0x00u;
    if (v->iie) {
        if (!v->altcharset) {
            if (code >= 0x40 && code <= 0x7F) {
                index = code & 0x3F;
                if (v->flash) invert_mask ^= 0x7F;
            }
        } else if (code >= 0x60 && code <= 0x7F) {
            index = code | 0x80;
            invert_mask ^= 0x7F;
        }
    } else {
        if (code >= 0x40 && code <= 0x7F) {
            if (v->flash) invert_mask ^= 0x7F;
        } else if (code < 0x40) {
            invert_mask ^= 0x7F;
        }
    }
    unsigned bits = (v->chargen[(index * 8 + (unsigned)(row & 7)) % (unsigned)v->chargen_size] & 0x7F) ^ invert_mask;
    if (!v->iie) {
        // the ][ / ][+ character ROM is stored mirrored against the //e's
        unsigned r = 0;
        for (int i = 0; i < 7; i++)
            if (bits & (1u << i)) r |= 1u << (6 - i);
        bits = r;
    }
    return (uint8_t)bits;
}

static void text_line(const vstate *v, int y, uint32_t *out)
{
    const int row = y >> 3, gy = y & 7;
    if (v->col80) {
        for (int col = 0; col < 80; col++) {
            const uint16_t addr = text_address(row, col >> 1, v->page2);
            const uint8_t code = (col & 1) == 0 ? (v->aux ? v->aux[addr] : 0xA0) : v->main[addr];
            const uint8_t bits = glyph_row(v, code, gy);
            for (int x = 0; x < 7; x++) out[col * 7 + x] = (bits >> x) & 1 ? C_WHITE : C_BLACK;
        }
    } else {
        for (int col = 0; col < 40; col++) {
            const uint8_t bits = glyph_row(v, v->main[text_address(row, col, v->page2)], gy);
            for (int x = 0; x < 7; x++) out[col * 14 + x * 2] = out[col * 14 + x * 2 + 1] = (bits >> x) & 1 ? C_WHITE : C_BLACK;
        }
    }
}

static void lores_line(const vstate *v, int y, uint32_t *out)
{
    const int row = y >> 3;
    for (int col = 0; col < 40; col++) {
        const uint8_t cell = v->main[text_address(row, col, v->page2)];
        const uint32_t c = lores_rgb[(y & 7) < 4 ? (cell & 0x0F) : (cell >> 4)];
        for (int x = 0; x < 14; x++) out[col * 14 + x] = c;
    }
}

static void hires_line(const vstate *v, int y, uint32_t *out)
{
    int prev = 0;
    for (int col = 0; col < 40; col++) {
        const uint8_t b = v->main[hires_address(y, col, v->page2)];
        const int palette = (b >> 7) & 1;
        for (int bit = 0; bit < 7; bit++) {
            const int pix = (b >> bit) & 1;
            const int next_bit = bit < 6 ? ((b >> (bit + 1)) & 1)
                                         : (col < 39 ? (v->main[hires_address(y, col + 1, v->page2)] & 1) : 0);
            const int x = col * 7 + bit;
            uint32_t c = C_BLACK;
            if (pix) {
                if (prev || next_bit) c = C_WHITE;
                else if (palette) c = (x & 1) ? C_BLUE : C_ORANGE;
                else c = (x & 1) ? C_PURPLE : C_GREEN;
            }
            out[x * 2] = out[x * 2 + 1] = c;
            prev = pix;
        }
    }
}

static void dhires_line(const vstate *v, int y, uint32_t *out)
{
    int bits = 0, count = 0, x = 0;
    for (int col = 0; col < 40; col++) {
        const uint16_t addr = hires_address(y, col, v->page2);
        const uint8_t a = v->aux ? v->aux[addr] : 0, mm = v->main[addr];
        for (int i = 0; i < 7; i++) bits |= ((a >> i) & 1) << count++;
        for (int i = 0; i < 7; i++) bits |= ((mm >> i) & 1) << count++;
        // 14 dots per column (aux then main), 560 a line: four dots are one
        // colour, the pattern rotated left by one (MAME's rotl4(n, 1))
        while (count >= 4 && x < 560) {
            const int nibble = bits & 0x0F;
            bits >>= 4;
            count -= 4;
            const uint32_t c = dhgr_rgb[((nibble << 1) | (nibble >> 3)) & 0x0F];
            for (int i = 0; i < 4 && x < 560; i++, x++) out[x] = c;
        }
    }
}

void apple2_render_row(const apple2 *m, int y, uint32_t *out)
{
    if (y < 0 || y >= A2_H) return;
    const vstate v = video_state(m);
    if (v.text || (v.mixed && y >= 160)) text_line(&v, y, out);
    else if (v.hires) {
        if (v.dhires) dhires_line(&v, y, out);
        else hires_line(&v, y, out);
    } else lores_line(&v, y, out);
}

bool apple2_is_80col(const apple2 *m) { return m->iie && m->col80; }

void apple2_set_rgb565(apple2 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b)) { m->rgb565 = fn; }

void apple2_render_row565(const apple2 *m, int y, uint16_t *out)
{
    uint32_t line[A2_W];
    apple2_render_row(m, y, line);
    for (int x = 0; x < A2_W; x++) {
        const uint32_t c = line[x];
        const uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
        out[x] = m->rgb565 ? m->rgb565(r, g, b) : (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// ---------------------------------------------------------------------------
// Media
const char *apple2_load_pbuf(apple2 *m, zx_pbuf *pb, const char *name)
{
    return diskii_load_pbuf(&m->disk, pb, name);
}

const char *apple2_load_file(apple2 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t buf[4096];
    size_t n;
    bool ok = true;
    while (ok && (n = fread(buf, 1, sizeof buf, f)) > 0) ok = zx_pb_append(&pb, buf, n);
    fclose(f);
    if (!ok || !zx_pb_finish(&pb)) {
        zx_pb_free(&pb);
        return "out of memory";
    }
    return apple2_load_pbuf(m, &pb, path);
}

void apple2_eject(apple2 *m) { diskii_eject(&m->disk); }
bool apple2_has_disk_card(const apple2 *m) { return m->disk_prom != NULL; }
bool apple2_disk_loaded(const apple2 *m) { return m->disk.loaded; }
bool apple2_drive_led(const apple2 *m) { return m->disk.motor_on; }
