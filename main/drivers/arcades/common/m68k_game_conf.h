/* Musashi set-up for the 68000 arcades on the common frontend (Bionic
 * Commando), included by cpu/m68k/m68kconf.h: a plain 68000 with prefetch */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_ON
#define M68K_INSTRUCTION_CALLBACK() cpu_instr_callback(REG_PC)
