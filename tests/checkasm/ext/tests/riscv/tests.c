#include "tests.h"

#if ARCH_RISCV

/* Re-use helpers from main checkasm library */
#include "src/cpu.h"

uint64_t selftest_get_cpu_flags_riscv(void)
{
    uint64_t flags = SELFTEST_CPU_FLAG_RVI;
    if (checkasm_has_float())
        flags |= SELFTEST_CPU_FLAG_RVF;
    if (checkasm_has_vector())
        flags |= SELFTEST_CPU_FLAG_RVV;
    return flags;
}

DEF_COPY_FUNC(copy_rvv);
DEF_COPY_GETTER(SELFTEST_CPU_FLAG_RVV, copy_rvv)

DEF_NOOP_FUNC(clobber_ra);
DEF_NOOP_FUNC(clobber_sp);
DEF_NOOP_FUNC(clobber_gp);
DEF_NOOP_FUNC(clobber_t0);
DEF_NOOP_FUNC(clobber_t1);
DEF_NOOP_FUNC(clobber_t2);
DEF_NOOP_FUNC(clobber_s0);
DEF_NOOP_FUNC(clobber_s1);
DEF_NOOP_FUNC(clobber_a0);
DEF_NOOP_FUNC(clobber_a1);
DEF_NOOP_FUNC(clobber_a2);
DEF_NOOP_FUNC(clobber_a3);
DEF_NOOP_FUNC(clobber_a4);
DEF_NOOP_FUNC(clobber_a5);
DEF_NOOP_FUNC(clobber_a6);
DEF_NOOP_FUNC(clobber_a7);
DEF_NOOP_FUNC(clobber_s2);
DEF_NOOP_FUNC(clobber_s3);
DEF_NOOP_FUNC(clobber_s4);
DEF_NOOP_FUNC(clobber_s5);
DEF_NOOP_FUNC(clobber_s6);
DEF_NOOP_FUNC(clobber_s7);
DEF_NOOP_FUNC(clobber_s8);
DEF_NOOP_FUNC(clobber_s9);
DEF_NOOP_FUNC(clobber_s10);
DEF_NOOP_FUNC(clobber_s11);
DEF_NOOP_FUNC(clobber_t3);
DEF_NOOP_FUNC(clobber_t4);
DEF_NOOP_FUNC(clobber_t5);
DEF_NOOP_FUNC(clobber_t6);

DEF_NOOP_FUNC(clobber_ft0);
DEF_NOOP_FUNC(clobber_ft1);
DEF_NOOP_FUNC(clobber_ft2);
DEF_NOOP_FUNC(clobber_ft3);
DEF_NOOP_FUNC(clobber_ft4);
DEF_NOOP_FUNC(clobber_ft5);
DEF_NOOP_FUNC(clobber_ft6);
DEF_NOOP_FUNC(clobber_ft7);
DEF_NOOP_FUNC(clobber_fs0);
DEF_NOOP_FUNC(clobber_fs1);
DEF_NOOP_FUNC(clobber_fa0);
DEF_NOOP_FUNC(clobber_fa1);
DEF_NOOP_FUNC(clobber_fa2);
DEF_NOOP_FUNC(clobber_fa3);
DEF_NOOP_FUNC(clobber_fa4);
DEF_NOOP_FUNC(clobber_fa5);
DEF_NOOP_FUNC(clobber_fa6);
DEF_NOOP_FUNC(clobber_fa7);
DEF_NOOP_FUNC(clobber_fs2);
DEF_NOOP_FUNC(clobber_fs3);
DEF_NOOP_FUNC(clobber_fs4);
DEF_NOOP_FUNC(clobber_fs5);
DEF_NOOP_FUNC(clobber_fs6);
DEF_NOOP_FUNC(clobber_fs7);
DEF_NOOP_FUNC(clobber_fs8);
DEF_NOOP_FUNC(clobber_fs9);
DEF_NOOP_FUNC(clobber_fs10);
DEF_NOOP_FUNC(clobber_fs11);
DEF_NOOP_FUNC(clobber_ft8);
DEF_NOOP_FUNC(clobber_ft9);
DEF_NOOP_FUNC(clobber_ft10);
DEF_NOOP_FUNC(clobber_ft11);

DEF_NOOP_FUNC(sigill_riscv);
DEF_NOOP_FUNC(corrupt_stack_riscv);
DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_RVI, sigill_riscv)
DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_RVI, corrupt_stack_riscv)

typedef struct RiscvRegister {
    const char *name;
    noop_func  *clobber;
} RiscvRegister;

static const RiscvRegister registers_safe[] = {
    { "ra", selftest_clobber_ra },
    { "t0", selftest_clobber_t0 },
    { "t1", selftest_clobber_t1 },
    { "t2", selftest_clobber_t2 },
    { "t3", selftest_clobber_t3 },
    { "t4", selftest_clobber_t4 },
    { "t5", selftest_clobber_t5 },
    { "t6", selftest_clobber_t6 },
    { "a0", selftest_clobber_a0 },
    { "a1", selftest_clobber_a1 },
    { "a2", selftest_clobber_a2 },
    { "a3", selftest_clobber_a3 },
    { "a4", selftest_clobber_a4 },
    { "a5", selftest_clobber_a5 },
    { "a6", selftest_clobber_a6 },
    { "a7", selftest_clobber_a7 },
    { NULL, NULL                }
};

static const RiscvRegister float_registers_safe[] = {
    { "ft0",  selftest_clobber_ft0  },
    { "ft1",  selftest_clobber_ft1  },
    { "ft2",  selftest_clobber_ft2  },
    { "ft3",  selftest_clobber_ft3  },
    { "ft4",  selftest_clobber_ft4  },
    { "ft5",  selftest_clobber_ft5  },
    { "ft6",  selftest_clobber_ft6  },
    { "ft7",  selftest_clobber_ft7  },
#ifdef __riscv_float_abi_soft
    { "fs0",  selftest_clobber_fs0  },
    { "fs1",  selftest_clobber_fs1  },
#endif
    { "fa0",  selftest_clobber_fa0  },
    { "fa1",  selftest_clobber_fa1  },
    { "fa2",  selftest_clobber_fa2  },
    { "fa3",  selftest_clobber_fa3  },
    { "fa4",  selftest_clobber_fa4  },
    { "fa5",  selftest_clobber_fa5  },
    { "fa6",  selftest_clobber_fa6  },
    { "fa7",  selftest_clobber_fa7  },
#ifdef __riscv_float_abi_soft
    { "fs2",  selftest_clobber_fs2  },
    { "fs3",  selftest_clobber_fs3  },
    { "fs4",  selftest_clobber_fs4  },
    { "fs5",  selftest_clobber_fs5  },
    { "fs6",  selftest_clobber_fs6  },
    { "fs7",  selftest_clobber_fs7  },
    { "fs8",  selftest_clobber_fs8  },
    { "fs9",  selftest_clobber_fs9  },
    { "fs10", selftest_clobber_fs10 },
    { "fs11", selftest_clobber_fs11 },
#endif
    { "ft8",  selftest_clobber_ft8  },
    { "ft9",  selftest_clobber_ft9  },
    { "ft10", selftest_clobber_ft10 },
    { "ft11", selftest_clobber_ft11 },
    { NULL,   NULL                  },
};

static const RiscvRegister registers_unsafe[] = {
    { "s0",  selftest_clobber_s0  },
    { "s1",  selftest_clobber_s1  },
    { "s2",  selftest_clobber_s2  },
    { "s3",  selftest_clobber_s3  },
    { "s4",  selftest_clobber_s4  },
    { "s5",  selftest_clobber_s5  },
    { "s6",  selftest_clobber_s6  },
    { "s7",  selftest_clobber_s7  },
    { "s8",  selftest_clobber_s8  },
    { "s9",  selftest_clobber_s9  },
    { "s10", selftest_clobber_s10 },
    { "s11", selftest_clobber_s11 },
    { "sp",  selftest_clobber_sp  },
    { "gp",  selftest_clobber_gp  },
    /* Can't clobber tp because checkasm.S saves registers in TLS */
    { NULL,  NULL                 }
};

static const RiscvRegister float_registers_unsafe[] = {
#ifndef __riscv_float_abi_soft
    { "fs0",  selftest_clobber_fs0  },
    { "fs1",  selftest_clobber_fs1  },
    { "fs2",  selftest_clobber_fs2  },
    { "fs3",  selftest_clobber_fs3  },
    { "fs4",  selftest_clobber_fs4  },
    { "fs5",  selftest_clobber_fs5  },
    { "fs6",  selftest_clobber_fs6  },
    { "fs7",  selftest_clobber_fs7  },
    { "fs8",  selftest_clobber_fs8  },
    { "fs9",  selftest_clobber_fs9  },
    { "fs10", selftest_clobber_fs10 },
    { "fs11", selftest_clobber_fs11 },
#endif
    { NULL,  NULL                 }
};

static void check_clobber(uint64_t mask, unsigned char letter,
                          const RiscvRegister *registers)
{
    const uint64_t flag = checkasm_get_cpu_flags() & mask;
    checkasm_declare(void, int);

    for (int i = 0; registers[i].name; i++) {
        noop_func *const func = flag ? registers[i].clobber : NULL;
        if (checkasm_check_func(func, "clobber_%s", registers[i].name)) {
            checkasm_call_new(0);
        }
    }

    checkasm_report("clobber_%c", letter);
}

void selftest_check_riscv(void)
{
    selftest_test_copy(get_copy_rvv(), "copy_rvv", 1);
    check_clobber(SELFTEST_CPU_FLAG_RVI, 'x', registers_safe);
    check_clobber(SELFTEST_CPU_FLAG_RVF, 'f', float_registers_safe);

    if (!checkasm_should_fail(1))
        return;
    selftest_test_noop(get_sigill_riscv(), "sigill");
    selftest_test_noop(get_corrupt_stack_riscv(), "corrupt_stack");
    check_clobber(SELFTEST_CPU_FLAG_RVI, 'x', registers_unsafe);
    check_clobber(SELFTEST_CPU_FLAG_RVF, 'f', float_registers_unsafe);
}

#endif
