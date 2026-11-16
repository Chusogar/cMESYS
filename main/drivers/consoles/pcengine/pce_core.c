// pce_core.c - NEC PC Engine / TurboGrafx-16 (1987): a C11 migration of
// dsp-cpp's drivers/consoles/pcengine. The HuC6280 (cpu/h6280, MAME's: the
// MMU, the timer and the interrupt controller; its PSG in
// sound/huc6280_psg), the HuC6270 VDC (video/huc6270) and the HuC6260 VCE
// (here: the 512 colours and the dot clock). HuCards (a copier header and
// bit-reversed dumps are handled), the 384 KB layout and Street Fighter II's
// mapper. The picture at its own width (256 / 336...; 512-dot modes at half
// width), 242 lines (dsp-cpp: every line doubled into 512x484).
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/h6280/h6280.h"
#include "../../../video/huc6270.h"
#include "../../../sound/huc6280_psg.h"

#define MASTER 21477270
#define CLOCK (MASTER / 3)
#define LINES 262
#define LINE_CLOCKS (1365 / 3)
#define RATE 44100
#define FB_W 352
#define FB_H 242
#define MAX_CART 0x400000

static const char *const kExts[] = { ".pce", ".bin", NULL };
const cons_info cons_machine = {
    "pcengine", "NEC PC Engine", "NEC / Hudson, 1987", kExts, "PCE", FB_W, FB_H,
    (double)MASTER / (1365.0 * LINES), RATE, { "I", "II", NULL, NULL }, "RUN", "SELECT", CONS_NO_KEYS, 1, NULL,
};

static struct {
    h6280 cpu;
    huc6270 vdc;
    huc6280_psg psg;
    // the VCE
    uint16_t vce_table[512];
    uint16_t vce_rgb[512];
    uint16_t vce_address;
    uint8_t vce_control, vce_latch;
    uint8_t *rom;
    size_t rom_size;
    uint8_t ram[0x2000];
    uint8_t backup_ram[0x800];
    uint8_t pad;
    bool pad_select, pad_clear;
    uint8_t sf2_bank;
    uint16_t *fb;
    int frame_w;
    uint16_t line[VDC_MAX_W];
    uint64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    bool loaded;
} S;

// ---- the VCE (HuC6260) ----
static void vce_refresh(int i)
{
    const uint16_t v = S.vce_table[i];
    const unsigned g = (v >> 6) & 7, r = (v >> 3) & 7, b = v & 7;
    // (3 bits a gun repeated, as dsp-cpp: (l << 5) | (l << 2) | (l >> 1))
    const unsigned R = (r << 5) | (r << 2) | (r >> 1), G = (g << 5) | (g << 2) | (g >> 1), B = (b << 5) | (b << 2) | (b >> 1);
    S.vce_rgb[i] = (uint16_t)(((R & 0xf8) << 8) | ((G & 0xfc) << 3) | (B >> 3));
}

static int vce_active_width(void)
{
    switch (S.vce_control & 3) {
    case 0: return 256;
    case 1: return 341;
    default: return 512;
    }
}

static uint8_t vce_read(uint8_t o)
{
    switch (o & 7) {
    case 4: return (uint8_t)S.vce_table[S.vce_address];
    case 5: {
        const uint8_t v = (uint8_t)((S.vce_table[S.vce_address] >> 8) & 1);
        S.vce_address = (S.vce_address + 1) & 511;
        return (uint8_t)(v | 0xfe);
    }
    default: return 0xff;
    }
}

static void vce_write(uint8_t o, uint8_t v)
{
    switch (o & 7) {
    case 0: S.vce_control = v; break;
    case 2: S.vce_address = (uint16_t)((S.vce_address & 0x100) | v); break;
    case 3: S.vce_address = (uint16_t)((S.vce_address & 0xff) | ((v & 1) << 8)); break;
    case 4:
        S.vce_latch = v;
        S.vce_table[S.vce_address] = (uint16_t)((S.vce_table[S.vce_address] & 0x100) | v);
        vce_refresh(S.vce_address);
        break;
    case 5:
        S.vce_table[S.vce_address] = (uint16_t)(((v & 1) << 8) | S.vce_latch);
        vce_refresh(S.vce_address);
        S.vce_address = (S.vce_address + 1) & 511;
        break;
    default: break;
    }
}

// ---- the memory ----
static uint32_t rom_offset(uint32_t bank)
{
    const size_t size = S.rom_size;
    if (!size) return 0;
    if (size == 0x60000) {
        const uint32_t low = bank & 0x3f;
        if (low < 0x20) return low * 0x2000;
        return 0x40000 + (low & 0x0f) * 0x2000;
    }
    if (size > 0x100000) {
        if (bank < 0x40) return bank * 0x2000;
        return 0x80000 + (uint32_t)S.sf2_bank * 0x80000 + (bank & 0x3f) * 0x2000;
    }
    return (uint32_t)(((size_t)bank * 0x2000) % size);
}

static void map_rom(void)
{
    for (uint32_t b = 0; b < 0x80; b++) {
        const uint32_t off = rom_offset(b);
        h6280_set_bank(&S.cpu, (uint8_t)b, S.rom && off + 0x2000 <= S.rom_size ? S.rom + off : NULL, NULL);
    }
}

static uint8_t rd(void *u, uint32_t a)
{
    (void)u;
    const uint32_t bank = (a >> 13) & 0xff;
    if (bank <= 0x7f) {
        if (!S.rom) return 0xff;
        const size_t off = rom_offset(bank) + (a & 0x1fff);
        return off < S.rom_size ? S.rom[off] : 0xff;
    }
    if (bank == 0xf7) return S.backup_ram[a & 0x7ff];
    if (bank >= 0xf8 && bank <= 0xfb) return S.ram[a & 0x1fff];
    if (bank == 0xff) {
        switch (a & 0x1c00) {
        case 0x0000: return huc6270_read(&S.vdc, (uint8_t)(a & 3));
        case 0x0400: return vce_read((uint8_t)(a & 7));
        default: return 0xff;
        }
    }
    return 0xff;
}

static void wr(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    const uint32_t bank = (a >> 13) & 0xff;
    if (bank <= 0x7f) {
        if (S.rom_size > 0x100000 && (a & 0x1ffc) == 0x1ff0) {
            S.sf2_bank = (uint8_t)(a & 3);
            map_rom();
        }
        return;
    }
    if (bank == 0xf7) { S.backup_ram[a & 0x7ff] = v; return; }
    if (bank >= 0xf8 && bank <= 0xfb) { S.ram[a & 0x1fff] = v; return; }
    if (bank == 0xff) {
        switch (a & 0x1c00) {
        case 0x0000: huc6270_write(&S.vdc, (uint8_t)(a & 3), v); break;
        case 0x0400: vce_write((uint8_t)(a & 7), v); break;
        default: break;
        }
    }
}

static void psg_w(void *u, uint8_t o, uint8_t v) { (void)u; huc6280_psg_write(&S.psg, o, v); }

static uint8_t port_r(void *u)
{
    (void)u;
    const uint8_t pad = S.pad_clear ? 0x00 : S.pad;
    return (uint8_t)(((S.pad_select ? pad >> 4 : pad) & 0x0f) | 0x30);
}

static void port_w(void *u, uint8_t v)
{
    (void)u;
    S.pad_select = v & 1;
    S.pad_clear = (v & 2) != 0;
}

static void vdc_irq(void *u, bool on) { (void)u; h6280_set_irq_line(&S.cpu, H6280_IRQ1, on ? H6280_ASSERT : H6280_CLEAR); }

bool cons_init(void)
{
    S.fb = (uint16_t *)calloc(FB_W * FB_H, sizeof(uint16_t));
    if (!S.fb) return false;
    h6280_init(&S.cpu);
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.psg_w = psg_w;
    S.cpu.port_r = port_r;
    S.cpu.port_w = port_w;
    for (int b = 0xf8; b <= 0xfb; b++) h6280_set_bank(&S.cpu, (uint8_t)b, S.ram, S.ram);
    S.vdc.irq = vdc_irq;
    huc6280_psg_init(&S.psg, 3579545, RATE);
    S.frame_w = 256;
    cons_reset();
    return true;
}

void cons_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    S.sf2_bank = 0;
    S.pad_select = S.pad_clear = false;
    S.pad = 0xff;
    S.audio_acc = 0;
    memset(S.fb, 0, FB_W * FB_H * sizeof(uint16_t));
    huc6270_reset(&S.vdc);
    memset(S.vce_table, 0, sizeof S.vce_table);
    for (int i = 0; i < 512; i++) vce_refresh(i);
    S.vce_address = 0;
    S.vce_control = S.vce_latch = 0;
    huc6280_psg_reset(&S.psg);
    map_rom();
    memset(S.cpu.mmr, 0, sizeof S.cpu.mmr);
    if (S.loaded) h6280_reset(&S.cpu);
}

static uint8_t bit_swap(uint8_t v)
{
    uint8_t o = 0;
    for (int b = 0; b < 8; b++)
        if (v & (1 << b)) o |= (uint8_t)(1 << (7 - b));
    return o;
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (data && size % 0x2000 == 0x200) {
        memmove(data, data + 0x200, size - 0x200);
        size -= 0x200;
    }
    if (!data || size < 0x2000) {
        if (why) *why = "HuCard image too small";
        free(data);
        return false;
    }
    if (size > MAX_CART) size = MAX_CART;
    if (data[0] == 0xaa && data[1] == 0xbb && data[2] == 0x02)
        for (size_t i = 0; i < size; i++) data[i] = bit_swap(data[i]);
    free(S.rom);
    S.rom = data;
    S.rom_size = size;
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(S.rom);
    S.rom = NULL;
    S.rom_size = 0;
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

static void blit_line(int dl, int width)
{
    if (dl < 0 || dl >= FB_H) return;
    uint16_t *d = S.fb + dl * FB_W;
    const int step = width > FB_W ? 2 : 1, w = width / step;
    for (int x = 0; x < w; x++) {
        const uint16_t idx = S.line[x * step];
        d[x] = S.vce_rgb[idx ? (idx & 511) : 0];
    }
    S.frame_w = w;
}

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    for (int line = 0; line < LINES; line++) {
        int width = huc6270_display_width(&S.vdc);
        if (vce_active_width() < width) width = vce_active_width();
        if (width > VDC_MAX_W) width = VDC_MAX_W;
        if (huc6270_scanline(&S.vdc, line, S.line, width)) blit_line(S.vdc.display_line, width);
        if (S.loaded) h6280_run(&S.cpu, LINE_CLOCKS);
        S.audio_acc += (uint64_t)LINE_CLOCKS * RATE;
        while (S.audio_acc >= CLOCK) {
            S.audio_acc -= CLOCK;
            const int16_t s = huc6280_psg_update(&S.psg);
            if (S.naudio < 1024) S.audio[S.naudio++] = s;
        }
    }
    huc6270_end_frame(&S.vdc);
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = S.frame_w;
    *h = FB_H;
    *stride = FB_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    if (player) return;
    uint8_t v = 0;
    if (!(b & CP_B1)) v |= 0x01;            // I
    if (!(b & CP_B2)) v |= 0x02;            // II
    if (!(b & (CP_SELECT | CP_B3 | CP_COIN))) v |= 0x04;
    if (!(b & CP_START)) v |= 0x08;
    if (!(b & CP_UP)) v |= 0x10;
    if (!(b & CP_RIGHT)) v |= 0x20;
    if (!(b & CP_DOWN)) v |= 0x40;
    if (!(b & CP_LEFT)) v |= 0x80;
    S.pad = v;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return NULL; }
