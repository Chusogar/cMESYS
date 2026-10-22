/* Musashi set-up for Gauntlet, included by m68kconf.h */
#define M68K_EMULATE_010            OPT_ON    /* Gauntlet has a 68010 */
#define M68K_SEPARATE_READS         OPT_ON    /* opcode fetches told apart (SLAPSTIC) */
#define M68K_EMULATE_PREFETCH       OPT_OFF
/* The driver's hook only matters after an interrupt line changed inside the
 * CPU or at its fast-forwarded busy loop: tested here, inline. */
extern _Bool gt_ipl_dirty;
#define M68K_INSTRUCTION_CALLBACK() do { if (gt_ipl_dirty || REG_PC == 0x40c22 || REG_PC == 0x47f0) cpu_instr_callback(REG_PC); } while (0)
