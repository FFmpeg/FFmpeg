#include <inttypes.h>
#include <stdio.h>

#include "tests.h"

#if ARCH_X86

/* Re-use helpers from main checkasm library */
#include "src/cpu.h"

uint64_t selftest_get_cpu_flags_x86(void)
{
    uint64_t       flags = SELFTEST_CPU_FLAG_X86;
    CpuidRegisters r;
    checkasm_cpu_cpuid(&r, 0, 0);
    const uint32_t max_leaf = r.eax;
    if (max_leaf < 1)
        return flags;

    checkasm_cpu_cpuid(&r, 1, 0);
    if (r.edx & 0x00800000) /* MMX */
        flags |= SELFTEST_CPU_FLAG_MMX;
    if (r.edx & 0x02000000) /* SSE */
        flags |= SELFTEST_CPU_FLAG_SSE;
    if (~r.ecx & 0x18000000) /* OSXSAVE/AVX */
        return flags;

    const uint64_t xcr0 = checkasm_cpu_xgetbv(0);
    if (~xcr0 & 0x6) /* XMM/YMM */
        return flags;

    flags |= SELFTEST_CPU_FLAG_AVX;

    if (max_leaf < 7 || ~xcr0 & 0xe0) /* ZMM/OPMASK */
        return flags;

    checkasm_cpu_cpuid(&r, 7, 0);
    if (r.ebx & 0x00010000) /* AVX512F */
        flags |= SELFTEST_CPU_FLAG_AVX512;
    return flags;
}

DEF_COPY_FUNC(copy_x86);
DEF_COPY_FUNC(copy_mmx);
DEF_COPY_FUNC(copy_sse);
DEF_COPY_FUNC(copy_avx);
DEF_COPY_FUNC(copy_avx512);

DEF_NOOP_FUNC(clobber_r0);
DEF_NOOP_FUNC(clobber_r1);
DEF_NOOP_FUNC(clobber_r2);
DEF_NOOP_FUNC(clobber_r3);
DEF_NOOP_FUNC(clobber_r4);
DEF_NOOP_FUNC(clobber_r5);
DEF_NOOP_FUNC(clobber_r6);
#if ARCH_X86_64
DEF_NOOP_FUNC(clobber_r7);
DEF_NOOP_FUNC(clobber_r8);
DEF_NOOP_FUNC(clobber_r9);
DEF_NOOP_FUNC(clobber_r10);
DEF_NOOP_FUNC(clobber_r11);
DEF_NOOP_FUNC(clobber_r12);
DEF_NOOP_FUNC(clobber_r13);
DEF_NOOP_FUNC(clobber_r14);
#endif

DEF_NOOP_FUNC(sigill_x86);
DEF_NOOP_FUNC(corrupt_stack_x86);

DEF_COPY_FUNC(copy_noemms_mmx);
DEF_COPY_FUNC(copy_novzeroupper_avx);

static copy_func *get_copy_x86(void)
{
    const uint64_t flags = checkasm_get_cpu_flags();
#if ARCH_X86
    if (flags & SELFTEST_CPU_FLAG_AVX512)
        return selftest_copy_avx512;
    if (flags & SELFTEST_CPU_FLAG_AVX)
        return selftest_copy_avx;
    if (flags & SELFTEST_CPU_FLAG_SSE)
        return selftest_copy_sse;
    if (flags & SELFTEST_CPU_FLAG_MMX)
        return selftest_copy_mmx;
    if (flags & SELFTEST_CPU_FLAG_X86)
        return selftest_copy_x86;
#endif
    return selftest_copy_c;
}

#if ARCH_X86_64
  #ifdef _WIN32
    #define NUM_SAFE 7
  #else
    #define NUM_SAFE 9
  #endif
  #define NUM_REGS 15
  #define STACK_ALIGN 16
#else
  #define NUM_SAFE 3
  #define NUM_REGS 7
  #define STACK_ALIGN 4
#endif

static noop_func *get_clobber(int reg)
{
    if (!(checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_X86))
        return NULL;

    switch (reg) {
    case 0: return selftest_clobber_r0;
    case 1: return selftest_clobber_r1;
    case 2: return selftest_clobber_r2;
    case 3: return selftest_clobber_r3;
    case 4: return selftest_clobber_r4;
    case 5: return selftest_clobber_r5;
    case 6: return selftest_clobber_r6;
#if ARCH_X86_64
    case 7:  return selftest_clobber_r7;
    case 8:  return selftest_clobber_r8;
    case 9:  return selftest_clobber_r9;
    case 10: return selftest_clobber_r10;
    case 11: return selftest_clobber_r11;
    case 12: return selftest_clobber_r12;
    case 13: return selftest_clobber_r13;
    case 14: return selftest_clobber_r14;
#endif
    /* can't clobber rsp without completely crashing the program */
    default: return NULL;
    }
}

DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_X86, sigill_x86)
DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_X86, corrupt_stack_x86)
DEF_COPY_GETTER(SELFTEST_CPU_FLAG_MMX, copy_noemms_mmx)
DEF_COPY_GETTER(SELFTEST_CPU_FLAG_AVX, copy_novzeroupper_avx)

uintptr_t selftest_get_stack_pointer_x86(int unused);

static void check_stack_alignment(void)
{
    checkasm_declare(uintptr_t, int);

    if (checkasm_check_func(selftest_get_stack_pointer_x86, "stack_alignment")) {
        uintptr_t sp = checkasm_call_new(0);

        /* Subtract return address */
        sp -= sizeof(sp);
        if (sp & (STACK_ALIGN - 1)) {
            if (checkasm_fail()) {
                fprintf(stderr, "stack pointer not %d-byte aligned: 0x%" PRIxPTR "\n",
                        STACK_ALIGN, sp);
            }
        }
    }
}

static void check_clobber(int from, int to)
{
    checkasm_declare(void, int);

    for (int reg = from; reg < to; reg++) {
        noop_func *clobber = get_clobber(reg);
        if (!clobber)
            break;

        if (checkasm_check_func(clobber, "clobber_r%d", reg)) {
            checkasm_call_new(0);
        }
    }

    checkasm_report("clobber");
}

static void test_copy_emms(copy_func fun, const char *name)
{
    CHECKASM_ALIGN(uint8_t c_dst[256]);
    CHECKASM_ALIGN(uint8_t a_dst[256]);
    CHECKASM_ALIGN(uint8_t src[256]);
    INITIALIZE_BUF(src);

    checkasm_declare_emms(SELFTEST_CPU_FLAG_MMX, void, uint8_t *dest,
                          const uint8_t *src, size_t n);

    for (size_t w = 8; w <= 256; w *= 2) {
        if (checkasm_check_func(fun, "%s_%zu", name, w)) {
            CLEAR_BUF(c_dst);
            CLEAR_BUF(a_dst);

            checkasm_call_ref(c_dst, src, w);
            checkasm_call_new(a_dst, src, w);
            checkasm_check(uint8_t, c_dst, 0, a_dst, 0, 256, 1, "dst data");
            checkasm_bench_new(a_dst, src, w);
        }
    }

    checkasm_report("%s", name);
}

void selftest_check_x86(void)
{
    selftest_test_copy(get_copy_x86(), "copy", 1);
    test_copy_emms(get_copy_noemms_mmx(), "copy_noemms");
    check_stack_alignment();
    check_clobber(0, NUM_SAFE);

    if (!checkasm_should_fail(SELFTEST_CPU_FLAG_X86))
        return;

    selftest_test_noop(get_sigill_x86(), "sigill");
    selftest_test_noop(get_corrupt_stack_x86(), "corrupt_stack");
    selftest_test_copy(get_copy_noemms_mmx(), "noemms", 8);
    check_clobber(NUM_SAFE, NUM_REGS);

    if (checkasm_get_check_vzeroupper())
        selftest_test_copy(get_copy_novzeroupper_avx(), "novzeroupper", 32);
}

#endif
