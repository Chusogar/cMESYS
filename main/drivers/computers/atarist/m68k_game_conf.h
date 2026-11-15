/* Musashi set-up for the Atari ST, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
/* dsp-cpp models the prefetch queue: copy-protected loaders patch the
 * instruction words the 68000 has already fetched */
#define M68K_EMULATE_PREFETCH       OPT_ON
/* An interrupt line that changed inside a slice (the MFP, an acknowledge)
 * is applied before the next instruction */
extern _Bool st_ipl_dirty;
#define M68K_INSTRUCTION_CALLBACK() do { if (st_ipl_dirty) cpu_instr_callback(REG_PC); } while (0)
