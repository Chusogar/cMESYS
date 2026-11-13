// m_ql.c - the Sinclair QL (drivers/computers/ql/ql_core.c) for the Android
// port (core.h). ROMs: [0] the 48 KB system ROM (JS, JM, Minerva...), [1]
// the IPC's 2 KB 8749 program. Media: QLAY .mdv cartridges: slot 0 = MDV1
// and boot it (F1 at the start-up prompt), 1 = MDV1, 2 = MDV2.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "ql_core.h"
#include "ql_mdv.h"
#include "zx8301.h"

static uint8_t *rom, *ipc;                 // kept: the core reads them in place
static uint8_t *ram[QL_RAM_BLOCKS];
static uint8_t *cart[2];                   // cartridge images (mdv1, mdv2)
static uint32_t palette[8];
static uint8_t held[MC_KEYS];

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model;
    if (n < 2 || !roms[0] || !roms[1] || lens[0] != QL_ROM_BYTES || lens[1] != QL_IPC_BYTES) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    for (int i = 2; i < n; i++) free(roms[i]);
    for (int b = 0; b < QL_RAM_BLOCKS; b++) {
        if (!ram[b]) ram[b] = (uint8_t *)malloc(0x8000);
        if (!ram[b]) return false;
        memset(ram[b], 0, 0x8000);
    }
    // (a new ROM: the cartridges stay in the drives)
    uint8_t *old_rom = rom, *old_ipc = ipc;
    rom = roms[0];
    ipc = roms[1];
    ql_init(rom, ipc, ram, (uint32_t)unix_time);
    free(old_rom);
    free(old_ipc);
    for (int d = 0; d < 2; d++) ql_cart_insert(d, cart[d]);
    for (int c = 0; c < 8; c++) palette[c] = 0xff000000u | zx8301_rgb(c);
    memset(held, 0, sizeof held);
    return true;
}

void mc_reset(void) { ql_reset(); }
int mc_width(void) { return QL_W; }
int mc_height(void) { return QL_H; }
double mc_fps(void) { return QL_FPS; }
int mc_audio_rate(void) { return QL_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    ql_keys_clear();
    for (int c = 0; c < 64; c++)
        if (keys[c]) ql_key(c, true);
    return ql_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    const uint8_t *scr = ql_screen();
    uint8_t ctrl = ql_video_control();
    bool flash = ql_flash();
    uint8_t row[QL_W];
    for (int y = 0; y < QL_H; y++) {
        zx8301_render_row(scr, ctrl, flash, y, row);
        uint32_t *o = argb + y * QL_W;
        for (int x = 0; x < QL_W; x++) o[x] = palette[row[x] & 7];
    }
}

void mc_joy(int port, unsigned bits) { (void)port, (void)bits; }   // the QL's joysticks are keys

const char *mc_load(const char *path, int slot)
{
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    uint8_t *img = (uint8_t *)malloc(QL_MDV_IMAGE_BYTES + 1);
    size_t n = img ? fread(img, 1, QL_MDV_IMAGE_BYTES + 1, f) : 0;
    fclose(f);
    if (!img) return "out of memory";
    if (n != QL_MDV_IMAGE_BYTES) {
        free(img);
        return "not a QLAY .mdv cartridge (174930 bytes)";
    }
    if (slot == 0) {
        ql_mount(img);
        free(cart[0]);
        cart[0] = img;
    } else {
        int d = slot == 2 ? 1 : 0;
        ql_cart_insert(d, img);
        free(cart[d]);
        cart[d] = img;
    }
    return NULL;
}

void mc_eject(int slot)
{
    int d = slot == 2 ? 1 : 0;
    ql_cart_insert(d, NULL);
    free(cart[d]);
    cart[d] = NULL;
}

void mc_turbo(bool on) { (void)on; }

int mc_status(void)
{
    return (ql_cart_motor(0) ? MC_LED1 : 0) | (ql_cart_motor(1) ? MC_LED2 : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_AUTOBOOT) ql_autoboot();
}
