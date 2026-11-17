// psx_xa_adpcm.h - the PlayStation CD-ROM XA-ADPCM decoder (see the .c).
#ifndef PSX_XA_ADPCM_H
#define PSX_XA_ADPCM_H
#include <stdbool.h>
#include <stdint.h>

#define XA_MAX_OUT 19000      // samples (interleaved L / R) a sector can give

int decode_xa_adpcm_sector(const uint8_t *sector, uint8_t coding_info, int16_t *out);
#endif
