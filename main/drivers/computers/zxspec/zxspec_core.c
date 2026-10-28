// zxspec_core.c - ZX Spectrum 48K / 128K / +3 / Pentagon / Scorpion (C11).
// Practical port of dsp-cpp spectrum / spectrum_128k / spectrum_3 / zx_clone.
#include "zxspec_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80/z80.h"
#include "spectrum_tape.h"
#include "spectrum_snap.h"
#include "ay8910.h"

/* Full ULA picture 352x296; output is centre crop ZX_W x ZX_H. */
#define ZX_PAPER_X      48
#define ZX_PAPER_Y      48
#define ZX_CROP_X       16
#define ZX_CROP_Y       28
#define ZX_IRQ_HOLD_T   32
#define ZX_BANK_SIZE    0x4000
#define ZX_AY_CLOCK     1750000u
#define ZX_BEEPER_AMP   4096

_Static_assert(ZX_CROP_X + ZX_W <= 352, "crop width");
_Static_assert(ZX_CROP_Y + ZX_H <= 296, "crop height");
_Static_assert(ZX_PAPER_X + 256 <= 352 && ZX_PAPER_Y + 192 <= 296, "paper");

/* Classic Spectrum ink/paper 0..15 as 0x00RRGGBB. */
static const uint32_t k_palette[16] = {
    0x00000000, 0x000000CD, 0x00CD0000, 0x00CD00CD,
    0x0000CD00, 0x0000CDCD, 0x00CDCD00, 0x00CDCDCD,
    0x00000000, 0x000000FF, 0x00FF0000, 0x00FF00FF,
    0x0000FF00, 0x0000FFFF, 0x00FFFF00, 0x00FFFFFF,
};

/* spectrum.h kScrTable — pixel line offsets within the 6K bitmap. */
static const uint16_t k_scr_table[192] = {
    0,    256, 512, 768, 1024, 1280, 1536, 1792, 32,   288, 544, 800, 1056, 1312, 1568, 1824,
    64,   320, 576, 832, 1088, 1344, 1600, 1856, 96,   352, 608, 864, 1120, 1376, 1632, 1888,
    128,  384, 640, 896, 1152, 1408, 1664, 1920, 160,  416, 672, 928, 1184, 1440, 1696, 1952,
    192,  448, 704, 960, 1216, 1472, 1728, 1984, 224,  480, 736, 992, 1248, 1504, 1760, 2016,
    2048, 2304, 2560, 2816, 3072, 3328, 3584, 3840, 2080, 2336, 2592, 2848, 3104, 3360, 3616, 3872,
    2112, 2368, 2624, 2880, 3136, 3392, 3648, 3904, 2144, 2400, 2656, 2912, 3168, 3424, 3680, 3936,
    2176, 2432, 2688, 2944, 3200, 3456, 3712, 3968, 2208, 2464, 2720, 2976, 3232, 3488, 3744, 4000,
    2240, 2496, 2752, 3008, 3264, 3520, 3776, 4032, 2272, 2528, 2784, 3040, 3296, 3552, 3808, 4064,
    4096, 4352, 4608, 4864, 5120, 5376, 5632, 5888, 4128, 4384, 4640, 4896, 5152, 5408, 5664, 5920,
    4160, 4416, 4672, 4928, 5184, 5440, 5696, 5952, 4192, 4448, 4704, 4960, 5216, 5472, 5728, 5984,
    4224, 4480, 4736, 4992, 5248, 5504, 5760, 6016, 4256, 4512, 4768, 5024, 5280, 5536, 5792, 6048,
    4288, 4544, 4800, 5056, 5312, 5568, 5824, 6080, 4320, 4576, 4832, 5088, 5344, 5600, 5856, 6112
};

struct zxspec {
    zx_model_t model;
    z80 cpu;
    spectrum_tape tape;
    ay8910 ay;
    bool has_ay;

    uint8_t *ram[8];
    const uint8_t *rom48;
    const uint8_t *rom128;
    size_t rom128_bytes;

    /* 16K page map: slots 0..3. Slot 0 may be ROM (slot0_rom). */
    uint8_t page[4];
    bool slot0_rom;
    uint8_t rom_sel;
    uint8_t pantalla;
    uint8_t port_7ffd;
    uint8_t port_1ffd;
    bool paging_locked;
    bool special_paging;

    uint8_t border;
    uint8_t speaker;    /* bit6 echo on FE read: 0x00 / 0x40 */
    uint8_t ear;        /* tape EAR: 0x00 / 0x40 */
    uint8_t keys[8];
    uint8_t joy;
    bool flash;
    int flash_count;

    int16_t beeper_level;
    int64_t audio_acc;
    int16_t audio[ZX_AUDIO_MAX + 8];
    int naudio;
};

static bool is_128_class(zx_model_t m)
{
    return m == ZX_SPEC128 || m == ZX_PLUS3 || m == ZX_PENTAGON || m == ZX_SCORPION;
}

static void update_memory_map(zxspec *m)
{
    m->pantalla = (m->port_7ffd & 0x08) ? 7 : 5;
    m->special_paging = false;
    m->slot0_rom = true;

    if (m->model == ZX_SPEC48) {
        m->page[0] = 0;
        m->page[1] = 5;
        m->page[2] = 2;
        m->page[3] = 0;
        m->rom_sel = 0;
        m->pantalla = 5;
        return;
    }

    if (m->model == ZX_PLUS3 && (m->port_1ffd & 0x01)) {
        static const uint8_t k_ram[4][4] = {
            {0, 1, 2, 3},
            {4, 5, 6, 7},
            {4, 5, 6, 3},
            {4, 7, 6, 3},
        };
        const int cfg = (m->port_1ffd >> 1) & 3;
        for (int i = 0; i < 4; i++) m->page[i] = k_ram[cfg][i];
        m->slot0_rom = false;
        m->special_paging = true;
        return;
    }

    if (m->model == ZX_SCORPION && (m->port_1ffd & 0x01)) {
        m->page[0] = 0;
        m->page[1] = 5;
        m->page[2] = 2;
        m->page[3] = (uint8_t)(m->port_7ffd & 7);
        m->slot0_rom = false;
        return;
    }

    m->page[1] = 5;
    m->page[2] = 2;
    m->page[3] = (uint8_t)(m->port_7ffd & 7);
    if (m->model == ZX_SCORPION)
        m->page[3] = (uint8_t)(((m->port_7ffd & 7) | ((m->port_1ffd & 0x10) >> 1)) & 7);

    if (m->model == ZX_PLUS3)
        m->rom_sel = (uint8_t)(((m->port_7ffd >> 4) & 1) | ((m->port_1ffd >> 1) & 2));
    else if (m->model == ZX_SCORPION && (m->port_1ffd & 0x02))
        m->rom_sel = 2;
    else
        m->rom_sel = (uint8_t)((m->port_7ffd >> 4) & 1);

    m->slot0_rom = true;
    m->page[0] = 0;
}

static void apply_7ffd(zxspec *m, uint8_t value)
{
    if (m->model == ZX_SPEC48) return;
    if (m->paging_locked) return;
    m->port_7ffd = value;
    m->paging_locked = (value & 0x20) != 0;
    update_memory_map(m);
}

static void apply_1ffd(zxspec *m, uint8_t value)
{
    if (m->model != ZX_PLUS3 && m->model != ZX_SCORPION) return;
    m->port_1ffd = value;
    update_memory_map(m);
}

static const uint8_t *vram(const zxspec *m)
{
    return m->ram[m->pantalla & 7];
}

static uint8_t rom_read(const zxspec *m, uint16_t off)
{
    size_t base = (size_t)m->rom_sel * ZX_BANK_SIZE;
    if (m->rom128 && base + off < m->rom128_bytes)
        return m->rom128[base + off];
    if (m->rom48) return m->rom48[off];
    return 0xff;
}

static uint8_t mem_read(void *ud, uint16_t addr)
{
    zxspec *m = (zxspec *)ud;
    const int slot = addr >> 14;
    const uint16_t off = (uint16_t)(addr & 0x3fff);
    if (slot == 0) {
        if (m->slot0_rom) return rom_read(m, off);
        return m->ram[m->page[0] & 7][off];
    }
    return m->ram[m->page[slot] & 7][off];
}

static void mem_write(void *ud, uint16_t addr, uint8_t value)
{
    zxspec *m = (zxspec *)ud;
    const int slot = addr >> 14;
    const uint16_t off = (uint16_t)(addr & 0x3fff);
    if (slot == 0) {
        if (m->slot0_rom) return;
        m->ram[m->page[0] & 7][off] = value;
        return;
    }
    m->ram[m->page[slot] & 7][off] = value;
}

static uint8_t port_in(z80 *z, uint16_t port)
{
    zxspec *m = (zxspec *)z->userdata;
    uint8_t result = 0xff;

    if ((port & 1) == 0) {
        uint8_t keys = 0x1f;
        if ((port & 0x8000) == 0) keys &= m->keys[7];
        if ((port & 0x4000) == 0) keys &= m->keys[6];
        if ((port & 0x2000) == 0) keys &= m->keys[5];
        if ((port & 0x1000) == 0) keys &= m->keys[4];
        if ((port & 0x0800) == 0) keys &= m->keys[3];
        if ((port & 0x0400) == 0) keys &= m->keys[2];
        if ((port & 0x0200) == 0) keys &= m->keys[1];
        if ((port & 0x0100) == 0) keys &= m->keys[0];
        result = (uint8_t)((keys & 0x1f) | 0xa0);
        result = (uint8_t)((result & 0xbf) | m->ear | m->speaker);
    }

    /* Kempston: classic $1F (and A5=0, A0=1 style). */
    if ((port & 0xff) == 0x1f || (port & 0x21) == 0x01)
        result = (uint8_t)(m->joy & 0x1f);

    if (m->has_ay && (port & 0xc002) == 0xc000)
        result = ay8910_data_r(&m->ay);

    return result;
}

static void port_out(z80 *z, uint16_t port, uint8_t value)
{
    zxspec *m = (zxspec *)z->userdata;

    if ((port & 1) == 0) {
        m->border = (uint8_t)(value & 7);
        m->speaker = (value & 0x10) ? 0x40 : 0x00;
        m->beeper_level = (value & 0x10) ? (int16_t)ZX_BEEPER_AMP : (int16_t)(-ZX_BEEPER_AMP);
    }

    if (m->model == ZX_SPEC48) return;

    /* $7FFD banking (A15 low-ish, A1=0, A0=1). */
    if ((port & 0x8002) == 0 && (port & 1) != 0) {
        if (m->model == ZX_PLUS3 || m->model == ZX_SCORPION) {
            if ((port & 0xff00) == 0x1f00) {
                apply_1ffd(m, value);
                return;
            }
        }
        apply_7ffd(m, value);
    }

    /* +3 / Scorpion also accept exact $1FFD. */
    if ((m->model == ZX_PLUS3 || m->model == ZX_SCORPION) && port == 0x1ffd)
        apply_1ffd(m, value);

    if (m->has_ay) {
        if ((port & 0xc002) == 0xc000)
            ay8910_address_w(&m->ay, value);
        else if ((port & 0xc002) == 0x8000)
            ay8910_data_w(&m->ay, value);
    }
}

static void maybe_start_tape_from_rom(zxspec *m)
{
    if (!tape_loaded(&m->tape) || tape_playing(&m->tape)) return;
    const uint16_t pc = m->cpu.pc;
    if ((pc >= 0x04c2 && pc < 0x0800) || (pc >= 0x056c && pc < 0x0600))
        tape_play(&m->tape);
}

static void on_cycles(zxspec *m, int cycles)
{
    if (cycles <= 0) return;

    maybe_start_tape_from_rom(m);
    if (tape_playing(&m->tape))
        m->ear = tape_ear_bit(&m->tape, cycles) ? 0x40 : 0x00;

    m->audio_acc += (int64_t)cycles * ZX_AUDIO_RATE;
    while (m->audio_acc >= (int64_t)ZX_CLOCK) {
        m->audio_acc -= (int64_t)ZX_CLOCK;
        int32_t sample = m->beeper_level;
        if (m->has_ay) sample += ay8910_render(&m->ay);
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        if (m->naudio < ZX_AUDIO_MAX)
            m->audio[m->naudio++] = (int16_t)sample;
    }
}

static void apply_snap(zxspec *m, const spectrum_snap *s)
{
    m->cpu.a = s->a;
    m->cpu.f = s->f;
    m->cpu.b = s->b;
    m->cpu.c = s->c;
    m->cpu.d = s->d;
    m->cpu.e = s->e;
    m->cpu.h = s->h;
    m->cpu.l = s->l;
    m->cpu.a_ = s->a2;
    m->cpu.f_ = s->f2;
    m->cpu.b_ = s->b2;
    m->cpu.c_ = s->c2;
    m->cpu.d_ = s->d2;
    m->cpu.e_ = s->e2;
    m->cpu.h_ = s->h2;
    m->cpu.l_ = s->l2;
    m->cpu.ix = s->ix;
    m->cpu.iy = s->iy;
    m->cpu.sp = s->sp;
    m->cpu.i = s->i;
    m->cpu.r = s->r;
    m->cpu.interrupt_mode = s->im;
    m->cpu.iff1 = s->iff1;
    m->cpu.iff2 = s->iff2;
    m->cpu.pc = s->pc;
    m->cpu.halted = false;
    z80_clr_irq(&m->cpu);

    m->border = (uint8_t)(s->border & 7);

    if (s->is_128) {
        for (int b = 0; b < 8; b++) {
            if (s->banks[b])
                memcpy(m->ram[b], s->banks[b], ZX_BANK_SIZE);
        }
        m->paging_locked = false;
        apply_7ffd(m, s->port_7ffd);
        if (m->model == ZX_PLUS3 || m->model == ZX_SCORPION)
            apply_1ffd(m, s->port_1ffd);
    } else {
        if (s->ram48) {
            memcpy(m->ram[5], s->ram48, ZX_BANK_SIZE);
            memcpy(m->ram[2], s->ram48 + ZX_BANK_SIZE, ZX_BANK_SIZE);
            memcpy(m->ram[0], s->ram48 + 2 * ZX_BANK_SIZE, ZX_BANK_SIZE);
        }
        m->paging_locked = false;
        if (is_128_class(m->model)) {
            /* Force 48K-compatible paging on a 128K-class machine. */
            apply_7ffd(m, 0x10);
        } else {
            update_memory_map(m);
        }
    }
}

static bool ends_with_ci(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t n = strlen(path), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        unsigned char a = (unsigned char)path[n - e + i];
        unsigned char b = (unsigned char)ext[i];
        if (tolower(a) != tolower(b)) return false;
    }
    return true;
}

static uint32_t pixel_at(const zxspec *m, int full_x, int full_y)
{
    const uint32_t border = k_palette[m->border & 7];
    if (full_x < ZX_PAPER_X || full_x >= ZX_PAPER_X + 256 ||
        full_y < ZX_PAPER_Y || full_y >= ZX_PAPER_Y + 192)
        return border;

    const int y = full_y - ZX_PAPER_Y;
    const int x = full_x - ZX_PAPER_X;
    const uint8_t *v = vram(m);
    const uint8_t pixels = v[k_scr_table[y] + (x >> 3)];
    const uint8_t attrib = v[0x1800 + ((y >> 3) << 5) + (x >> 3)];
    int ink = attrib & 7;
    int paper = (attrib >> 3) & 7;
    if (attrib & 0x40) {
        ink += 8;
        paper += 8;
    }
    if ((attrib & 0x80) && m->flash) {
        int tmp = ink;
        ink = paper;
        paper = tmp;
    }
    const int bit = 7 - (x & 7);
    return k_palette[(pixels & (1 << bit)) ? ink : paper];
}

static uint16_t to_rgb565(uint32_t c)
{
    unsigned r = (c >> 16) & 0xff;
    unsigned g = (c >> 8) & 0xff;
    unsigned b = c & 0xff;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ---- public API --------------------------------------------------------- */

void zxspec_init(zxspec *m, zx_model_t model)
{
    if (!m) return;
    /* Preserve any RAM banks already allocated by zxspec_create. */
    uint8_t *banks[8];
    for (int b = 0; b < 8; b++) banks[b] = m->ram[b];
    memset(m, 0, sizeof *m);
    for (int b = 0; b < 8; b++) m->ram[b] = banks[b];
    m->model = model;
    m->has_ay = is_128_class(model);
    m->border = 7;
    memset(m->keys, 0xff, sizeof m->keys);
    tape_init(&m->tape);
    z80_init(&m->cpu);
    m->cpu.read_byte = mem_read;
    m->cpu.write_byte = mem_write;
    m->cpu.port_in = port_in;
    m->cpu.port_out = port_out;
    m->cpu.userdata = m;
    if (m->has_ay)
        ay8910_init(&m->ay, ZX_AY_CLOCK, ZX_AUDIO_RATE);
    update_memory_map(m);
}

void zxspec_set_rom(zxspec *m, const uint8_t *rom48, const uint8_t *rom128, size_t rom128_bytes)
{
    if (!m) return;
    m->rom48 = rom48;
    m->rom128 = rom128;
    m->rom128_bytes = rom128_bytes;
}

void zxspec_reset(zxspec *m)
{
    if (!m) return;
    for (int b = 0; b < 8; b++) {
        if (m->ram[b]) memset(m->ram[b], 0, ZX_BANK_SIZE);
    }
    m->port_7ffd = 0;
    m->port_1ffd = 0;
    m->paging_locked = false;
    m->special_paging = false;
    update_memory_map(m);
    z80_reset(&m->cpu);
    if (m->has_ay) ay8910_reset(&m->ay);
    m->border = 7;
    m->speaker = 0;
    m->ear = 0;
    memset(m->keys, 0xff, sizeof m->keys);
    m->joy = 0;
    m->flash = false;
    m->flash_count = 0;
    m->beeper_level = 0;
    m->audio_acc = 0;
    m->naudio = 0;
    tape_stop(&m->tape);
}

int zxspec_run_frame(zxspec *m, const int16_t **audio)
{
    if (!m) {
        if (audio) *audio = NULL;
        return 0;
    }
    m->naudio = 0;

    z80_assert_irq(&m->cpu, 0xff);
    int frame_t = 0;
    while (frame_t < ZX_T_FRAME) {
        unsigned left = (unsigned)(ZX_T_FRAME - frame_t);
        unsigned ask = left > 64u ? (left > (unsigned)ZX_T_LINE ? (unsigned)ZX_T_LINE : left) : 1u;
        unsigned ran = z80_step_n(&m->cpu, ask);
        if (ran == 0) {
            ran = z80_step(&m->cpu);
            if (ran == 0) break;
        }
        frame_t += (int)ran;
        on_cycles(m, (int)ran);
        if (frame_t >= ZX_IRQ_HOLD_T)
            z80_clr_irq(&m->cpu);
    }

    m->flash_count = (m->flash_count + 1) & 0x0f;
    if (m->flash_count == 0) m->flash = !m->flash;

    if (audio) *audio = m->audio;
    return m->naudio;
}

void zxspec_key(zxspec *m, int code, bool down)
{
    if (!m) return;
    int row = (code >> 3) & 7;
    int bit = code & 7;
    if (bit > 4) return;
    if (down)
        m->keys[row] = (uint8_t)(m->keys[row] & ~(1u << bit));
    else
        m->keys[row] = (uint8_t)(m->keys[row] | (1u << bit));
}

void zxspec_keys_clear(zxspec *m)
{
    if (!m) return;
    memset(m->keys, 0xff, sizeof m->keys);
}

void zxspec_joy(zxspec *m, uint8_t kempston)
{
    if (!m) return;
    m->joy = (uint8_t)(kempston & 0x1f);
}

bool zxspec_load_file(zxspec *m, const char *path)
{
    if (!m || !path) return false;
    if (ends_with_ci(path, ".tap") || ends_with_ci(path, ".tzx")) {
        if (!tape_load_file(&m->tape, path)) return false;
        tape_stop(&m->tape);
        return true;
    }
    if (ends_with_ci(path, ".sna")) {
        spectrum_snap snap;
        spectrum_snap_init(&snap);
        if (!spectrum_snap_load_file(&snap, path)) {
            spectrum_snap_free(&snap);
            return false;
        }
        apply_snap(m, &snap);
        spectrum_snap_free(&snap);
        return true;
    }
    /* Web upload (/zx_upload.bin) and unknown names: sniff contents. */
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return false; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return false; }
    fclose(f);
    bool ok = zxspec_load_mem(m, buf, (size_t)sz, path);
    free(buf);
    return ok;
}

bool zxspec_load_mem(zxspec *m, const uint8_t *data, size_t n, const char *name_hint)
{
    if (!m || !data || n == 0) return false;
    const char *hint = name_hint ? name_hint : "";
    if (ends_with_ci(hint, ".tap") || ends_with_ci(hint, ".tzx")) {
        if (!tape_load_mem(&m->tape, data, n)) return false;
        tape_stop(&m->tape);
        return true;
    }
    if (ends_with_ci(hint, ".sna")) {
        spectrum_snap snap;
        spectrum_snap_init(&snap);
        if (!spectrum_snap_load_mem(&snap, data, n)) {
            spectrum_snap_free(&snap);
            return false;
        }
        apply_snap(m, &snap);
        spectrum_snap_free(&snap);
        return true;
    }
    /* Heuristic: SNA sizes, else treat as tape. */
    if (n == 49179 || n >= 49183) {
        spectrum_snap snap;
        spectrum_snap_init(&snap);
        if (spectrum_snap_load_mem(&snap, data, n)) {
            apply_snap(m, &snap);
            spectrum_snap_free(&snap);
            return true;
        }
        spectrum_snap_free(&snap);
    }
    if (tape_load_mem(&m->tape, data, n)) {
        tape_stop(&m->tape);
        return true;
    }
    return false;
}

void zxspec_tape_play(zxspec *m)
{
    if (m && tape_loaded(&m->tape)) tape_play(&m->tape);
}

void zxspec_tape_stop(zxspec *m)
{
    if (!m) return;
    tape_stop(&m->tape);
    m->ear = 0;
}

bool zxspec_tape_loaded(const zxspec *m)
{
    return m && tape_loaded(&m->tape);
}

bool zxspec_tape_playing(const zxspec *m)
{
    return m && tape_playing(&m->tape);
}

void zxspec_render(const zxspec *m, uint32_t *rgb)
{
    if (!m || !rgb) return;
    for (int y = 0; y < ZX_H; y++) {
        const int fy = y + ZX_CROP_Y;
        uint32_t *row = rgb + (size_t)y * ZX_W;
        for (int x = 0; x < ZX_W; x++)
            row[x] = pixel_at(m, x + ZX_CROP_X, fy);
    }
}

void zxspec_render_row565(const zxspec *m, int y, uint16_t *out320)
{
    if (!m || !out320 || y < 0 || y >= ZX_H) return;
    const int fy = y + ZX_CROP_Y;
    for (int x = 0; x < ZX_W; x++)
        out320[x] = to_rgb565(pixel_at(m, x + ZX_CROP_X, fy));
}

const char *zxspec_title(zx_model_t model)
{
    switch (model) {
        case ZX_SPEC48:   return "ZX Spectrum 48K";
        case ZX_SPEC128:  return "ZX Spectrum 128K";
        case ZX_PLUS3:    return "ZX Spectrum +3";
        case ZX_PENTAGON: return "Pentagon";
        case ZX_SCORPION: return "Scorpion";
        default:          return "ZX Spectrum";
    }
}

zx_model_t zxspec_model(const zxspec *m)
{
    return m ? m->model : ZX_SPEC48;
}

zxspec *zxspec_create(zx_model_t model)
{
    zxspec *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    uint8_t *banks[8] = {0};
    for (int b = 0; b < 8; b++) {
        banks[b] = calloc(1, ZX_BANK_SIZE);
        if (!banks[b]) {
            for (int i = 0; i < b; i++) free(banks[i]);
            free(m);
            return NULL;
        }
    }
    zxspec_init(m, model);
    for (int b = 0; b < 8; b++) m->ram[b] = banks[b];
    update_memory_map(m);
    return m;
}

void zxspec_destroy(zxspec *m)
{
    if (!m) return;
    tape_unload(&m->tape);
    for (int b = 0; b < 8; b++) {
        free(m->ram[b]);
        m->ram[b] = NULL;
    }
    free(m);
}
