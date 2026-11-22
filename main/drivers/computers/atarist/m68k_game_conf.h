/* Musashi set-up for the Atari ST, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
/* copy-protected loaders (Top Gun's) patch words the 68000 has prefetched */
#define M68K_EMULATE_PREFETCH       OPT_ON
/* no per-instruction hook: the MFP, the floppy, the IKBD and the PSG run
   between slices, which end at their next event (st_core.c) */
#ifndef M68K_INSTRUCTION_CALLBACK
#define M68K_INSTRUCTION_CALLBACK() do { } while (0)
#endif
