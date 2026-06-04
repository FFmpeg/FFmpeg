#include "tests.h"
#include <checkasm/checkasm.h>

static const CheckasmCpuInfo cpus[] = {
    { "Bad C",           "badc",    SELFTEST_CPU_FLAG_BAD_C   },
#if ARCH_X86
    { "Generic x86",     "x86",     SELFTEST_CPU_FLAG_X86     },
    { "MMX",             "mmx",     SELFTEST_CPU_FLAG_MMX     },
    { "SSE2",            "sse2",    SELFTEST_CPU_FLAG_SSE2    },
    { "AVX-2",           "avx2",    SELFTEST_CPU_FLAG_AVX2    },
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

static const CheckasmTest tests[] = {
    { "generic",    selftest_check_generic },
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
        .cpu       = SELFTEST_CPU_FLAG_BAD_C,
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

    return checkasm_main(&cfg, argc, argv);
}
