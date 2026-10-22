/* Musashi set-up for Snow Bros (68000), included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_ON
#define M68K_INSTRUCTION_CALLBACK() cpu_instr_callback(REG_PC)
