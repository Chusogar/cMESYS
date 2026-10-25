/* Musashi set-up for OutRun (two 68000s sharing Musashi), included by m68kconf.h */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_ON
/* The driver's hook only matters after an interrupt line changed inside the
 * CPU, or at the fast-forwarded busy loops: tested here, inline. */
extern _Bool or_ipl_dirty;
#define M68K_INSTRUCTION_CALLBACK() do { if (or_ipl_dirty || REG_PC == 0x7dda || REG_PC == 0x7964 || REG_PC == 0x1182) cpu_instr_callback(REG_PC); } while (0)
