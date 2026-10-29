// z8002.c - Zilog Z8002. See z8002.h; the opcodes are in z8002_ops.inc.
#include "z8002_ops.inc"

#define Z8_HOT Z8_RAM

static bool tables_ready;

void z8002_init(z8002 *c)
{
    if (!tables_ready) {
        for (int i = 0; i < 256; i++) {
            int p = (i ^ (i >> 1) ^ (i >> 2) ^ (i >> 3) ^ (i >> 4) ^ (i >> 5) ^ (i >> 6) ^ (i >> 7)) & 1;
            z8000_zsp[i] = (uint8_t)(((i == 0) ? F_Z : 0) | ((i & 128) ? F_S : 0) | (p ? 0 : F_PV));
        }
        tables_ready = true;
    }
    uint8_t (*rd)(void *, uint16_t) = c->read;
    void (*wr)(void *, uint16_t, uint8_t) = c->write;
    uint16_t (*rd16)(void *, uint16_t) = c->read16;
    void *user = c->user;
    const uint8_t *fetch = c->fetch;
    uint32_t fetch_end = c->fetch_end;
    memset(c, 0, sizeof *c);
    c->fetch = fetch;
    c->fetch_end = fetch_end;
    c->read = rd;
    c->write = wr;
    c->read16 = rd16;
    c->user = user;
}

void z8002_reset(z8002 *c)
{
    c->op[0] = c->op[1] = c->op[2] = c->op[3] = 0;
    c->ppc = c->pc = 0;
    c->psapseg = c->psapoff = 0;
    c->fcw = 0;
    c->refresh = 0;
    c->nspseg = c->nspoff = 0;
    c->irq_req = 0;
    c->irq_vec = 0;
    c->op_valid = 0;
    memset(&c->regs, 0, sizeof c->regs);
    c->nmi_state = 0;
    c->irq_state[0] = c->irq_state[1] = c->irq_state[2] = 0;
    c->mi = 0;
    c->halt = false;
    c->irq_req |= Z8000_RESET;
    c->refresh &= 0x7fff;
}

void z8002_set_reset_line(z8002 *c, bool held)
{
    if (held && !c->reset_held) z8002_reset(c);
    c->reset_held = held;
}

// As MAME's execute_set_input: every assert re-queues NVI while NVIE is set
// (Pole Position leaves NVI asserted and relies on the vblank re-assert).
void z8002_set_nvi(z8002 *c, bool assert)
{
    c->irq_state[0] = assert ? ASSERT_LINE : CLEAR_LINE;
    if (!assert) {
        if (!(c->fcw & F_NVIE)) c->irq_req &= (uint8_t)~Z8000_NVI;
    } else {
        if (c->fcw & F_NVIE) c->irq_req |= Z8000_NVI;
    }
}

static void Z8_HOT Interrupt(void)
{
    uint16_t fcw = m_fcw;
    if (m_irq_req & Z8000_RESET) {
        m_pc = get_reset_pc();
        m_irq_req &= (uint8_t)(Z8000_NVI | Z8000_VI);
        CHANGE_FCW(RDMEM_W(m_program, 2));
        return;
    }
#define TAKE(flag, vecaddr, vec)                                    \
    do {                                                            \
        CHANGE_FCW((uint16_t)(fcw | F_S_N | F_SEG_Z8001()));        \
        PUSH_PC();                                                  \
        PUSHW(SP, fcw);                                             \
        PUSHW(SP, (uint16_t)(vec));                                 \
        m_pc = (vecaddr);                                           \
        m_irq_req &= (uint8_t)~(flag);                              \
    } while (0)
    if (m_irq_req & Z8000_EPU) { TAKE(Z8000_EPU, GET_PC(EPU), m_op[0]); CHANGE_FCW(GET_FCW(EPU)); return; }
    if (m_irq_req & Z8000_TRAP) { TAKE(Z8000_TRAP, GET_PC(TRAP), m_op[0]); CHANGE_FCW(GET_FCW(TRAP)); return; }
    if (m_irq_req & Z8000_SYSCALL) { TAKE(Z8000_SYSCALL, GET_PC(SYSCALL), m_op[0]); CHANGE_FCW(GET_FCW(SYSCALL)); return; }
    if (m_irq_req & Z8000_NMI) {
        m_irq_vec = 0xffff;
        m_halt = false;
        TAKE(Z8000_NMI, GET_PC(NMI), m_irq_vec);
        CHANGE_FCW(GET_FCW(NMI));
        return;
    }
    if (m_irq_req & Z8000_SEGTRAP) {
        m_irq_vec = 0xffff;
        TAKE(Z8000_SEGTRAP, GET_PC(SEGTRAP), m_irq_vec);
        CHANGE_FCW(GET_FCW(SEGTRAP));
        return;
    }
    if ((m_irq_req & Z8000_VI) && (m_fcw & F_VIE)) {
        m_irq_vec = 0xffff;
        m_halt = false;
        TAKE(Z8000_VI, read_irq_vector(), m_irq_vec);
        CHANGE_FCW(GET_FCW(VI));
        return;
    }
    if ((m_irq_req & Z8000_NVI) && (m_fcw & F_NVIE)) {
        m_irq_vec = 0xffff;
        m_halt = false;
        TAKE(Z8000_NVI, GET_PC(NVI), m_irq_vec);
        CHANGE_FCW(GET_FCW(NVI));
    }
#undef TAKE
}

int Z8_HOT z8002_run(z8002 *c, int cycles)
{
    if (c->reset_held) return cycles;
    Z = c;
    m_icount = cycles;
    const int start = m_icount;
    do {
        if (m_irq_req) Interrupt();
        m_ppc = m_pc;
        if (m_halt) {
            m_icount = 0;
            break;
        }
        m_op[0] = RDOP();
        m_op_valid = 1;
        const z8000_init *e = &table[z8002_exec_ent[z8002_exec_blk[m_op[0] >> 4] * 16 + (m_op[0] & 15)]];
        m_icount -= e->cycles;
        e->opcode();
        m_op_valid = 0;
    } while (m_icount > 0);
    return start - m_icount;
}
