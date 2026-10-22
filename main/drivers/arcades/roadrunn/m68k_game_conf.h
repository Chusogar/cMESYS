/* Musashi set-up for Road Runner (Atari System 1), included by m68kconf.h */
#define M68K_EMULATE_010            OPT_ON    /* the System 1 has a 68010 */
#define M68K_SEPARATE_READS         OPT_ON    /* opcode fetches told apart (SLAPSTIC) */
#define M68K_EMULATE_PREFETCH       OPT_OFF   /* word-by-word fetches, as dsp-cpp's 68000 */
/* The driver's hook only matters after an interrupt line changed inside the
 * CPU or at its two fast-forwarded busy loops: tested here, inline. */
extern _Bool as1_ipl_dirty;
#define M68K_INSTRUCTION_CALLBACK() do { if (as1_ipl_dirty || REG_PC == 0x1482c || REG_PC == 0x5290) cpu_instr_callback(REG_PC); } while (0)
