/* Musashi set-up for Puzzle Bobble (68000), included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_ON
/* The driver's hook (idle loop fast-forward) only looks at one address */
#define M68K_INSTRUCTION_CALLBACK() do { if (REG_PC == 0x1706) cpu_instr_callback(REG_PC); } while (0)
