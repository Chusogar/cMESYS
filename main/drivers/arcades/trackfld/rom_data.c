// rom_data.c - trackfld without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh trackfld with
// them in roms/trackfld/ to build them in.
#include "rom_data.h"
#include <stddef.h>
const uint8_t *tf_main_rom = NULL;
const uint8_t *tf_audio_rom = NULL;
const uint8_t *tf_vlm_rom = NULL;
const uint8_t *tf_prom = NULL;
const uint8_t *tf_chr_pix = NULL;
const uint8_t *tf_spr_pix = NULL;
