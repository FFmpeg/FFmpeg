#include <stdio.h>

#include "tests.h"
#include <checkasm/checkasm.h>

static const CheckasmCpuInfo cpus[] = {
    { "Bad C",           "badc",    SELFTEST_CPU_FLAG_BAD_C   },
    { "A",               "a",       SELFTEST_CPU_FLAG_A       },
    { "B",               "b",       SELFTEST_CPU_FLAG_B, .mask = SELFTEST_CPU_FLAG_A },
    { "AB",              "ab",      SELFTEST_CPU_FLAG_AB      },
#if ARCH_X86
    { "Generic x86",     "x86",     SELFTEST_CPU_FLAG_X86     },
    { "MMX",             "mmx",     SELFTEST_CPU_FLAG_MMX     },
    { "SSE",             "sse",     SELFTEST_CPU_FLAG_SSE     },
    { "AVX",             "avx",     SELFTEST_CPU_FLAG_AVX     },
    { "AVX-512",         "avx512",  SELFTEST_CPU_FLAG_AVX512  },
#endif
#if ARCH_RISCV
    { "Generic RISC-V",  "rvi",     SELFTEST_CPU_FLAG_RVI     },
    { "Floating point",  "rvf",     SELFTEST_CPU_FLAG_RVF     },
    { "Vector",          "rvv",     SELFTEST_CPU_FLAG_RVV     },
#endif
#if ARCH_AARCH64
    { "Generic aarch64", "aarch64", SELFTEST_CPU_FLAG_AARCH64 },
#endif
#if ARCH_ARM
    { "Generic ARM",     "arm",     SELFTEST_CPU_FLAG_ARM     },
    { "VFP",             "vfp",     SELFTEST_CPU_FLAG_VFP     },
    { "VFP D32",         "vfpd32",  SELFTEST_CPU_FLAG_VFPD32  },
#endif
    {0}
};

static int  seen_c, seen_a, seen_b, seen_ab, seen_any;
static void selftest_check_flags(void)
{
    seen_any = 1;

    switch (checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_AB) {
    case 0:                    seen_c  = 1; break;
    case SELFTEST_CPU_FLAG_A:  seen_a  = 1; break;
    case SELFTEST_CPU_FLAG_B:  seen_b  = 1; break;
    case SELFTEST_CPU_FLAG_AB: seen_ab = 1; break;
    }
}

static const CheckasmTest tests[] = {
    { "flags",      selftest_check_flags   },
    { "generic",    selftest_check_generic },
    { "utils",      selftest_check_utils },
#if ARCH_X86
    { "x86",        selftest_check_x86     },
#elif ARCH_RISCV
    { "riscv",      selftest_check_riscv },
#elif ARCH_AARCH64
    { "aarch64",    selftest_check_aarch64 },
#elif ARCH_ARM
    { "arm",        selftest_check_arm },
#endif
    {0}
};

int main(int argc, const char *argv[])
{
    CheckasmConfig cfg = {
        .cpu_flags = cpus,
        .tests     = tests,
        .cpu       = SELFTEST_CPU_FLAG_BAD_C | SELFTEST_CPU_FLAG_AB,
    };

#if ARCH_X86
    cfg.cpu |= selftest_get_cpu_flags_x86();
#elif ARCH_RISCV
    cfg.cpu |= selftest_get_cpu_flags_riscv();
#elif ARCH_AARCH64
    cfg.cpu |= selftest_get_cpu_flags_aarch64();
#elif ARCH_ARM
    cfg.cpu |= selftest_get_cpu_flags_arm();
#endif

    int ret = checkasm_main(&cfg, argc, argv);
    if (ret)
        return ret;

    if (seen_any && (!seen_c || !seen_a || !seen_b || !seen_ab)) {
        fprintf(stderr, "checkasm: missing expected CPU flags;\n"
                        "  seen_c=%d, seen_a=%d, seen_b=%d, seen_ab=%d\n",
                seen_c, seen_a, seen_b, seen_ab);
        return 1;
    }

    return 0;
}
