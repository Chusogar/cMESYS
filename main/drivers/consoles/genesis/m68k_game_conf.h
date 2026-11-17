/* Musashi set-up for the Mega Drive / Genesis (68000), included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
#ifndef M68K_INSTRUCTION_CALLBACK
#define M68K_INSTRUCTION_CALLBACK() do { } while (0)
#endif
