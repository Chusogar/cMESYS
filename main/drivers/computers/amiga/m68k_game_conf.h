/* Musashi set-up for the Amiga 500, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
/* no per-instruction hook: the CIAs, Paula and the beam run between slices
   (a profiling build may give its own, see amiga.lf) */
#ifndef M68K_INSTRUCTION_CALLBACK
#define M68K_INSTRUCTION_CALLBACK() do { } while (0)
#endif
