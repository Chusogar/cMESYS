// vx_cpu.c - the Vectrex's 6809 (cpu/mc6809) built without setjmp() per
// instruction (faults are only flagged, see vectrex_core.c).
#define MC6809_NO_SETJMP
#include "../../../cpu/mc6809/mc6809.c"
