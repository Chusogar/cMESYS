// ql_cart_host.h - runtime microdrive cartridge selection for the QL frontends.
// Keeps the current image/name and mounts it with ql_mount / ql_mount_reader.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ql_core.h"

#define QL_CART_HOST_NAME_MAX 80

const char *ql_cart_host_name(void);
bool ql_cart_host_loaded(void);

// Load a full QLAY .mdv (exactly QL_MDV_IMAGE_BYTES) into a heap buffer and mount it.
bool ql_cart_host_load_bytes(const void *data, size_t n, const char *name);
// Desktop/web: read a path from disk into RAM and mount it.
bool ql_cart_host_load_file(const char *path);
// Embedded SD (or other): on-demand sector reader; fn/user must outlive the mount.
bool ql_cart_host_load_reader(ql_cart_sector_fn fn, void *user, const char *name);
// Mount the cartridge baked into the firmware (rom_data), if any.
bool ql_cart_host_firmware(void);
void ql_cart_host_eject(void);
// F1 / MDV button: if mounted, eject; else remount the last selected cartridge.
void ql_cart_host_toggle(void);
