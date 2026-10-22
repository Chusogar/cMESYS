unsigned int cpu_read_byte(unsigned int a){return 0;} unsigned int cpu_read_word(unsigned int a){return 0;} unsigned int cpu_read_long(unsigned int a){return 0;}
unsigned int cpu_read_word_dasm(unsigned int a){return 0;} unsigned int cpu_read_long_dasm(unsigned int a){return 0;}
void cpu_write_byte(unsigned int a,unsigned int v){} void cpu_write_word(unsigned int a,unsigned int v){} void cpu_write_long(unsigned int a,unsigned int v){} void cpu_write_long_pd(unsigned int a,unsigned int v){}
void cpu_pulse_reset(void){} void cpu_instr_callback(unsigned pc){} int cpu_irq_ack(int l){return -1;}
unsigned int m68k_read_immediate_16(unsigned int a){return 0;} unsigned int m68k_read_immediate_32(unsigned int a){return 0;}
unsigned int m68k_read_pcrelative_8(unsigned int a){return 0;} unsigned int m68k_read_pcrelative_16(unsigned int a){return 0;} unsigned int m68k_read_pcrelative_32(unsigned int a){return 0;}
_Bool as1_ipl_dirty;   /* referenced by the Indy m68k_game_conf.h hook test */
_Bool gt_ipl_dirty;    /* referenced by the Gauntlet m68k_game_conf.h hook test */
