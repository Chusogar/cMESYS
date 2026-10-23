/* Musashi set-up for the Sinclair QL, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
/* no per-instruction hook: the IPC and the microdrives run between slices */
#define M68K_INSTRUCTION_CALLBACK() do { } while (0)
