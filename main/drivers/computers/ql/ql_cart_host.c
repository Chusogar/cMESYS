// ql_cart_host.c - see ql_cart_host.h.
#include "ql_cart_host.h"
#include "ql_mdv.h"
#include "rom_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char name_buf[QL_CART_HOST_NAME_MAX];
static uint8_t *ram_image;
static ql_cart_sector_fn saved_fn;
static void *saved_user;
static enum { H_NONE, H_RAM, H_READER, H_FW } kind;

const char *ql_cart_host_name(void)
{
    if (name_buf[0]) return name_buf;
    if (kind == H_FW && ql_mdv_name[0]) return ql_mdv_name;
    return "(none)";
}

bool ql_cart_host_loaded(void) { return ql_cart_loaded(0); }

static void set_name(const char *name)
{
    if (!name || !name[0]) { name_buf[0] = 0; return; }
    // basename
    const char *base = name, *p = name;
    for (; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(name_buf, sizeof name_buf, "%s", base);
}

static void clear_ram(void)
{
    free(ram_image);
    ram_image = NULL;
    saved_fn = NULL;
    saved_user = NULL;
}

bool ql_cart_host_load_bytes(const void *data, size_t n, const char *name)
{
    if (!data || n != QL_MDV_IMAGE_BYTES) return false;
    uint8_t *b = (uint8_t *)malloc(QL_MDV_IMAGE_BYTES);
    if (!b) return false;
    memcpy(b, data, QL_MDV_IMAGE_BYTES);
    clear_ram();
    ram_image = b;
    kind = H_RAM;
    set_name(name);
    ql_mount(ram_image);
    return true;
}

bool ql_cart_host_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t *b = (uint8_t *)malloc(QL_MDV_IMAGE_BYTES + 1);
    if (!b) { fclose(f); return false; }
    size_t n = fread(b, 1, QL_MDV_IMAGE_BYTES + 1, f);
    fclose(f);
    if (n != QL_MDV_IMAGE_BYTES) { free(b); return false; }
    clear_ram();
    ram_image = b;
    kind = H_RAM;
    set_name(path);
    ql_mount(ram_image);
    return true;
}

bool ql_cart_host_load_reader(ql_cart_sector_fn fn, void *user, const char *name)
{
    if (!fn) return false;
    clear_ram();
    saved_fn = fn;
    saved_user = user;
    kind = H_READER;
    set_name(name);
    ql_mount_reader(fn, user);
    return true;
}

bool ql_cart_host_firmware(void)
{
    if (!ql_mdv_image) return false;
    clear_ram();
    kind = H_FW;
    set_name(ql_mdv_name);
    ql_mount(ql_mdv_image);
    return true;
}

void ql_cart_host_eject(void)
{
    ql_mount(NULL);
}

void ql_cart_host_toggle(void)
{
    if (ql_cart_loaded(0)) {
        ql_cart_host_eject();
        return;
    }
    switch (kind) {
    case H_RAM:
        if (ram_image) ql_mount(ram_image);
        break;
    case H_READER:
        if (saved_fn) ql_mount_reader(saved_fn, saved_user);
        break;
    case H_FW:
        if (ql_mdv_image) ql_mount(ql_mdv_image);
        break;
    default:
        if (ql_mdv_image) {
            kind = H_FW;
            set_name(ql_mdv_name);
            ql_mount(ql_mdv_image);
        }
        break;
    }
}
