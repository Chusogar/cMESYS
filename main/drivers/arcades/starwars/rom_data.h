// rom_data.h - the Star Wars and The Empire Strikes Back ROMs: built in
// (rom_data.c, made by tools/gen_roms_starwars.py; run tools/regen.sh
// starwars) or from storage (rom_load.c, main/rom/romfs.h: roms/starwars/,
// roms/esb/ or MAME's starwars.zip / esb.zip). NULL until then.
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
// Star Wars (MAME "starwars")
#define SW_MAIN_ROM_LEN (0xC000)
extern const uint8_t *sw_main_rom;   // 136021.214 (16K), .102, .203, .104, .206 (8K each)
#define SW_VEC_ROM_LEN (0x1000)
extern const uint8_t *sw_vec_rom;   // 136021-105 vector ROM
#define SW_SND_ROM_LEN (0x4000)
extern const uint8_t *sw_snd_rom;   // 136021-107, -208 sound program
#define SW_AVG_PROM_LEN (0x100)
extern const uint8_t *sw_avg_prom;   // 136021-109 AVG state machine
#define SW_MATH_PROM_LEN (0x1000)
extern const uint8_t *sw_math_prom;   // 136021-110..113 mathbox microcode
// The Empire Strikes Back (MAME "esb")
#define ESB_MAIN_ROM_LEN (0x10000)
extern const uint8_t *esb_main_rom;   // 136031-101, -102, -203, -104 (16K each)
#define ESB_SLAP_ROM_LEN (0x8000)
extern const uint8_t *esb_slap_rom;   // 136031-105, -106: the slapstic's banks
#define ESB_VEC_ROM_LEN (0x1000)
extern const uint8_t *esb_vec_rom;   // 136031-111 vector ROM
#define ESB_SND_ROM_LEN (0x8000)
extern const uint8_t *esb_snd_rom;   // 136031-113, -112 sound program
#define ESB_AVG_PROM_LEN (0x100)
extern const uint8_t *esb_avg_prom;   // 136021-109 AVG state machine
#define ESB_MATH_PROM_LEN (0x1000)
extern const uint8_t *esb_math_prom;   // 136031-110..107 mathbox microcode
// The ROMs of one game (SW_GAME_*): built in, or from storage; false if
// they are not there (romfs_error()).
bool sw_roms_get(int game);
#endif
