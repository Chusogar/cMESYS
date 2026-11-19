// samcoupe_core.c - MGT SAM Coupe (see samcoupe_core.h).
#include "samcoupe_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80dsp/z80dsp.h"
#include "saa1099.h"
#include "sam_disk.h"
#include "sam_tape.h"
#include "wd1772.h"

#define T_LINE 384
#define LINES 312
#define T_FRAME (T_LINE * LINES)     // 119808
#define TOP_BORDER 68
#define SCREEN_LINES 192
#define SIDE_CELLS 8
#define SCREEN_CELLS 32
#define INT_ACTIVE 128
#define PAGES 32
#define SAA_CLOCK 8000000

enum { SECT_ROM0 = -1, SECT_ROM1 = -2, SECT_NONE = -3 };

static const uint8_t kDefaultClut[16] = { 0, 16, 32, 48, 64, 80, 96, 120, 0, 17, 34, 51, 68, 85, 102, 127 };

static struct {
    zd_z80 cpu;
    saa1099 saa;
    sam_disk disk[2];
    sam_tape tape;
    wd1772 fdc;
    uint8_t *ram;                   // PAGES x 16 KB
    uint8_t rom[0x8000];
    uint8_t *fb;                    // SAM_W x SAM_H, the colour (0-127) of each dot
    uint8_t lmpr, hmpr, vmpr, border, lepr, hepr, status, line_int;
    int line_int_active;
    int section_page[4];
    uint8_t clut[16];
    uint8_t border_line[T_LINE];    // the border colour at every T-state of the line
    bool keys[SK_COUNT], keys_user[SK_COUNT];
    int boot_frames;
    long frames;                    // since the reset
    uint8_t joy[2];
    uint8_t key_matrix[9];
    bool flash_phase;
    int flash_count;
    int line, t_in_line, frame_t;
    int16_t audio[SAM_AUDIO_MAX];
    int naudio;
    int64_t audio_acc;
    unsigned last_activity;
    int busy_frames;
} S;

static uint32_t argb[128];
static uint16_t c565[128];

static void make_palette(void)
{
    for (int v = 0; v < 128; v++) {
        const int bright = (v >> 3) & 1;
#define LEVEL(lo, hi) ((((((v >> (hi)) & 1) << 2) | (((v >> (lo)) & 1) << 1) | bright) * 255 + 3) / 7)
        const uint32_t r = (uint32_t)LEVEL(1, 5), g = (uint32_t)LEVEL(2, 6), b = (uint32_t)LEVEL(0, 4);
#undef LEVEL
        argb[v] = 0xff000000u | (r << 16) | (g << 8) | b;
        c565[v] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// ---- memory -----------------------------------------------------------------

static void update_paging(void)
{
    S.section_page[0] = (S.lmpr & 0x20) ? (S.lmpr & 0x1f) : SECT_ROM0;
    S.section_page[1] = (S.lmpr + 1) & 0x1f;
    if (S.hmpr & 0x80) {            // external memory: none fitted
        S.section_page[2] = SECT_NONE;
        S.section_page[3] = (S.lmpr & 0x40) ? SECT_ROM1 : SECT_NONE;
    } else {
        S.section_page[2] = S.hmpr & 0x1f;
        S.section_page[3] = (S.lmpr & 0x40) ? SECT_ROM1 : ((S.hmpr + 1) & 0x1f);
    }
}

static uint8_t mem_read(void *u, uint16_t addr)
{
    (void)u;
    const int page = S.section_page[addr >> 14];
    if (page >= 0) return S.ram[((uint32_t)page << 14) | (addr & 0x3fff)];
    if (page == SECT_ROM0) return S.rom[addr & 0x3fff];
    if (page == SECT_ROM1) return S.rom[0x4000 | (addr & 0x3fff)];
    return 0xff;
}

static void mem_write(void *u, uint16_t addr, uint8_t value)
{
    (void)u;
    const int slot = addr >> 14;
    const int page = S.section_page[slot];
    if (page < 0) return;
    if (slot == 0 && (S.lmpr & 0x80)) return;        // WPRAM
    S.ram[((uint32_t)page << 14) | (addr & 0x3fff)] = value;
}

// ---- keyboard ---------------------------------------------------------------

static void build_matrix(void)
{
    memset(S.key_matrix, 0xff, sizeof S.key_matrix);
    for (int k = 0; k < SK_COUNT; k++)
        if (S.keys[k]) S.key_matrix[k >> 3] &= (uint8_t)~(1u << (k & 7));
    // joystick 1 = 6 7 8 9 0 (left right down up fire), 2 = 1 2 3 4 5
    static const uint8_t j1[5] = { SK_7, SK_6, SK_8, SK_9, SK_0 };   // right left down up fire
    static const uint8_t j2[5] = { SK_2, SK_1, SK_3, SK_4, SK_5 };
    for (int b = 0; b < 5; b++) {
        if (S.joy[0] & (1 << b)) S.key_matrix[j1[b] >> 3] &= (uint8_t)~(1u << (j1[b] & 7));
        if (S.joy[1] & (1 << b)) S.key_matrix[j2[b] >> 3] &= (uint8_t)~(1u << (j2[b] & 7));
    }
}

void samcoupe_key(int code, bool down)
{
    if (code < 0 || code >= SK_COUNT || S.keys_user[code] == down) return;
    S.keys_user[code] = down;
    S.keys[code] = down;
    build_matrix();
}

void samcoupe_keys_clear(void)
{
    memset(S.keys, 0, sizeof S.keys);
    memset(S.keys_user, 0, sizeof S.keys_user);
    build_matrix();
}

void samcoupe_joy(int port, uint8_t bits)
{
    if (port < 0 || port > 1 || S.joy[port] == bits) return;
    S.joy[port] = bits;
    build_matrix();
}

static uint8_t keys_for(uint8_t high)
{
    uint8_t keys = 0xff;
    for (int c = 0; c < 8; c++)
        if (!(high & (1 << c))) keys &= S.key_matrix[c];
    return keys;
}

// ---- I/O --------------------------------------------------------------------

static int select_disk_port(uint16_t port)
{
    const int low = port & 0xff;
    int base = -1;
    if (low >= 224 && low <= 231) base = 224;
    else if (low >= 240 && low <= 247) base = 240;
    if (base < 0) return -1;
    const int off = low - base;
    wd1772_set_disk(&S.fdc, &S.disk[base == 224 ? 0 : 1]);
    wd1772_set_side(&S.fdc, off >= 4);
    return off & 3;
}

static uint8_t io_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t low = (uint8_t)port, high = (uint8_t)(port >> 8);
    uint8_t r = 0xff;
    if (low == 249) {                                  // STATUS + keys K6-K8
        r = (uint8_t)((S.status & 0x1f) | (keys_for(high) & 0xe0));
    } else if (low == 254) {                           // keys K1-K5, EAR
        uint8_t keys = keys_for(high);
        if (high == 0xff) keys &= S.key_matrix[8];
        uint8_t hb = 0xe0;
        if (sam_tape_inserted(&S.tape) && !S.tape.ear) hb &= (uint8_t)~0x40;
        r = (uint8_t)((keys & 0x1f) | hb);
    } else if (low == 252) {
        r = S.vmpr;
    } else if (low == 251) {
        r = S.hmpr;
    } else if (low == 250) {
        r = S.lmpr;
    } else if (low == 248) {
        if (port & 0x0100) {                           // HPEN: the line drawn
            const int y = S.line - TOP_BORDER;
            r = (y >= 0 && y < SCREEN_LINES) ? (uint8_t)y : (uint8_t)SCREEN_LINES;
        } else {                                       // LPEN
            int x = S.t_in_line - 2 * SIDE_CELLS * 8;
            if (x < 0) x = 0;
            r = (uint8_t)(((uint8_t)x & 0xfc) | (S.border & 1));
        }
    } else if (low == 255) {
        r = 0xff;
    } else {
        const int off = select_disk_port(port);
        if (off == 0) r = wd1772_status_r(&S.fdc);
        else if (off == 1) r = wd1772_track_r(&S.fdc);
        else if (off == 2) r = wd1772_sector_r(&S.fdc);
        else if (off == 3) r = wd1772_data_r(&S.fdc);
    }
    return r;
}

static void io_out(void *u, uint16_t port, uint8_t value)
{
    (void)u;
    const uint8_t low = (uint8_t)port;
    if (low == 0xff) {                                 // SAA1099: 0x1FF address, 0x0FF data
        if (port & 0x100) saa1099_select(&S.saa, value);
        else saa1099_write(&S.saa, value);
        return;
    }
    if (low == 253) {
        // MIDI OUT
    } else if (low == 254) {
        S.border = value;
    } else if (low == 249) {                           // the line interrupt's line; acks it
        S.line_int = value;
        S.status |= 0x01;
        if (S.status & 0x08) zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
    } else if (low == 252) {
        S.vmpr = value & 0x7f;
    } else if (low == 251) {
        S.hmpr = value;
        update_paging();
    } else if (low == 128) {
        S.lepr = value;
        update_paging();
    } else if (low == 129) {
        S.hepr = value;
        update_paging();
    } else if (low == 250) {
        S.lmpr = value;
        update_paging();
    } else if (low == 0xf8) {                          // CLUT (register = port bits 8-11)
        const int i = port >> 8;
        if (i < 16) S.clut[i] = value & 0x7f;
    } else {
        const int off = select_disk_port(port);
        if (off == 0) wd1772_command_w(&S.fdc, value);
        else if (off == 1) wd1772_track_w(&S.fdc, value);
        else if (off == 2) wd1772_sector_w(&S.fdc, value);
        else if (off == 3) wd1772_data_w(&S.fdc, value);
    }
}

// ---- video ------------------------------------------------------------------

static inline uint8_t border_index(void) { return (uint8_t)(((S.border & 0x20) >> 2) | (S.border & 0x07)); }

static void render_attr(uint8_t *p, const uint8_t *page, uint32_t data_base, uint32_t attr_base)
{
    for (int cell = 0; cell < SCREEN_CELLS; cell++) {
        const uint8_t data = page[data_base + cell], attr = page[attr_base + cell];
        int ink = ((attr >> 3) & 8) | (attr & 7), paper = (attr >> 3) & 0xf;
        if (S.flash_phase && (attr & 0x80)) { const int t = ink; ink = paper; paper = t; }
        const uint8_t ci = S.clut[ink], cp = S.clut[paper];
        for (int b = 0; b < 8; b++) p[b * 2] = p[b * 2 + 1] = (data & (0x80 >> b)) ? ci : cp;
        p += 16;
    }
}

static inline uint8_t video_byte(int base_page, uint32_t offset)
{
    const int page = (base_page + (int)(offset >> 14)) & (PAGES - 1);
    return S.ram[((uint32_t)page << 14) | (offset & 0x3fff)];
}

static void render_line(int line)
{
    if (line < 0 || line >= LINES) return;
    uint8_t *dst = S.fb + (size_t)line * SAM_W;
    const bool screen = line >= TOP_BORDER && line < TOP_BORDER + SCREEN_LINES;
    for (int t = 0; t < T_LINE; t++) {
        if (screen && t == 64) t = 320;
        dst[t * 2] = dst[t * 2 + 1] = S.clut[S.border_line[t] & 0x0f];
    }
    if (!screen) return;
    const int y = line - TOP_BORDER;
    const int base = (S.vmpr & 0x1e) & (PAGES - 1);
    const uint8_t *page = S.ram + ((uint32_t)base << 14);
    uint8_t *p = dst + SIDE_CELLS * 16;
    switch ((S.vmpr >> 5) & 3) {
    case 0: {                                           // mode 1 (Spectrum layout)
        const int off = (y & 0xc0) | ((y << 3) & 0x38) | ((y >> 3) & 0x07);
        render_attr(p, page, (uint32_t)off << 5, 6144u + (uint32_t)((y & 0xf8) << 2));
        break;
    }
    case 1:                                             // mode 2
        render_attr(p, page, (uint32_t)y << 5, ((uint32_t)y << 5) + 0x2000);
        break;
    case 2: {                                           // mode 3: 2 bits a dot
        const uint8_t b48 = (uint8_t)((S.hmpr & 0x60) >> 3);
        const uint8_t m3[4] = { S.clut[b48 | 0], S.clut[b48 | 2], S.clut[b48 | 1], S.clut[b48 | 3] };
        const uint32_t lb = (uint32_t)y << 7;
        for (int i = 0; i < 128; i++) {
            const uint8_t d = video_byte(base, lb + (uint32_t)i);
            p[0] = m3[d >> 6];
            p[1] = m3[(d >> 4) & 3];
            p[2] = m3[(d >> 2) & 3];
            p[3] = m3[d & 3];
            p += 4;
        }
        break;
    }
    default: {                                          // mode 4: 4 bits a dot
        const uint32_t lb = (uint32_t)y << 7;
        for (int i = 0; i < 128; i++) {
            const uint8_t d = video_byte(base, lb + (uint32_t)i);
            p[0] = p[1] = S.clut[d >> 4];
            p[2] = p[3] = S.clut[d & 0xf];
            p += 4;
        }
        break;
    }
    }
}

void samcoupe_render_row(int y, uint32_t *out)
{
    const uint8_t *s = S.fb + (size_t)y * SAM_W;
    for (int x = 0; x < SAM_W; x++) out[x] = argb[s[x]];
}

void samcoupe_render_row565(int y, uint16_t *out)
{
    const uint8_t *s = S.fb + (size_t)y * SAM_W;
    for (int x = 0; x < SAM_W; x++) out[x] = c565[s[x]];
}

// ---- timing -----------------------------------------------------------------

static void end_of_line(void)
{
    if (S.line < LINES) {
        render_line(S.line);
        S.line++;
        if (S.line_int < 192 && S.line == TOP_BORDER + S.line_int) {
            S.status &= (uint8_t)~0x01;                 // LINE interrupt (active low)
            S.line_int_active = INT_ACTIVE;
            zd_set_irq(&S.cpu, ZD_HOLD, 0xff);
        }
    }
}

static void on_cycles(void *u, int cycles)
{
    (void)u;
    sam_tape_tick(&S.tape, cycles);
    if (S.line_int_active > 0) {
        S.line_int_active -= cycles;
        if (S.line_int_active <= 0) {
            S.line_int_active = 0;
            S.status |= 0x01;
            if (S.status & 0x08) zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
        }
    }
    const uint8_t col = border_index();
    int n = cycles;
    while (n > 0) {
        int chunk = T_LINE - S.t_in_line;
        if (chunk > n) chunk = n;
        if (S.line < LINES) memset(S.border_line + S.t_in_line, col, (size_t)chunk);
        S.t_in_line += chunk;
        S.frame_t += chunk;
        n -= chunk;
        if (S.t_in_line >= T_LINE) {
            S.t_in_line -= T_LINE;
            end_of_line();
        }
    }
    S.audio_acc += (int64_t)cycles * SAM_AUDIO_RATE;
    while (S.audio_acc >= SAM_CLOCK) {
        S.audio_acc -= SAM_CLOCK;
        int32_t l, r;
        saa1099_update(&S.saa, &l, &r);
        if (S.naudio < SAM_AUDIO_MAX) S.audio[S.naudio++] = (int16_t)((l + r) / 2);
    }
}

static void boot_keys(void);

int samcoupe_run_frame(const int16_t **audio)
{
    boot_keys();
    S.frames++;
    S.line = S.t_in_line = S.frame_t = 0;
    S.naudio = 0;
    const uint8_t start_col = border_index();
    wd1772_tick_frame(&S.fdc);
    S.status |= 0x01;
    S.line_int_active = 0;
    S.status &= (uint8_t)~0x08;                         // FRAME interrupt
    zd_set_irq(&S.cpu, ZD_HOLD, 0xff);
    int remaining = T_FRAME;
    while (remaining > 0) {
        const int ran = zd_run(&S.cpu, remaining < T_LINE ? remaining : T_LINE);
        if (ran <= 0) break;
        remaining -= ran;
        if (remaining < T_FRAME - 128) {
            S.status |= 0x08;
            if (S.status & 0x01) zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
        }
    }
    // the lines the CPU did not reach (the border as at the frame's start)
    if (S.line < LINES) {
        memset(S.border_line + S.t_in_line, start_col, (size_t)(T_LINE - S.t_in_line));
        while (S.line < LINES) {
            render_line(S.line++);
            memset(S.border_line, start_col, T_LINE);
        }
    }
    S.flash_count = (S.flash_count + 1) & 0x1f;
    if (S.flash_count == 0) S.flash_phase = !S.flash_phase;
    if (S.fdc.activity != S.last_activity) {
        S.last_activity = S.fdc.activity;
        S.busy_frames = 25;
    } else if (S.busy_frames) {
        S.busy_frames--;
    }
    *audio = S.audio;
    return S.naudio;
}

double samcoupe_fps(void) { return (double)SAM_CLOCK / (double)T_FRAME; }

// ---- set up -----------------------------------------------------------------

void samcoupe_reset(void)
{
    memset(S.ram, 0, (size_t)PAGES << 14);
    S.lmpr = S.hmpr = S.vmpr = 0;
    update_paging();
    S.border = 0;
    S.lepr = S.hepr = 0;
    S.status = 0xff;
    S.line_int = 0xff;
    S.line_int_active = 0;
    memcpy(S.clut, kDefaultClut, sizeof S.clut);
    S.flash_phase = false;
    S.flash_count = 0;
    S.frames = 0;
    S.boot_frames = 0;
    S.line = S.t_in_line = S.frame_t = 0;
    S.naudio = 0;
    S.audio_acc = 0;
    zd_reset(&S.cpu);
    saa1099_reset(&S.saa);
    wd1772_reset(&S.fdc);
    memset(S.fb, S.clut[0], (size_t)SAM_W * SAM_H);
}

bool samcoupe_init(const uint8_t *rom)
{
    make_palette();
    if (!S.ram) S.ram = (uint8_t *)malloc((size_t)PAGES << 14);
    if (!S.fb) S.fb = (uint8_t *)malloc((size_t)SAM_W * SAM_H);
    if (!S.ram || !S.fb) return false;
    memcpy(S.rom, rom, sizeof S.rom);
    zd_z80 *z = &S.cpu;
    memset(z, 0, sizeof *z);
    z->read = mem_read;
    z->write = mem_write;
    z->in = io_in;
    z->out = io_out;
    z->cycles = on_cycles;
    z->user = &S;
    zd_init(z);
    saa1099_init(&S.saa, SAA_CLOCK, SAM_AUDIO_RATE);
    memset(S.keys, 0, sizeof S.keys);
    memset(S.keys_user, 0, sizeof S.keys_user);
    memset(S.joy, 0, sizeof S.joy);
    build_matrix();
    samcoupe_reset();
    return true;
}

// ---- media ------------------------------------------------------------------

const char *samcoupe_insert_disk(int drive, const uint8_t *data, uint32_t size)
{
    if (drive < 0 || drive > 1) return "no such drive";
    return sam_disk_load(&S.disk[drive], data, size);
}

void samcoupe_eject_disk(int drive)
{
    if (drive >= 0 && drive <= 1) sam_disk_eject(&S.disk[drive]);
}

bool samcoupe_disk_inserted(int drive) { return drive >= 0 && drive <= 1 && S.disk[drive].loaded; }
bool samcoupe_disk_busy(void) { return S.busy_frames > 0; }
bool samcoupe_disk_dirty(int drive) { return drive >= 0 && drive <= 1 && S.disk[drive].dirty; }
bool samcoupe_disk_image(int drive, uint8_t *out) { return drive >= 0 && drive <= 1 && sam_disk_image(&S.disk[drive], out); }

const char *samcoupe_insert_tape(const uint8_t *data, uint32_t size) { return sam_tape_load(&S.tape, data, size); }
void samcoupe_eject_tape(void) { sam_tape_eject(&S.tape); }
bool samcoupe_tape_inserted(void) { return sam_tape_inserted(&S.tape); }
bool samcoupe_tape_playing(void) { return S.tape.playing; }

void samcoupe_tape_play(bool on)
{
    if (!on) { sam_tape_stop(&S.tape); return; }
    if (S.tape.index >= S.tape.n) sam_tape_rewind(&S.tape);
    sam_tape_play(&S.tape);
}

bool samcoupe_boot(void)
{
    if (!S.disk[0].loaded) return false;
    // as on the real one: F9 (the ROM's BOOT key); a RETURN first, as the
    // ROM takes the first key after power on only to clear its start screen
    S.boot_frames = 1;
    return true;
}

// The keys BOOT presses, frame by frame (RETURN, then F9)
static void boot_keys(void)
{
    if (!S.boot_frames || S.frames < 260) return;     // (the ROM reads no keys before its start screen)
    const int f = S.boot_frames++;
    const bool ret = f >= 1 && f <= 3, f9 = f >= 9 && f <= 14;
    S.keys[SK_RETURN] = ret ? true : S.keys_user[SK_RETURN];
    S.keys[SK_F9] = f9 ? true : S.keys_user[SK_F9];
    if (f > 14) S.boot_frames = 0;
    build_matrix();
}

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

bool samcoupe_is_tape(const char *name) { return ends_with(name, ".tap") || ends_with(name, ".tzx"); }

const char *samcoupe_load_file(const char *path, int drive)
{
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (16L << 20)) { fclose(f); return "not a SAM disk or tape"; }
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data) { fclose(f); return "out of memory"; }
    const bool ok = fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    const char *err = !ok ? "read error"
                    : samcoupe_is_tape(path) ? samcoupe_insert_tape(data, (uint32_t)size)
                    : samcoupe_insert_disk(drive, data, (uint32_t)size);
    free(data);
    return err;
}
