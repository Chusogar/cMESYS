/* Musashi set-up for the Macintosh Plus, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
/* the hook before every instruction: the chips, the .Sony and resource
   traps, the interrupt level (macplus_core.c) */
#ifndef M68K_INSTRUCTION_CALLBACK
#define M68K_INSTRUCTION_CALLBACK() cpu_instr_callback(REG_PC)
#endif
