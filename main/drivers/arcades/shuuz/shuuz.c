// shuuz.c - Atari Games' Shuuz (1990) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/shuuz (after MAME's
// atari/shuuz.cpp). A 68000 at 7.16 MHz (here Musashi: dsp-cpp has its own
// core, so the timing drifts apart from it), the VAD's 64x64 playfield of 8x8
// tiles, motion objects drawn in SLIP bands of 8 lines, an OKI M6295 for the
// sound and the trackball (here on the joystick).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/atari_mo.h"

#define W 336
#define H 240
#define SCANLINES 262
#define CPU_SYNC 4
#define FPS 59.922743
#define MAIN_CLOCK (14318181 / 2)
#define OKI_CLOCK (14318181 / 16)
#define RATE 44100
#define PF_REGION 0x80000
#define SPR_REGION 0x100000
#define MO_W 512
#define MO_H 512
#define MO_NONE 0xffff

static const char *const kSets[] = { "shuuz", "shuuz2", NULL };
const arc_info arc_game = { "shuuz", "Shuuz", "Atari Games", 1990, W, H, FPS, RATE, kSets,
                            { "BOTON 1", "BOTON 2", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {{"136083-4010.23p", 0x20000, 0, 0x1c2459f8}, {"136083-4011.13p", 0x20000, 1, 0x6db53a85}};
static const arc_rom kPf[] = {
    {"136083-2030.43x", 0x20000, 0x000000, 0x8ecf1ed8}, {"136083-2032.20x", 0x20000, 0x020000, 0x5af184e6},
    {"136083-2031.87x", 0x20000, 0x040000, 0x72e9db63}, {"136083-2033.65x", 0x20000, 0x060000, 0x8f552498}};
static const arc_rom kSpr[] = {
    {"136083-1020.43u", 0x20000, 0x000000, 0xd21ad039}, {"136083-1022.20u", 0x20000, 0x020000, 0x0c10bc90},
    {"136083-1024.43m", 0x20000, 0x040000, 0xadb09347}, {"136083-1026.20m", 0x20000, 0x060000, 0x9b20e13d},
    {"136083-1021.87u", 0x20000, 0x080000, 0x8388910c}, {"136083-1023.65u", 0x20000, 0x0a0000, 0x71353112},
    {"136083-1025.87m", 0x20000, 0x0c0000, 0xf7b20a64}, {"136083-1027.65m", 0x20000, 0x0e0000, 0x55d54952}};
static const arc_rom kOki[] = {{"136083-1040.75b", 0x20000, 0x00000, 0x0896702b}, {"136083-1041.65b", 0x20000, 0x20000, 0xb3b07ce9}};

// MAME's pfmolayout: 8x8, 4 planes over the two halves (the first one the MSB)
static const int kX[8] = {0, 1, 2, 3, 8, 9, 10, 11}, kY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const arc_layout kPfL = {8, 8, PF_REGION / 2 / 16, 4, 128, false, false, false, {0, 4, PF_REGION / 2 * 8, PF_REGION / 2 * 8 + 4}, kX, kY};
static const arc_layout kSprL = {8, 8, SPR_REGION / 2 / 16, 4, 128, false, false, false, {0, 4, SPR_REGION / 2 * 8, SPR_REGION / 2 * 8 + 4}, kX, kY};

static const atari_mo_config kMoCfg = {
    .tile_width = 8, .tile_height = 8, .bankcount = 1, .linked = true, .split = false,
    .maxperline = 0, .palettebase = 0, .link_entry = {0x00ff, 0, 0, 0},
    .code_entry = {{0, 0x7fff, 0, 0}, {0, 0, 0, 0}}, .color_entry = {{0, 0, 0x000f, 0}, {0, 0, 0, 0}},
    .xpos_entry = {0, 0, 0xff80, 0}, .ypos_entry = {0, 0, 0, 0xff80}, .width_entry = {0, 0, 0, 0x0070},
    .height_entry = {0, 0, 0, 0x0007}, .hflip_entry = {0, 0x8000, 0, 0}, .slipheight = 8,
};

static struct state {
    uint16_t rom[0x20000];
    uint8_t *pf_gfx, *spr_gfx, *oki_rom;
    okim6295d oki;
    atari_mo mo;
    uint16_t playfield[0x1000], pf_ext[0x1000], eof[0x40], slip[0x40], ram[0x4000], vad[0x20], pal_ram[0x400];
    uint8_t eeprom[0x800];
    uint16_t pal[0x400];
    uint16_t mo_index[W * H];
    int scanline, irq_scanline, line_clip0, line_clip1;
    bool hblank, eeprom_unlocked, irq_armed, service;
    uint32_t pf0_xscroll, pf1_xscroll, pf0_yscroll, mo_xscroll, mo_yscroll;
    uint16_t system_port, buttons_port;
    int16_t leta[2];
    int8_t dx, dy;
    int64_t audio_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------
// The 68000 (dsp-cpp's core)

static m68kd cpu;

static void set_irq(int level, bool on) { m68kd_set_irq(&cpu, level, on ? M68_ASSERT : M68_CLEAR); }

static uint8_t pal6(uint8_t b) { b &= 0x3f; return (uint8_t)((b << 2) | (b >> 4)); }

static void set_palette(int i, uint16_t v)
{
    i &= 0x3ff;
    S.pal_ram[i] = v;
    const uint8_t in = (v >> 15) & 1;
    const unsigned r = pal6((uint8_t)(((v >> 9) & 0x3e) | in)), g = pal6((uint8_t)(((v >> 4) & 0x3e) | in)), b = pal6((uint8_t)(((v << 1) & 0x3e) | in));
    S.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void vad_update_parameter(uint16_t w)
{
    const uint32_t s = ((uint32_t)w >> 7) & 0x1ff;
    switch (w & 15) {
    case 9: S.mo_xscroll = s; break;
    case 10: S.pf1_xscroll = s; break;
    case 11: S.pf0_xscroll = s; break;
    case 13: S.mo_yscroll = s; break;
    case 15: S.pf0_yscroll = s; break;
    default: break;
    }
}

static void vad_w(int o, uint16_t v)
{
    o &= 0x1f;
    S.vad[o] = v;
    if (o == 3) {
        S.irq_scanline = v & 0x1ff;
        S.irq_armed = true;
    } else if (o >= 0x10 && o <= 0x1b) vad_update_parameter(v);
    else if (o == 0x1e) set_irq(4, false);
}

static uint16_t vad_r(int o)
{
    if (o == 0) {
        uint16_t r = (uint16_t)(S.scanline > 255 ? 255 : S.scanline);
        if (S.scanline >= H) r |= 0x4000;
        return r;
    }
    return S.vad[o & 0x1f];
}

static uint16_t read16(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x40000) return S.rom[a >> 1];
    if (a >= 0x100000 && a <= 0x100fff) return (uint16_t)(0xff00 | S.eeprom[(a & 0xfff) >> 1]);
    if (a >= 0x103000 && a <= 0x103003) {
        const int w = (a >> 1) & 1;
        if (w == 0) {
            S.leta[0] = (int16_t)(S.dx + S.dy);
            S.leta[1] = (int16_t)(S.dx - S.dy);
        }
        return (uint16_t)S.leta[w];
    }
    if (a >= 0x105000 && a <= 0x105001) {
        uint16_t r = S.system_port;
        if (S.scanline >= H) r &= (uint16_t)~0x0800;
        else r |= 0x0800;
        if ((r & 0x0800) && S.hblank) r &= (uint16_t)~0x0800;
        return r;
    }
    if (a >= 0x105002 && a <= 0x105003) return S.buttons_port;
    if (a >= 0x106000 && a <= 0x106001) return (uint16_t)(0xff00 | okid_read(&S.oki));
    if (a >= 0x107000 && a <= 0x107007) return 0xffff;
    if (a >= 0x3e0000 && a <= 0x3e07ff) return S.pal_ram[(a & 0x7ff) >> 1];
    if (a >= 0x3effc0 && a <= 0x3effff) return vad_r((int)((a - 0x3effc0) >> 1));
    if (a >= 0x3f4000 && a <= 0x3f5eff) return S.playfield[(a - 0x3f4000) >> 1];
    if (a >= 0x3f5f00 && a <= 0x3f5f7f) return S.eof[(a - 0x3f5f00) >> 1];
    if (a >= 0x3f5f80 && a <= 0x3f5fff) return S.slip[(a - 0x3f5f80) >> 1];
    if (a >= 0x3f6000 && a <= 0x3f7fff) return S.pf_ext[(a - 0x3f6000) >> 1];
    if (a >= 0x3f8000 && a <= 0x3fffff) return S.ram[(a - 0x3f8000) >> 1];
    return 0xffff;
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a < 0x40000) return;
    if (a >= 0x100000 && a <= 0x100fff) {
        if (S.eeprom_unlocked) {
            S.eeprom[(a & 0xfff) >> 1] = (uint8_t)v;
            S.eeprom_unlocked = false;
        }
    } else if (a >= 0x101000 && a <= 0x101fff) S.eeprom_unlocked = true;
    else if (a >= 0x106000 && a <= 0x106001) okid_write(&S.oki, (uint8_t)v);
    else if (a >= 0x3e0000 && a <= 0x3e07ff) set_palette((int)((a & 0x7ff) >> 1), v);
    else if (a >= 0x3effc0 && a <= 0x3effff) vad_w((int)((a - 0x3effc0) >> 1), v);
    else if (a >= 0x3f4000 && a <= 0x3f5eff) {
        const size_t o = (a - 0x3f4000) >> 1;
        S.playfield[o] = v;
        if (S.vad[0x0a] & 0x80) S.pf_ext[o] = (uint16_t)((S.pf_ext[o] & 0x00ff) | (S.vad[0x1c] & 0xff00));
    } else if (a >= 0x3f5f00 && a <= 0x3f5f7f) S.eof[(a - 0x3f5f00) >> 1] = v;
    else if (a >= 0x3f5f80 && a <= 0x3f5fff) S.slip[(a - 0x3f5f80) >> 1] = v;
    else if (a >= 0x3f6000 && a <= 0x3f7fff) S.pf_ext[(a - 0x3f6000) >> 1] = v;
    else if (a >= 0x3f8000 && a <= 0x3fffff) S.ram[(a - 0x3f8000) >> 1] = v;
}

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return read16(a); }
static void cpu_wr(void *u, uint32_t a, uint16_t v) { (void)u; write16(a, v); }
static void on_cycles(int c);
static void cpu_cyc(void *u, int c) { (void)u; on_cycles(c); }

static void on_cycles(int c)
{
    S.oki_acc += (int64_t)c * okid_sample_frequency(&S.oki);
    while (S.oki_acc >= MAIN_CLOCK) {
        S.oki_acc -= MAIN_CLOCK;
        S.last_oki = okid_update(&S.oki);
    }
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= MAIN_CLOCK) {
        S.audio_acc -= MAIN_CLOCK;
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(S.last_oki);
    }
}

// ---------------------------------------------------------------------------
// Video

static int mo_code(void *u, int raw) { (void)u; return raw; }
static int mo_color(void *u, int i) { (void)u; return i; }

static void mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx, int prio)
{
    (void)u, (void)gfx, (void)prio;
    const int sx = x >= W ? x - MO_W : x, sy = y >= H ? y - MO_H : y;
    for (int r = 0; r < 8; r++) {
        const int ty = sy + r;
        if (ty < S.line_clip0 || ty > S.line_clip1) continue;
        const int srow = vflip ? 7 - r : r;
        for (int c = 0; c < 8; c++) {
            const int tx = sx + c;
            if (tx < 0 || tx >= W) continue;
            const unsigned pen = arc_gfx_px(&kSprL, S.spr_gfx, SPR_REGION, code, hflip ? 7 - c : c, srow);
            if (pen) S.mo_index[ty * W + tx] = (uint16_t)(color + pen);
        }
    }
}

static void mo_band(int line)
{
    static const atari_mo_lookup lk = {mo_code, mo_color, NULL};
    S.line_clip0 = line;
    S.line_clip1 = line + 7 < H - 1 ? line + 7 : H - 1;
    for (int y = line; y <= S.line_clip1; y++)
        for (int x = 0; x < W; x++) S.mo_index[y * W + x] = MO_NONE;
    const int band = atari_mo_band_for_line(&S.mo, line, (int)S.mo_yscroll);
    atari_mo_draw_band(&S.mo, &S.ram[(0x3fd000 - 0x3f8000) >> 1], S.slip, band, (int)S.mo_xscroll, (int)S.mo_yscroll, -1, &lk, mo_tile, NULL);
}

static void render_line(int line)
{
    const int scrollx = (int)(S.pf0_xscroll + (S.pf1_xscroll & 7)), sy = (line + (int)S.pf0_yscroll) & 0x1ff;
    const int row = sy >> 3, prow = sy & 7;
    uint16_t *d = S.fb + line * W;
    for (int x = 0; x < W; x++) {
        const int sx = (x + scrollx) & 0x1ff;
        const int t = (sx >> 3) * 64 + row;
        const uint16_t data = S.playfield[t];
        int col = sx & 7;
        if (data & 0x8000) col = 7 - col;
        const unsigned pen = arc_gfx_px(&kPfL, S.pf_gfx, PF_REGION, data & 0x3fff, col, prow);
        int index = 0x100 + ((S.pf_ext[t] >> 8) & 0x0f) * 16 + (int)pen;
        const uint16_t obj = S.mo_index[line * W + x];
        if (obj != MO_NONE) {
            const bool o13 = (index & 0xf0) == 0xf0;
            const bool mopf = ((index & 0x80) ? ((obj & 0xc0) == 0xc0) : ((obj & 0xc0) != 0xc0)) && !o13;
            if (mopf) {
                if (obj & 0x0e) index = obj;
                else if (obj & 0x01) index |= 0x200;
            }
        }
        d[x] = S.pal[index & 0x3ff];
    }
}

// ---------------------------------------------------------------------------

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *prog = (uint8_t *)calloc(1, 0x40000), *t = (uint8_t *)malloc(0x20000);
    S.pf_gfx = (uint8_t *)calloc(1, PF_REGION);
    S.spr_gfx = (uint8_t *)calloc(1, SPR_REGION);
    S.oki_rom = (uint8_t *)calloc(1, 0x40000);
    if (!prog || !t || !S.pf_gfx || !S.spr_gfx || !S.oki_rom) return false;
    bool ok = true;
    for (int i = 0; i < 2 && ok; i++) {             // the even and odd bytes
        arc_rom r = kMain[i];
        r.offset = 0;
        memset(t, 0, 0x20000);
        ok = arc_load(&r, 1, t, 0x20000);
        for (uint32_t b = 0; b < 0x20000; b++) prog[(kMain[i].offset & ~1u) + b * 2 + (kMain[i].offset & 1u)] = t[b];
    }
    for (int i = 0; i < 0x40000; i += 2) S.rom[i >> 1] = (uint16_t)(prog[i] << 8 | prog[i + 1]);
    free(prog);
    free(t);
    ok = ok && ARC_LOAD(kPf, S.pf_gfx, PF_REGION) && ARC_LOAD(kSpr, S.spr_gfx, SPR_REGION) && ARC_LOAD(kOki, S.oki_rom, 0x40000);
    if (!ok) return false;
    for (int i = 0; i < PF_REGION; i++) S.pf_gfx[i] = (uint8_t)~S.pf_gfx[i];    // ROMREGION_INVERT
    for (int i = 0; i < SPR_REGION; i++) S.spr_gfx[i] = (uint8_t)~S.spr_gfx[i];
    memset(S.eeprom, 0xff, sizeof S.eeprom);
    okid_init(&S.oki, OKI_CLOCK, true, S.oki_rom, 0x40000);
    atari_mo_init(&S.mo, &kMoCfg, W + 8, H + 8);
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    cpu.cycles = cpu_cyc;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    okid_reset(&S.oki);
    S.oki.divisor = 132;                            // (pin 7 high)
    memset(S.playfield, 0, sizeof S.playfield);
    memset(S.pf_ext, 0, sizeof S.pf_ext);
    memset(S.eof, 0, sizeof S.eof);
    memset(S.slip, 0, sizeof S.slip);
    memset(S.ram, 0, sizeof S.ram);
    memset(S.vad, 0, sizeof S.vad);
    memset(S.pal_ram, 0, sizeof S.pal_ram);
    memset(S.pal, 0, sizeof S.pal);
    for (int i = 0; i < W * H; i++) S.mo_index[i] = MO_NONE;
    S.scanline = 0;
    S.hblank = S.eeprom_unlocked = S.irq_armed = false;
    S.pf0_xscroll = S.pf1_xscroll = S.pf0_yscroll = S.mo_xscroll = S.mo_yscroll = 0;
    S.irq_scanline = 0;
    S.mo.bank = 0;
    S.system_port = S.buttons_port = 0xffff;
    S.leta[0] = S.leta[1] = 0;
    S.dx = S.dy = 0;
    S.audio_acc = S.oki_acc = 0;
    S.last_oki = 0;
    m68kd_reset(&cpu);
}

int arc_run_frame(const int16_t **audio)
{
    const int cycles = (int)((double)MAIN_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    S.naudio = 0;
    for (int i = 0; i < 0x1c; i++)                  // the end of frame (EOF) parameters
        if (S.eof[i]) vad_w(i, S.eof[i]);
    for (int line = 0; line < SCANLINES; line++) {
        S.scanline = line;
        if (S.irq_armed && line == S.irq_scanline) {
            set_irq(4, true);
            S.irq_armed = false;
        }
        for (int step = 0; step < CPU_SYNC; step++) {
            S.hblank = step == CPU_SYNC - 1;
            m68kd_run(&cpu, cycles);
        }
        S.hblank = false;
        if (line < H) {
            if ((line & 7) == 0) mo_band(line);
            render_line(line);
        }
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    S.system_port = 0xffff;
    if (in->coin1) S.system_port &= (uint16_t)~1;
    if (in->coin2) S.system_port &= (uint16_t)~2;
    S.buttons_port = 0xffff;
    if (p & AP_B1) S.buttons_port &= (uint16_t)~1;
    if (p & AP_B2) S.buttons_port &= (uint16_t)~2;
    if (S.service) S.buttons_port &= (uint16_t)~0x0800;
    int dx = 0, dy = 0;
    if (p & AP_RIGHT) dx += 8;
    if (p & AP_LEFT) dx -= 8;
    if (p & AP_UP) dy += 8;
    if (p & AP_DOWN) dy -= 8;
    S.dx = (int8_t)dx;
    S.dy = (int8_t)dy;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.service = v & 1;
}
