// zxspec_core.c - ZX Spectrum family: the public API, the parts of the ULA
// the dsp-cpp drivers share (per-T-state border rows, paper latches, the
// picture, beeper / AY mixing, tape motor), the frame loops and the media.
// The models themselves are in zx48.c, zx128.c and zxclone.c.
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "zx_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Spectrum ink / paper 0-15 (dsp-cpp kPalette), 0x00RRGGBB
static const uint32_t k_palette[16] = {
    0x000000, 0x0000C0, 0xC00000, 0xC000C0, 0x00C000, 0x00C0C0, 0xC0C000, 0xC0C0C0,
    0x000000, 0x0000FF, 0xFF0000, 0xFF00FF, 0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF,
};

// Pixel line offsets within the 6 KB bitmap (dsp-cpp kScrTable)
const uint16_t zx_scr_table[192] = {
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
    4288, 4544, 4800, 5056, 5312, 5568, 5824, 6080, 4320, 4576, 4832, 5088, 5344, 5600, 5856, 6112};

const uint8_t zx_empty_page[ZX_BANK];

static uint16_t to565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 0xf8) << 8) | (((c >> 8) & 0xfc) << 3) | ((c & 0xff) >> 3));
}

static void set_colour(zxspec *m, int i, uint32_t rgb)
{
    m->palette_ext[i] = rgb;
    m->pal565[i] = to565(rgb);
}

// ---------------------------------------------------------------------------
// ULA+ (spectrum_48k.pas set_pal_color: GRB332)
// ---------------------------------------------------------------------------
static uint32_t ulaplus_decode(uint8_t v)
{
    const uint32_t b = 0x21u * (v & 1) + 0x47u * (v & 1) + 0x97u * ((v >> 1) & 1);
    const uint32_t r = 0x21u * ((v >> 2) & 1) + 0x47u * ((v >> 3) & 1) + 0x97u * ((v >> 4) & 1);
    const uint32_t g = 0x21u * ((v >> 5) & 1) + 0x47u * ((v >> 6) & 1) + 0x97u * ((v >> 7) & 1);
    return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

static void ulaplus_reset(zxspec *m)
{
    m->ulaplus_active = false;
    m->ulaplus_mode = 0;
    m->ulaplus_last_reg = 0;
    memset(m->ulaplus_pal, 0, sizeof m->ulaplus_pal);
    for (int i = 0; i < 16; i++) set_colour(m, i, k_palette[i]);
    for (int i = 16; i < 80; i++) set_colour(m, i, 0);
}

void zx_ulaplus_out(zxspec *m, uint16_t port, uint8_t value)
{
    if (!m->ulaplus_enabled) return;
    if (port == 0xbf3b) {
        m->ulaplus_mode = (uint8_t)(value >> 6);
        if (m->ulaplus_mode == 0) m->ulaplus_last_reg = value & 0x3f;
    }
    if (port == 0xff3b) {
        if (m->ulaplus_mode == 0) {
            m->ulaplus_pal[m->ulaplus_last_reg & 63] = value;
            set_colour(m, 16 + (m->ulaplus_last_reg & 63), ulaplus_decode(value));
        } else if (m->ulaplus_mode == 1) {
            m->ulaplus_active = (value & 1) != 0;
        }
    }
}

bool zx_ulaplus_in(zxspec *m, uint16_t port, uint8_t *result)
{
    if (port != 0xff3b || !m->ulaplus_enabled) return false;
    if (m->ulaplus_mode == 0) *result = m->ulaplus_pal[m->ulaplus_last_reg & 63];
    else if (m->ulaplus_mode == 1) *result = m->ulaplus_active ? 1 : 0;
    else return false;
    return true;
}

static bool ulaplus_on(const zxspec *m)
{
    return m->kind != ZK_CLONE && m->ulaplus_active && m->ulaplus_enabled;
}

uint8_t zx_border_index(const zxspec *m)
{
    return ulaplus_on(m) ? (uint8_t)(16 + (m->border & 7)) : (uint8_t)(m->border & 7);
}

// ---------------------------------------------------------------------------
// keyboard (port $FE)
// ---------------------------------------------------------------------------
uint8_t zx_keys_read(const zxspec *m, uint16_t port)
{
    uint8_t keys = 0x1f;
    for (int row = 0; row < 8; row++) {
        if (port & (0x100 << row)) continue;
        uint8_t k = m->keys[row];
        if (row == 4) k &= (uint8_t)~m->sinclair;     // 128K: Interface 2 right stick on 6-0
        keys &= k;
    }
    return keys;
}

// ---------------------------------------------------------------------------
// border rows, paper latches, the picture (render_line of the drivers)
// ---------------------------------------------------------------------------
void zx_start_border_frame(zxspec *m)
{
    // Lines the beam does not reach keep the colour the frame started with.
    m->border_seed = zx_border_index(m);
    memset(m->border_cur, m->border_seed, (size_t)m->t_line);
    memset(m->border_prev, m->border_seed, (size_t)m->t_line);
}

void zx_next_border_line(zxspec *m)
{
    uint8_t *t = m->border_cur;
    m->border_cur = m->border_prev;
    m->border_prev = t;
    memset(m->border_cur, m->border_seed, (size_t)m->t_line);
}

static inline void latch_column(zxspec *m, int col)
{
    const int y = m->line - m->paper_first;
    const uint8_t *vram = m->ram[m->pantalla];
    m->latch_pix[col] = vram[zx_scr_table[y] + col];
    m->latch_attr[col] = vram[0x1800 + ((y >> 3) << 5) + col];
    m->latch_mask |= 1u << col;
}

// The paper's ink / paper indices of a cell
static inline void cell_colours(const zxspec *m, uint8_t attrib, bool uplus, uint8_t *c_ink, uint8_t *c_paper)
{
    if (uplus) {
        const int bank = ((((attrib & 0x80) >> 6) + ((attrib & 0x40) >> 6)) << 4) + 16;
        *c_ink = (uint8_t)(bank + (attrib & 7));
        *c_paper = (uint8_t)(bank + ((attrib >> 3) & 7) + 8);
    } else {
        int ink = attrib & 7, paper = (attrib >> 3) & 7;
        if (attrib & 0x40) { ink += 8; paper += 8; }
        if ((attrib & 0x80) && m->flash) { int t = ink; ink = paper; paper = t; }
        *c_ink = (uint8_t)ink;
        *c_paper = (uint8_t)paper;
    }
}

// The boards' 320x240 picture, 4 bits a pixel: the same as below, written
// two pixels a byte straight away (crop_x = 16: left border T 8-23, right
// border T 128-143).
static void ZX_HOT emit_packed(zxspec *m, int line, int row, uint8_t *out)
{
    // left border: T left_t+8 .. left_t+23 of the previous line
    if (m->kind == ZK_CLONE && row == 0) {
        memset(out, (m->border & 7) * 0x11, 16);
    } else {
        const uint8_t *prev = m->border_prev + m->left_t + 8;
        for (int f = 0; f < 16; f++) out[f] = (uint8_t)((prev[f] & 15) * 0x11);
    }
    const uint8_t *brow = m->border_cur;
    for (int f = 0; f < 16; f++) out[144 + f] = (uint8_t)((brow[128 + f] & 15) * 0x11);
    if (line >= m->paper_first && line < m->paper_first + 192) {
        const int y = line - m->paper_first;
        const uint8_t *vram = m->ram[m->pantalla];
        const uint16_t pix_base = zx_scr_table[y];
        const int attr_row = (y >> 3) << 5;
        const bool uplus = ulaplus_on(m);
        uint8_t *cell = out + 16;
        for (int col = 0; col < 32; col++, cell += 4) {
            const bool latched = (m->latch_mask >> col) & 1;
            const uint8_t attrib = latched ? m->latch_attr[col] : vram[0x1800 + attr_row + col];
            const uint8_t pix = latched ? m->latch_pix[col] : vram[pix_base + col];
            uint8_t ci, cp;
            cell_colours(m, attrib, uplus, &ci, &cp);
            ci &= 15;
            cp &= 15;
            const uint8_t lut[4] = { (uint8_t)(cp | cp << 4), (uint8_t)(cp | ci << 4),
                                     (uint8_t)(ci | cp << 4), (uint8_t)(ci | ci << 4) };
            cell[0] = lut[(pix >> 6) & 3];
            cell[1] = lut[(pix >> 4) & 3];
            cell[2] = lut[(pix >> 2) & 3];
            cell[3] = lut[pix & 3];
        }
    } else {
        for (int f = 0; f < 128; f++) out[16 + f] = (uint8_t)((brow[f] & 15) * 0x11);
    }
}

void ZX_HOT zx_emit_line(zxspec *m, int line)
{
    const int row = line - m->pic_first;
    if (row < 0 || row >= ZX_FULL_H) return;
    const int sy = row - m->crop_y;
    if (sy < 0 || sy >= m->pic_h) return;
    if (m->packed && m->crop_x == 16 && m->pic_w == 320) {
        emit_packed(m, line, row, m->pic + (size_t)sy * 160u);
        return;
    }
    uint8_t *dst = m->line_px;

    // left border: T left_t..left_t+23 of the previous line
    if (m->kind == ZK_CLONE && row == 0) {
        memset(dst, m->border & 7, 48);
    } else {
        const uint8_t *prev = m->border_prev + m->left_t;
        for (int f = 0; f < 24; f++) dst[2 * f] = dst[2 * f + 1] = prev[f];
    }
    // right border: T 128..151
    const uint8_t *brow = m->border_cur;
    for (int f = 0; f < 24; f++) dst[304 + 2 * f] = dst[304 + 2 * f + 1] = brow[128 + f];

    if (line >= m->paper_first && line < m->paper_first + 192) {
        // paper: the ULA latches (fetch-time bytes) for 8x1 multicolour
        const int y = line - m->paper_first;
        const uint8_t *vram = m->ram[m->pantalla];
        const uint16_t pix_base = zx_scr_table[y];
        const int attr_row = (y >> 3) << 5;
        const bool uplus = ulaplus_on(m);
        for (int col = 0; col < 32; col++) {
            const bool latched = (m->latch_mask >> col) & 1;
            const uint8_t attrib = latched ? m->latch_attr[col] : vram[0x1800 + attr_row + col];
            uint8_t pix = latched ? m->latch_pix[col] : vram[pix_base + col];
            uint8_t c_ink, c_paper;
            cell_colours(m, attrib, uplus, &c_ink, &c_paper);
            uint8_t *cell = dst + 48 + col * 8;
            for (int b = 0; b < 8; b++, pix <<= 1) cell[b] = (pix & 0x80) ? c_ink : c_paper;
        }
    } else {
        // top / bottom border: T 0..127
        for (int f = 0; f < 128; f++) dst[48 + 2 * f] = dst[48 + 2 * f + 1] = brow[f];
    }

    const uint8_t *src = dst + m->crop_x;
    if (m->packed) {
        uint8_t *out = m->pic + (size_t)sy * (size_t)(m->pic_w / 2);
        for (int x = 0; x < m->pic_w; x += 2) out[x / 2] = (uint8_t)((src[x] & 15) | (src[x + 1] << 4));
    } else {
        memcpy(m->pic + (size_t)sy * (size_t)m->pic_w, src, (size_t)m->pic_w);
    }
}

// ---------------------------------------------------------------------------
// tape motor, sound
// ---------------------------------------------------------------------------
// Start the tape when the ROM enters LD-BYTES / SA-BYTES ($04C2-$07FF), so
// it cannot run through before LOAD "" does (dsp-cpp: whatever ROM is paged;
// the 128K starts and advances it in the same step, the others start it).
static inline bool at_tape_routines(const zxspec *m)
{
    const uint16_t pc = m->cpu.pc;
    return pc >= 0x04c2 && pc < 0x0800;
}

void ZX_HOT zx_tape_cycles(zxspec *m, int cycles)
{
    if (m->kind == ZK_128) {
        if (zt_is_loaded(&m->tape) && !zt_is_playing(&m->tape) && !m->tape_held && at_tape_routines(m))
            zt_play(&m->tape, !zt_is_paused(&m->tape));
        if (zt_is_playing(&m->tape)) m->ear = zt_advance(&m->tape, cycles) ? 0x40 : 0x00;
        return;
    }
    if (zt_is_playing(&m->tape)) {
        m->ear = zt_advance(&m->tape, cycles) ? 0x40 : 0x00;
    } else if (zt_is_loaded(&m->tape) && !m->tape_held && at_tape_routines(m)) {
        zt_play(&m->tape, !zt_is_paused(&m->tape));
    }
}

static void ZX_HOT audio_samples(zxspec *m)
{
    while (m->audio_acc >= (int64_t)m->clock) {
        m->audio_acc -= m->clock;
        int32_t s = m->beeper_level;
        if (m->nay) {
            s += ay8910_render(&m->ay[0]);
            if (m->ay1_used) s += ay8910_render(&m->ay[1]);
        }
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (m->naudio < ZX_AUDIO_MAX) m->audio[m->naudio++] = (int16_t)s;
    }
}

void ZX_HOT zx_push_audio(zxspec *m, int cycles)
{
    m->audio_acc += (int64_t)cycles * m->audio_rate;
    if (m->audio_acc >= (int64_t)m->clock) audio_samples(m);
}

// on_cycles of the drivers: the border colour for every elapsed T-state, the
// paper latches every 4 T-states, a line drawn as the beam leaves it.
void ZX_HOT zx_on_cycles(zxspec *m, int cycles)
{
    for (int left = cycles; left > 0;) {
        // waits can take the beam past the frame before the Z80 has run it
        if (m->frame_t >= m->t_frame) {
            m->frame_t += left;
            break;
        }
        int n = m->t_line - m->t_in_line;
        if (n > left) n = left;
        const int end = m->t_in_line + n;
        if (m->line >= m->paper_first && m->line < m->paper_first + 192 && m->t_in_line < 128) {
            const int stop = end < 128 ? end : 128;
            for (int t = (m->t_in_line + 3) & ~3; t < stop; t += 4) latch_column(m, t >> 2);
        }
        {
            uint8_t *b = m->border_cur + m->t_in_line;
            const uint8_t c = zx_border_index(m);
            if (n <= 8) for (int k = 0; k < n; k++) b[k] = c;
            else memset(b, c, (size_t)n);
        }
        m->t_in_line = end;
        m->frame_t += n;
        left -= n;
        if (m->t_in_line >= m->t_line) {
            m->t_in_line -= m->t_line;
            zx_emit_line(m, m->line);
            m->latch_mask = 0;
            m->line++;
            zx_next_border_line(m);
        }
    }
    if (m->tape.loaded) zx_tape_cycles(m, cycles);
    if (m->if2_rom && !m->if2_switched) {
        m->if2_delay += cycles;
        if (m->if2_delay > 10500000) m->if2_switched = true;
    }
    m->audio_acc += (int64_t)cycles * m->audio_rate;
    if (m->audio_acc >= (int64_t)m->clock) audio_samples(m);
}

// Brings the beam to the T-state inside the current instruction (Fuse-style
// interleaved timing) and returns the frame T-state.
int ZX_HOT zx_ula_time(zxspec *m)
{
    const int delta = zd_t_in_instruction(&m->cpu) - m->instr_t_flushed;
    if (delta > 0) {
        zx_on_cycles(m, delta);
        m->instr_t_flushed += delta;
    }
    return m->frame_t;
}

void ZX_HOT zx_sync_to(zxspec *m, int t_in_instr)
{
    const int delta = t_in_instr - m->instr_t_flushed;
    if (delta > 0) {
        zx_on_cycles(m, delta);
        m->instr_t_flushed += delta;
    }
}

void ZX_HOT zx_on_insn_cycles(void *user, int cycles)
{
    zxspec *m = (zxspec *)user;
    const int rem = cycles - m->instr_t_flushed;
    m->instr_t_flushed = 0;
    if (rem > 0) zx_on_cycles(m, rem);
}

// ---------------------------------------------------------------------------
// frames
// ---------------------------------------------------------------------------
static void frame_begin(zxspec *m)
{
    m->line = 0;
    m->t_in_line = 0;
    m->frame_t = 0;
    m->instr_t_flushed = 0;
    m->latch_mask = 0;
    zx_start_border_frame(m);
}

void zx_finish_frame(zxspec *m)
{
    // Lines the beam did not reach (never re-draw one: that would wipe the
    // per-T-state border); latches belong to the line the beam stopped in.
    while (m->line < m->lines) {
        zx_emit_line(m, m->line);
        m->latch_mask = 0;
        m->line++;
        zx_next_border_line(m);
    }
    m->line = 0;
    m->t_in_line = 0;
    m->frame_t = 0;
    m->instr_t_flushed = 0;
    m->flash_count = (m->flash_count + 1) & 0x0f;
    if (m->flash_count == 0) m->flash = !m->flash;
}

static void run_rzx_frame(zxspec *m)
{
    if (!zx_rzx_begin_frame(&m->rzx)) return;
    frame_begin(m);
    zd_set_irq(&m->cpu, ZD_HOLD, 0xff);
    // until the frame's fetch budget is spent (one interrupt per RZX frame)
    int safety = m->t_frame * 4;
    while (m->rzx.fetches_left > 0 && safety-- > 0) {
        if (zd_run(&m->cpu, 1) <= 0) break;
        if (m->frame_t >= 32) zd_set_irq(&m->cpu, ZD_CLEAR, 0xff);
    }
    zx_finish_frame(m);
}

static void run_frame(zxspec *m)
{
    frame_begin(m);
    zd_set_irq(&m->cpu, ZD_HOLD, 0xff);
    if (m->kind == ZK_CLONE) {
        // no contention: driven by the Z80's cycles
        int remaining = m->t_frame;
        while (remaining > 0) {
            const int ran = zd_run(&m->cpu, remaining < m->t_line ? remaining : m->t_line);
            if (ran <= 0) break;
            remaining -= ran;
            if (remaining < m->t_frame - 32) zd_set_irq(&m->cpu, ZD_CLEAR, 0xff);
        }
    } else {
        // driven by ULA time: contended waits advance the beam
        while (m->frame_t < m->t_frame) {
            const int left = m->t_frame - m->frame_t;
            int ask = left > 64 ? (left - 32 < m->t_line ? left - 32 : m->t_line) : 1;
            if (ask < 1) ask = 1;
            if (zd_run(&m->cpu, ask) <= 0) break;
            if (m->frame_t >= 32) zd_set_irq(&m->cpu, ZD_CLEAR, 0xff);
        }
    }
    zx_finish_frame(m);
}

int zxspec_run_frame(zxspec *m, const int16_t **audio)
{
    if (!m) {
        if (audio) *audio = NULL;
        return 0;
    }
    m->naudio = 0;
    if (m->rzx.playing) run_rzx_frame(m);
    else run_frame(m);
    if (audio) *audio = m->audio;
    return m->naudio;
}

// ---------------------------------------------------------------------------
// creation
// ---------------------------------------------------------------------------
static zx_kind kind_of(zx_model_t model)
{
    switch (model) {
    case ZX_SPEC48: return ZK_48;
    case ZX_SPEC128: return ZK_128;
    case ZX_PLUS3: return ZK_PLUS3;
    default: return ZK_CLONE;
    }
}

static int pages_wanted(zx_model_t model)
{
    switch (model) {
    case ZX_PENTAGON1024:
#ifdef ZX_SMALL
        return 8;               // no room for 1 MB: a 128K Pentagon
#else
        return 64;
#endif
    case ZX_SCORPION:
#if defined(ZX_SMALL) && !defined(ESP_PLATFORM)
        return 8;               // the RP2040
#else
        return 16;
#endif
    default:
        return 8;
    }
}

static void free_machine(zxspec *m)
{
    for (int b = 0; b < ZX_MAX_PAGES; b++) free(m->ram[b]);
    free(m->pic);
    free(m->if2_rom);
    zt_free(&m->tape);
    zx_rzx_free(&m->rzx);
    zx_beta_free(&m->beta);
    nec_free(&m->fdc);
    free(m);
}

zxspec *zxspec_create(zx_model_t model)
{
    zxspec *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->model = model;
    m->kind = kind_of(model);
    m->audio_rate = ZX_AUDIO_RATE;
    m->border_cur = m->border_rows[0];
    m->border_prev = m->border_rows[1];
    zt_init(&m->tape);
    zx_rzx_init(&m->rzx);
    zx_beta_init(&m->beta);
    nec_init(&m->fdc);

    // the picture first: the RAM pages that do not fit after it are left out
    m->pic_w = ZX_PIC_W;
    m->pic_h = ZX_PIC_H;
    m->crop_x = (ZX_FULL_W - m->pic_w) / 2;
    m->crop_y = (ZX_FULL_H - m->pic_h) / 2;
#ifdef ZX_SMALL
    m->packed = true;
    m->pic = calloc(1, (size_t)(m->pic_w / 2) * (size_t)m->pic_h);
#else
    m->pic = calloc(1, (size_t)m->pic_w * (size_t)m->pic_h);
#endif
    if (!m->pic) goto fail;

    if (m->kind == ZK_48) {
        const int banks[3] = { 5, 2, 0 };
        for (int i = 0; i < 3; i++)
            if (!(m->ram[banks[i]] = calloc(1, ZX_BANK))) goto fail;
        m->ram_pages = 8;
    } else {
        int want = pages_wanted(model), n = 0;
        for (; n < want; n++)
            if (!(m->ram[n] = calloc(1, ZX_BANK))) break;
        if (n < 8) goto fail;
#ifdef ESP_PLATFORM
        // the CYD: the Scorpion's 256K only if the firmware keeps its heap
        if (n > 8 && heap_caps_get_free_size(MALLOC_CAP_8BIT) < 56 * 1024) n = 8;
#endif
        int pages = 8;
        while (pages * 2 <= n) pages *= 2;      // a power of two (paging masks)
        for (int b = pages; b < ZX_MAX_PAGES; b++) {
            free(m->ram[b]);
            m->ram[b] = NULL;
        }
        m->ram_pages = pages;
    }

    switch (m->kind) {
    case ZK_48: zx48_setup(m); break;
    case ZK_128:
    case ZK_PLUS3: zx128_setup(m); break;
    default: zxclone_setup(m); break;
    }
    m->cpu.user = m;
    zd_init(&m->cpu);
    for (int i = 0; i < 4; i++)
        if (!m->rom[i]) m->rom[i] = zx_empty_page;
    zxspec_reset(m);
    return m;
fail:
    free_machine(m);
    return NULL;
}

void zxspec_destroy(zxspec *m)
{
    if (m) free_machine(m);
}

static bool contains(const uint8_t *p, size_t n, const char *text)
{
    const size_t k = strlen(text);
    for (size_t i = 0; i + k <= n; i++)
        if (memcmp(p + i, text, k) == 0) return true;
    return false;
}

void zxspec_set_rom(zxspec *m, const uint8_t *rom48, const uint8_t *rom128, size_t rom128_bytes)
{
    if (!m) return;
    for (int i = 0; i < 4; i++) m->rom[i] = zx_empty_page;
    if (m->kind == ZK_48) {
        if (rom48) m->rom[0] = rom48;
    } else if (rom128) {
        const int n = (int)(rom128_bytes / ZX_BANK);
        for (int i = 0; i < 4 && i < n; i++) m->rom[i] = rom128 + (size_t)i * ZX_BANK;
        // dsp-cpp ZxClone: the Gluk / service ROM in page 2 boots the Pentagon
        m->gluk_present = (m->model == ZX_PENTAGON || m->model == ZX_PENTAGON1024) && n >= 4 &&
                          (contains(m->rom[2], ZX_BANK, "GLUK") || contains(m->rom[2], ZX_BANK, "RESET SERVICE"));
    }
}

void zxspec_reset(zxspec *m)
{
    if (!m) return;
    zd_reset(&m->cpu);
    m->border = 7;
    m->speaker = m->ear = 0;
    memset(m->keys, 0xff, sizeof m->keys);
    m->joy = m->sinclair = 0;
    m->flash = false;
    m->flash_count = 0;
    m->line = m->t_in_line = m->frame_t = m->instr_t_flushed = 0;
    m->latch_mask = 0;
    m->audio_acc = 0;
    m->beeper_level = 0;
    m->naudio = 0;
    m->ay_select = 0;
    m->ay1_used = false;
    for (int i = 0; i < m->nay; i++) ay8910_reset(&m->ay[i]);
    zt_stop(&m->tape);
    m->tape_held = false;
    zx_rzx_stop(&m->rzx);
    ulaplus_reset(m);
    memset(m->border_rows, 7, sizeof m->border_rows);
    switch (m->kind) {
    case ZK_48: zx48_reset(m); break;
    case ZK_128:
    case ZK_PLUS3: zx128_reset(m); break;
    default: zxclone_reset(m); break;
    }
    // the picture in the border colour
    if (m->packed) memset(m->pic, 0x77, (size_t)(m->pic_w / 2) * (size_t)m->pic_h);
    else memset(m->pic, 7, (size_t)m->pic_w * (size_t)m->pic_h);
}

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------
void zxspec_key(zxspec *m, int code, bool down)
{
    if (!m) return;
    const int row = (code >> 3) & 7, bit = code & 7;
    if (bit > 4) return;
    if (down) m->keys[row] = (uint8_t)(m->keys[row] & ~(1u << bit));
    else m->keys[row] = (uint8_t)(m->keys[row] | (1u << bit));
}

void zxspec_keys_clear(zxspec *m)
{
    if (m) memset(m->keys, 0xff, sizeof m->keys);
}

void zxspec_joy(zxspec *m, uint8_t kempston)
{
    if (!m) return;
    m->joy = (uint8_t)(kempston & 0x1f);
    // dsp-cpp Spectrum128k: player 1 is also the Interface 2 right stick
    // (6 left, 7 right, 8 down, 9 up, 0 fire) on the keyboard matrix.
    m->sinclair = 0;
    if (m->kind == ZK_128) {
        if (kempston & 0x02) m->sinclair |= 0x10;
        if (kempston & 0x01) m->sinclair |= 0x08;
        if (kempston & 0x04) m->sinclair |= 0x04;
        if (kempston & 0x08) m->sinclair |= 0x02;
        if (kempston & 0x10) m->sinclair |= 0x01;
    }
}

void zxspec_magic(zxspec *m)
{
    if (m && m->kind == ZK_CLONE) zxclone_magic(m);
}

// ---------------------------------------------------------------------------
// media
// ---------------------------------------------------------------------------
static bool ends_with_ci(const char *path, const char *ext)
{
    if (!path) return false;
    const size_t n = strlen(path), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)path[n - k + i]) != ext[i]) return false;
    return true;
}

typedef enum { MT_NONE, MT_TAPE, MT_SNAP, MT_RZX, MT_TRD, MT_DSK, MT_IF2 } media_t;

static media_t media_type(const char *name, const uint8_t *d, size_t n)
{
    static const char *const tapes[] = { ".tap", ".tzx", ".cdt", ".csw", ".pzx" };
    for (size_t i = 0; i < sizeof tapes / sizeof tapes[0]; i++)
        if (ends_with_ci(name, tapes[i])) return MT_TAPE;
    if (ends_with_ci(name, ".sna") || ends_with_ci(name, ".z80")) return MT_SNAP;
    if (ends_with_ci(name, ".rzx")) return MT_RZX;
    if (ends_with_ci(name, ".trd") || ends_with_ci(name, ".scl")) return MT_TRD;
    if (ends_with_ci(name, ".dsk")) return MT_DSK;
    if (ends_with_ci(name, ".rom") || ends_with_ci(name, ".bin") || ends_with_ci(name, ".if2")) return MT_IF2;
    // by content (a web upload or a file without a known name)
    if (n >= 7 && memcmp(d, "ZXTape!", 7) == 0) return MT_TAPE;
    if (n >= 22 && memcmp(d, "Compressed Square Wave", 22) == 0) return MT_TAPE;
    if (n >= 4 && memcmp(d, "PZXT", 4) == 0) return MT_TAPE;
    if (n >= 4 && memcmp(d, "RZX!", 4) == 0) return MT_RZX;
    if (n >= 8 && (memcmp(d, "MV - CPC", 8) == 0 || memcmp(d, "EXTENDED", 8) == 0)) return MT_DSK;
    if (n >= 8 && memcmp(d, "SINCLAIR", 8) == 0) return MT_TRD;
    if (n == 49179 || n == 131103 || n == 147487) return MT_SNAP;
    if (n >= 0x900 && n % 0x1000 == 0 && d[0x8e7] == 0x10) return MT_TRD;   // TR-DOS id byte
    return MT_TAPE;                                                          // a .tap has no signature
}

static void apply_snap(zxspec *m, const zx_snap *s)
{
    zd_z80 *z = &m->cpu;
    z->a = s->a; z->f = s->f; z->b = s->b; z->c = s->c;
    z->d = s->d; z->e = s->e; z->h = s->h; z->l = s->l;
    z->a2 = s->a2; z->f2 = s->f2; z->b2 = s->b2; z->c2 = s->c2;
    z->d2 = s->d2; z->e2 = s->e2; z->h2 = s->h2; z->l2 = s->l2;
    z->ix = s->ix; z->iy = s->iy; z->sp = s->sp;
    z->i = s->i; z->r = s->r; z->im = s->im;
    z->iff1 = s->iff1; z->iff2 = s->iff2;
    z->pc = s->pc;
    z->halted = false;
    zd_set_irq(z, ZD_CLEAR, 0xff);
    m->border = s->border & 7;
    if (m->kind == ZK_128 || m->kind == ZK_PLUS3) zx128_apply_snap(m, s);
    else if (m->kind == ZK_CLONE) zxclone_apply_snap(m, s);
}

static bool load_snapshot(zxspec *m, const uint8_t *d, size_t n, const char *ext)
{
    zx_snap s;
    uint8_t *banks[8];
    for (int b = 0; b < 8; b++) banks[b] = m->ram[b];
    if (!zx_snap_load(d, n, ext, m->kind == ZK_48, banks, &s, &m->error)) return false;
    apply_snap(m, &s);
    return true;
}

static bool load_if2(zxspec *m, const uint8_t *d, size_t n)
{
    if (m->kind != ZK_128 && m->kind != ZK_PLUS3) {
        m->error = "Interface 2 cartridges: 128K / +3 only";
        return false;
    }
    if (n < 0x4000) {
        m->error = "IF2 ROM too small (need 16K or 32K)";
        return false;
    }
    if (!m->if2_rom && !(m->if2_rom = malloc(0x8000))) {
        m->error = "out of memory for the cartridge";
        return false;
    }
    memset(m->if2_rom, 0xff, 0x8000);
    const size_t k = n < 0x8000 ? n : 0x8000;
    memcpy(m->if2_rom, d, k);
    if (k < 0x8000) memcpy(m->if2_rom + 0x4000, m->if2_rom, 0x4000);  // 16K mirrored
    m->if2_switched = false;
    m->if2_delay = 0;
    return true;
}

static bool load(zxspec *m, const uint8_t *d, uint8_t *owned, size_t n, const char *name)
{
    m->error = NULL;
    switch (media_type(name, d, n)) {
    case MT_TAPE: {
        bool ok = owned ? zt_load_take(&m->tape, owned, n, &m->error) : zt_load(&m->tape, d, n, &m->error);
        // stopped until the ROM loader is entered (playing from boot would
        // run through the pilot before LOAD "")
        zt_stop(&m->tape);
        m->tape_held = false;
        return ok;
    }
    case MT_SNAP: {
        bool ok = load_snapshot(m, d, n, name);
        free(owned);
        return ok;
    }
    case MT_RZX: {
        zx_rzx_stop(&m->rzx);
        bool ok = zx_rzx_load(&m->rzx, d, n, &m->error);
        free(owned);
        if (!ok) return false;
        ok = load_snapshot(m, m->rzx.snap, m->rzx.snap_len, m->rzx.snap_ext);
        zx_rzx_drop_snap(&m->rzx);
        if (ok) zx_rzx_start(&m->rzx);
        return ok;
    }
    case MT_TRD: {
        bool ok = false;
        if (m->kind == ZK_CLONE) ok = zx_beta_load(&m->beta, d, n, &m->error);
        else m->error = "TR-DOS disks (.trd / .scl): Pentagon / Scorpion only";
        free(owned);
        return ok;
    }
    case MT_DSK:
        if (m->kind != ZK_PLUS3) {
            free(owned);
            m->error = "+3 disks (.dsk): Spectrum +3 only";
            return false;
        }
        if (owned ? nec_load_disk_take(&m->fdc, 0, owned, n, &m->error) : nec_load_disk(&m->fdc, 0, d, n, &m->error)) {
            m->disk_present = true;
            return true;
        }
        return false;
    case MT_IF2: {
        bool ok = load_if2(m, d, n);
        free(owned);
        if (ok) zxspec_reset(m);        // a cartridge starts the machine from it
        return ok;
    }
    default:
        free(owned);
        m->error = "unknown media";
        return false;
    }
}

bool zxspec_load_mem(zxspec *m, const uint8_t *data, size_t n, const char *name_hint)
{
    if (!m || !data || n == 0) return false;
    return load(m, data, NULL, n, name_hint ? name_hint : "");
}

bool zxspec_load_take(zxspec *m, uint8_t *data, size_t n, const char *name_hint)
{
    if (!m || !data || n == 0) {
        free(data);
        return false;
    }
    return load(m, data, data, n, name_hint ? name_hint : "");
}

bool zxspec_load_file(zxspec *m, const char *path)
{
    if (!m || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->error = "cannot open the file";
        return false;
    }
    long sz = -1;
    if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        m->error = "empty or too big";
        return false;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        m->error = "cannot read the file";
        return false;
    }
    fclose(f);
    return zxspec_load_take(m, buf, (size_t)sz, path);
}

bool zxspec_trd_begin(zxspec *m, size_t size)
{
    if (!m) return false;
    m->error = NULL;
    if (m->kind != ZK_CLONE) {
        m->error = "TR-DOS disks (.trd / .scl): Pentagon / Scorpion only";
        return false;
    }
    return zx_beta_stream_begin(&m->beta, size, &m->error);
}

bool zxspec_trd_data(zxspec *m, const uint8_t *data, size_t n)
{
    return m && zx_beta_stream_data(&m->beta, data, n, &m->error);
}

bool zxspec_trd_end(zxspec *m)
{
    return m && zx_beta_stream_end(&m->beta, &m->error);
}

const char *zxspec_last_error(const zxspec *m) { return m && m->error ? m->error : ""; }

void zxspec_tape_play(zxspec *m)
{
    if (!m || !zt_is_loaded(&m->tape)) return;
    m->tape_held = false;
    zt_play(&m->tape, !zt_is_paused(&m->tape));
}

void zxspec_tape_stop(zxspec *m)
{
    if (!m) return;
    zt_stop(&m->tape);
    m->ear = 0;
    m->tape_held = true;
}

void zxspec_tape_toggle(zxspec *m)
{
    if (!m || !zt_is_loaded(&m->tape)) return;
    if (zt_is_playing(&m->tape)) {
        zt_pause(&m->tape);
        m->tape_held = true;
    } else {
        zxspec_tape_play(m);
    }
}

bool zxspec_tape_loaded(const zxspec *m) { return m && zt_is_loaded(&m->tape); }
bool zxspec_tape_playing(const zxspec *m) { return m && zt_is_playing(&m->tape); }
bool zxspec_rzx_playing(const zxspec *m) { return m && m->rzx.playing; }

// ---------------------------------------------------------------------------
// picture out
// ---------------------------------------------------------------------------
int zxspec_width(const zxspec *m) { return m ? m->pic_w : ZX_PIC_W; }
int zxspec_height(const zxspec *m) { return m ? m->pic_h : ZX_PIC_H; }

void zxspec_render(const zxspec *m, uint32_t *rgb)
{
    if (!m || !rgb) return;
    const int n = m->pic_w * m->pic_h;
    if (m->packed) {
        const uint32_t *pal = m->palette_ext + (ulaplus_on(m) ? 16 : 0);
        for (int i = 0; i < n; i += 2) {
            const uint8_t b = m->pic[i / 2];
            rgb[i] = pal[b & 15];
            rgb[i + 1] = pal[b >> 4];
        }
    } else {
        for (int i = 0; i < n; i++) rgb[i] = m->palette_ext[m->pic[i] < 80 ? m->pic[i] : 0];
    }
}

void ZX_HOT zxspec_render_row565(const zxspec *m, int y, uint16_t *out)
{
    if (!m || !out || y < 0 || y >= ZX_H) return;
    if (m->packed) {
        // with the picture limited to 4 bits, ULA+ shows its first bank (dsp-cpp)
        const uint16_t *pal = m->pal565 + (ulaplus_on(m) ? 16 : 0);
        const uint8_t *row = m->pic + (size_t)(y + (m->pic_h - ZX_H) / 2) * (size_t)(m->pic_w / 2) +
                             (m->pic_w - ZX_W) / 4;
        for (int x = 0; x < ZX_W; x += 2) {
            const uint8_t b = row[x / 2];
            out[x] = pal[b & 15];
            out[x + 1] = pal[b >> 4];
        }
    } else {
        const uint8_t *row = m->pic + (size_t)(y + (m->pic_h - ZX_H) / 2) * (size_t)m->pic_w + (m->pic_w - ZX_W) / 2;
        for (int x = 0; x < ZX_W; x++) out[x] = m->pal565[row[x] < 80 ? row[x] : 0];
    }
}

const char *zxspec_title(zx_model_t model)
{
    switch (model) {
    case ZX_SPEC48:       return "ZX Spectrum 48K";
    case ZX_SPEC128:      return "ZX Spectrum 128K";
    case ZX_PLUS3:        return "ZX Spectrum +3";
    case ZX_PENTAGON:     return "Pentagon 128";
    case ZX_PENTAGON1024: return "Pentagon 1024";
    case ZX_SCORPION:     return "Scorpion ZS-256";
    default:              return "ZX Spectrum";
    }
}

zx_model_t zxspec_model(const zxspec *m) { return m ? m->model : ZX_SPEC48; }
double zxspec_fps(const zxspec *m) { return m ? m->fps : ZX_FPS; }
int zxspec_ram_kb(const zxspec *m)
{
    if (!m) return 0;
    return m->kind == ZK_48 ? 48 : m->ram_pages * 16;
}
