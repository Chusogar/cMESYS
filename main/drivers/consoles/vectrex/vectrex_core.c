// vectrex_core.c - GCE / Milton Bradley Vectrex (1982): a C11 migration of
// dsp-cpp's drivers/consoles/vectrex (its analog side after jhawthorn's
// vecx). The 6809 at 1.5 MHz (cpu/m6809dsp: dsp-cpp's core and timing), the 6522 VIA (machine/via6522)
// driving the DAC, the sample & holds, the integrators and the beam, the
// AY-3-8912 (sound/ay8910) with the four buttons. Every vector the beam
// traces in a frame is drawn antialiased with its brightness, the last
// frame fading as the phosphor does; here at 192x240 (dsp-cpp: 512x640).
// The BIOS (8 KB, rom_data.c) is needed: built in, or loaded as the
// cartridge (exec_rom.bin, by its CRC: then it plays Mine Storm).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../machine/via6522.h"
#include "../../../sound/ay8910.h"
#include "rom_data.h"

#define CPU_CLOCK 1500000
#define FPS 50
#define RATE 44100
#define ALG_X 33000
#define ALG_Y 41000
#define HALF_X (ALG_X / 2)
#define HALF_Y (ALG_Y / 2)
#define W 192
#define H 240
#define PERSISTENCE 0.45f
#define BIOS_CRC 0xba13fb57u
#define MAX_SEGS 4096

static const char *const kExts[] = { ".vec", ".bin", ".gam", NULL };
const cons_info cons_machine = {
    "vectrex", "Vectrex", "GCE, 1982", kExts, "VEC BIN", W, H, FPS, RATE,
    { "1", "2", "3", "4" }, NULL, NULL, CONS_NO_KEYS, 1, NULL,
};

typedef struct { float x0, y0, x1, y1, v; } segment;

static struct {
    m6809d cpu;
    via6522 via;
    ay8910 ay;
    uint8_t bios[0x2000];
    bool have_bios;
    uint8_t *cart;
    size_t cart_size;
    uint8_t ram[0x400];
    uint8_t porta, portb, xsh, rsh, ysh, zsh, jsh, snd_select, ay_latch;
    int dx, dy;
    float pos_x, pos_y;
    bool zero_active;
    int frame_carry;
    bool vectoring;
    float vx0, vy0, vx1, vy1, vint;
    int vdx, vdy;
    uint8_t vzsh;
    segment *segs;
    int nsegs;
    uint8_t *frame, *glow;
    uint16_t *fb;
    uint16_t gray[256];
    uint8_t buttons;
    int8_t joy_x, joy_y;
    bool via_irq;
    int16_t audio[1024];
    int naudio;
    uint64_t audio_acc;
    bool loaded;
} V;

// ---- the memory ----
static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return a < V.cart_size ? V.cart[a] : 0xff;
    if (a >= 0xe000) return V.bios[a & 0x1fff];
    if ((a & 0xe000) == 0xc000) {
        if (a & 0x1000) return via6522_read(&V.via, (uint8_t)(a & 0x0f));
        if (a & 0x0800) return V.ram[a & 0x3ff];
    }
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x8000 || a >= 0xe000) return;
    if ((a & 0xe000) == 0xc000) {
        if (a & 0x0800) V.ram[a & 0x3ff] = v;
        if (a & 0x1000) via6522_write(&V.via, (uint8_t)(a & 0x0f), v);
    }
}


// ---- the VIA's ports (vecx) ----
static uint8_t joy_pot(int ch)
{
    switch (ch) {
    case 0: return (uint8_t)(128 + V.joy_x);
    case 1: return (uint8_t)(128 + V.joy_y);
    default: return 128;
    }
}

static void snd_update(void)
{
    switch (V.portb & 0x18) {
    case 0x10:
        if (V.snd_select != 14) ay8910_data_w(&V.ay, V.porta);
        break;
    case 0x18:
        if ((V.porta & 0xf0) == 0) {
            V.snd_select = V.porta & 0x0f;
            ay8910_address_w(&V.ay, V.porta);
        }
        break;
    default:
        break;
    }
}

static void alg_update(void)
{
    switch (V.portb & 0x06) {
    case 0x00:
        V.jsh = joy_pot(0);
        if (!(V.portb & 0x01)) V.ysh = V.xsh;
        break;
    case 0x02:
        V.jsh = joy_pot(1);
        if (!(V.portb & 0x01)) V.rsh = V.xsh;
        break;
    case 0x04:
        V.jsh = joy_pot(2);
        if (!(V.portb & 0x01)) V.zsh = V.xsh > 0x80 ? (uint8_t)(V.xsh - 0x80) : 0;
        break;
    default:
        V.jsh = joy_pot(3);
        break;
    }
    V.dx = (int)V.xsh - (int)V.rsh;
    V.dy = (int)V.rsh - (int)V.ysh;
}

static uint8_t pa_in(void *u)
{
    (void)u;
    if ((V.portb & 0x18) == 0x08) {
        if (V.snd_select == 14) return (uint8_t)(0xf0 | (~V.buttons & 0x0f));   // the buttons on port A
        return ay8910_data_r(&V.ay);
    }
    return V.porta;
}

static uint8_t pb_in(void *u)
{
    (void)u;
    uint8_t v = V.portb;
    if (V.via.acr & 0x80) v = (uint8_t)((v & 0x7f) | (via6522_out_b(&V.via) & 0x80));
    if (V.jsh > V.xsh) v |= 0x20;
    else v &= (uint8_t)~0x20;
    return v;
}

static void pa_out(void *u, uint8_t v)
{
    (void)u;
    V.porta = v;
    V.xsh = v ^ 0x80;
    snd_update();
    alg_update();
}

static void pb_out(void *u, uint8_t v)
{
    (void)u;
    V.portb = v;
    snd_update();
    alg_update();
}

static void ca2(void *u, bool level) { (void)u; V.zero_active = !level; }
static void via_irq(void *u, bool on) { (void)u; V.via_irq = on; m6809d_set_irq(&V.cpu, on ? M6809D_ASSERT : M6809D_CLEAR); }

// ---- the beam (vecx alg_sstep) ----
static inline bool on_screen(float x, float y) { return x > -HALF_X && x < HALF_X && y > -HALF_Y && y < HALF_Y; }

static void add_segment(void)
{
    if (V.nsegs < MAX_SEGS) V.segs[V.nsegs++] = (segment){ V.vx0, V.vy0, V.vx1, V.vy1, V.vint };
}

static void start_vector(float sdx, float sdy)
{
    V.vx0 = V.vx1 = V.pos_x;
    V.vy0 = V.vy1 = V.pos_y;
    V.vdx = (int)sdx;
    V.vdy = (int)sdy;
    V.vzsh = V.zsh;
    V.vint = (float)V.zsh / 127.0f;
}

static void step_one_cycle(void)
{
    via6522_tick(&V.via, 1);
    const bool unblanked = (V.via.acr & 0x10) ? V.via.shift_out_bit : V.via.hs_cb2;
    float sdx = 0, sdy = 0;
    if (V.zero_active) {
        sdx = -V.pos_x;
        sdy = -V.pos_y;
    } else {
        const bool ramp = (V.via.acr & 0x80) ? (via6522_out_b(&V.via) & 0x80) != 0 : (V.portb & 0x80) != 0;
        if (!ramp) {
            sdx = (float)V.dx;
            sdy = (float)V.dy;
        }
    }
    if (!V.vectoring) {
        if (unblanked && on_screen(V.pos_x, V.pos_y)) {
            V.vectoring = true;
            start_vector(sdx, sdy);
        }
    } else if (!unblanked) {
        add_segment();
        V.vectoring = false;
    } else if ((int)sdx != V.vdx || (int)sdy != V.vdy || V.zsh != V.vzsh) {
        add_segment();
        if (on_screen(V.pos_x, V.pos_y)) start_vector(sdx, sdy);
        else V.vectoring = false;
    }
    V.pos_x += sdx;
    V.pos_y += sdy;
    if (V.vectoring && on_screen(V.pos_x, V.pos_y)) {
        V.vx1 = V.pos_x;
        V.vy1 = V.pos_y;
    }
}

static void cycles(int c)
{
    for (int i = 0; i < c; i++) step_one_cycle();
    V.audio_acc += (uint64_t)c * RATE;
    while (V.audio_acc >= CPU_CLOCK) {
        V.audio_acc -= CPU_CLOCK;
        int32_t s = ay8910_render(&V.ay);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (V.naudio < 1024) V.audio[V.naudio++] = (int16_t)s;
    }
}

static void cpu_cycles(void *u, int c) { (void)u; cycles(c); }

// ---- the picture ----
static inline void plot(int x, int y, float v)
{
    if (x < 0 || x >= W || y < 0 || y >= H || v <= 0) return;
    uint8_t *p = &V.frame[y * W + x];
    const int n = *p + (int)(v * 255.0f + 0.5f);
    *p = (uint8_t)(n > 255 ? 255 : n);
}

static void draw_line(float x0, float y0, float x1, float y1, float v)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    if (len < 0.5f) {
        const int px = (int)floorf(x0), py = (int)floorf(y0);
        plot(px, py, v);
        plot(px + 1, py, v * 0.35f);
        plot(px - 1, py, v * 0.35f);
        plot(px, py + 1, v * 0.35f);
        plot(px, py - 1, v * 0.35f);
        return;
    }
    const int steps = (int)ceilf(len);
    const bool steep = fabsf(dy) > fabsf(dx);
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / (float)steps, fx = x0 + dx * t, fy = y0 + dy * t;
        if (steep) {
            const int py = (int)floorf(fy + 0.5f), px = (int)floorf(fx);
            const float f = fx - (float)px;
            plot(px, py, v * (1 - f));
            plot(px + 1, py, v * f);
            plot(px - 1, py, v * 0.2f * (1 - f));
            plot(px + 2, py, v * 0.2f * f);
        } else {
            const int px = (int)floorf(fx + 0.5f), py = (int)floorf(fy);
            const float f = fy - (float)py;
            plot(px, py, v * (1 - f));
            plot(px, py + 1, v * f);
            plot(px, py - 1, v * 0.2f * (1 - f));
            plot(px, py + 2, v * 0.2f * f);
        }
    }
}

static void render_vectors(void)
{
    if (V.vectoring) {
        add_segment();
        V.vx0 = V.vx1;
        V.vy0 = V.vy1;
    }
    memset(V.frame, 0, W * H);
    const float sx = (float)W / ALG_X, sy = (float)H / ALG_Y, cx = W * 0.5f, cy = H * 0.5f;
    for (int i = 0; i < V.nsegs; i++) {
        const segment *s = &V.segs[i];
        if (s->v <= 0) continue;
        const float v = s->v * 1.25f;
        draw_line(cx + s->x0 * sx, cy + s->y0 * sy, cx + s->x1 * sx, cy + s->y1 * sy, v < 1 ? v : 1);
    }
    V.nsegs = 0;
    for (int i = 0; i < W * H; i++) {
        const int faded = (int)(V.glow[i] * PERSISTENCE);
        const uint8_t g = (uint8_t)(faded > V.frame[i] ? faded : V.frame[i]);
        V.glow[i] = g;
        V.fb[i] = V.gray[g];
    }
}

// ---- the console ----
bool cons_init(void)
{
    V.segs = (segment *)malloc(MAX_SEGS * sizeof(segment));
    V.frame = (uint8_t *)calloc(1, W * H);
    V.glow = (uint8_t *)calloc(1, W * H);
    V.fb = (uint16_t *)calloc(W * H, sizeof(uint16_t));
    if (!V.segs || !V.frame || !V.glow || !V.fb) return false;
    for (int i = 0; i < 256; i++) {
        const int v = (int)(powf((float)i / 255.0f, 0.8f) * 255.0f + 0.5f);
        V.gray[i] = (uint16_t)(((v & 0xf8) << 8) | ((v & 0xfc) << 3) | (v >> 3));
    }
    if (vectrex_rom_bios) {
        memcpy(V.bios, vectrex_rom_bios, sizeof V.bios);
        V.have_bios = true;
    }
    V.cpu.read = rd;
    V.cpu.write = wr;
    V.cpu.cycles = cpu_cycles;
    V.via.pa_in = pa_in;
    V.via.pb_in = pb_in;
    V.via.pa_out = pa_out;
    V.via.pb_out = pb_out;
    V.via.ca2_out = ca2;
    V.via.irq = via_irq;
    ay8910_init(&V.ay, CPU_CLOCK, RATE);
    cons_reset();
    return true;
}

void cons_reset(void)
{
    memset(V.ram, 0, sizeof V.ram);
    via6522_reset(&V.via);
    ay8910_reset(&V.ay);
    V.porta = V.portb = 0;
    V.xsh = V.rsh = V.ysh = V.jsh = 128;
    V.zsh = 0;
    V.dx = V.dy = 0;
    V.snd_select = 0;
    V.zero_active = false;
    V.frame_carry = 0;
    V.pos_x = V.pos_y = 0;
    V.vectoring = false;
    V.nsegs = 0;
    memset(V.glow, 0, W * H);
    memset(V.fb, 0, W * H * sizeof(uint16_t));
    V.audio_acc = 0;
    V.via_irq = false;
    if (V.have_bios) m6809d_reset(&V.cpu);
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || !size) {
        if (why) *why = "empty file";
        free(data);
        return false;
    }
    if (size == 0x2000 && (cons_crc32(data, size) == BIOS_CRC || data[0x1ffe] >= 0xe0)) {   // the BIOS itself (its reset vector in $E000-$FFFF)
        memcpy(V.bios, data, 0x2000);
        V.have_bios = true;
        free(data);
        free(V.cart);
        V.cart = NULL;
        V.cart_size = 0;
        V.loaded = true;
        cons_reset();
        return true;
    }
    if (!V.have_bios) {
        if (why) *why = "the Vectrex BIOS is needed first (load exec_rom.bin)";
        free(data);
        return false;
    }
    if (size > 0x8000) size = 0x8000;
    free(V.cart);
    V.cart = data;
    V.cart_size = size;
    V.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(V.cart);
    V.cart = NULL;
    V.cart_size = 0;
    V.loaded = V.have_bios;
    cons_reset();
}

bool cons_loaded(void) { return V.loaded; }

int cons_run_frame(const int16_t **audio)
{
    V.naudio = 0;
    *audio = V.audio;
    if (!V.have_bios) {
        for (int i = 0; i < RATE / FPS; i++) V.audio[i] = 0;
        return RATE / FPS;
    }
    int period = CPU_CLOCK / FPS;
    const uint16_t t2 = (uint16_t)((V.via.t2lh << 8) | V.via.t2ll);   // the BIOS frames with T2 = 30000
    if (t2 >= 20000 && t2 <= 60000) period = t2;
    int remaining = period - V.frame_carry;
    while (remaining > 0) {
        const int ran = m6809d_run(&V.cpu, remaining < 1000 ? remaining : 1000);
        if (ran <= 0) break;
        remaining -= ran;
    }
    V.frame_carry = remaining < 0 ? -remaining : 0;
    render_vectors();
    return V.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = W;
    *h = H;
    *stride = W;
    return V.fb;
}

void cons_pad(int player, unsigned b)
{
    if (player != 0) return;
    V.buttons = (uint8_t)(((b & CP_B1) ? 1 : 0) | ((b & CP_B2) ? 2 : 0) | ((b & CP_B3) ? 4 : 0) | ((b & CP_B4) ? 8 : 0));
    V.joy_x = (int8_t)((b & CP_LEFT) ? -127 : (b & CP_RIGHT) ? 127 : 0);
    V.joy_y = (int8_t)((b & CP_DOWN) ? -127 : (b & CP_UP) ? 127 : 0);
}

void cons_key(int key, bool down) { (void)key, (void)down; }

const char *cons_status(void)
{
    if (!V.have_bios) return "no BIOS: load exec_rom.bin";
    return V.cart_size ? NULL : "Mine Storm";
}
